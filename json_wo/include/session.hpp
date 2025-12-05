// session.hpp
#pragma once

#include <unordered_map>
#include <functional>
#include <boost/asio/steady_timer.hpp>
#include "ocpp_model.hpp"
#include "transport.hpp"
#include <mutex>

//Purpose of Session class: handles OCPP message correlation, state, timers
struct Session {
  enum class State { Disconnected, Connected, Booting, Ready };
  State state = State::Disconnected;
  boost::asio::io_context& io;
  std::shared_ptr<Transport> transport;
  std::mutex pending_mtx;
  struct Pending {
    std::unique_ptr<boost::asio::steady_timer> timer;
    std::function<void(const OcppFrame&)> resolve;//callback to resolve the pending call
  };
  std::unordered_map<std::string, Pending> pending;

  Session(boost::asio::io_context& io_, std::shared_ptr<Transport> t) : io(io_), transport(t) {
    transport->on_message([this](std::string_view sv){
        this->on_message(sv);
    });
    transport->on_close([this](){
        this->on_close();
    });
  }

    std::unique_ptr<boost::asio::steady_timer> timer;

    std::function<void()> on_heartbeat_response_;
    void on_heartbeat_response(std::function<void()> cb){
        on_heartbeat_response_ = std::move(cb);
    }

    void start(void)
    {
        transport->start();
    }

    void start_heartbeat(int interval)
    {
        timer = std::make_unique<boost::asio::steady_timer>(io, std::chrono::seconds(interval));
        timer->async_wait([interval, this](auto ec){
            if (ec) 
            {
                std::cerr << "Heartbeat timer canceled: " << ec.message() << "\n";
                return; // timer canceled = got reply
            }
            // send heartbeat
            send_call(HeartBeat{},
                                [interval, this](const OcppFrame& f){
                if (std::holds_alternative<CallResult>(f)) {
                    const auto& r = std::get<CallResult>(f);
                    std::cout << "HeartbeatResponse: " << r.payload << "\n";
                } else if (std::holds_alternative<CallError>(f)){
                    const auto& e = std::get<CallError>(f);
                    std::cerr << "Heartbeat Error: " << e.errorDescription << "\n";
                }
            });

            // restart timer
            start_heartbeat(interval);
        });
    }

/**
 * send_call
 *
 * Send an OCPP Call built from the given payload and arrange to receive a reply.
 *
 * Parameters:
 *  - p: payload to be wrapped into a Call (any type serializable by json(c)).
 *  - on_reply: completion callback invoked when a reply is received or on error/timeout.
 *              The callback receives an OcppFrame which will be either CallResult
 *              (successful reply) or CallError (error, timeout, or connection closed).
 *  - timeout: optional timeout duration after which a synthetic CallError is delivered
 *             to on_reply. Defaults to 10 seconds.
 *
 * Return value:
 *  - void
 */
   template<typename Payload>
  void send_call(const Payload& p,
                 std::function<void(const OcppFrame&)> on_reply,
                 std::chrono::seconds timeout = std::chrono::seconds(10))
  {
    auto id = generate_message_id();
    Call c = create_call(id, p);
    auto line = json(c).dump();
    // store pending
    Pending pend;
    pend.timer = std::make_unique<boost::asio::steady_timer>(io, timeout);
    pend.resolve = std::move(on_reply);
    std::lock_guard<std::mutex> lock(pending_mtx);
    auto [it, ok] = pending.emplace(id, std::move(pend));


    // start timer
    it->second.timer->async_wait([this,id](auto ec){
      if (ec) return; // canceled = got reply
      // synthesize timeout error and resolve
      // it hides data race and timing bug. the serializing and displaying out
      // makes things happen before the main and in time for it to p[rint]
      // std::cout<<"Triggering timeout for messageId "<<id<<"\n";
      CallError err{4, id, "Timeout", "Request timed out", json::object()};
      std::function<void(const OcppFrame &)> cb;
      {
        std::lock_guard<std::mutex> lock(pending_mtx);
        auto p = pending.find(id);
        if (p != pending.end()) { cb = p->second.resolve; pending.erase(p); }
      }
      if(cb) cb(OcppFrame{err}); });

    transport->send(line);
  }

  void on_frame(const OcppFrame &f)
  {
    if (std::holds_alternative<CallResult>(f))
    {
      const auto &r = std::get<CallResult>(f);
      std::lock_guard<std::mutex> lock(pending_mtx);
      auto it = pending.find(r.messageId);
      if (it != pending.end())
      {
        it->second.timer->cancel();
        it->second.resolve(f);
        pending.erase(it);
      }
    }
    else if (std::holds_alternative<CallError>(f))
    {
      const auto &e = std::get<CallError>(f);
      std::function<void(const OcppFrame &)> cb;
      {
        std::lock_guard<std::mutex> lock(pending_mtx);
        auto it = pending.find(e.messageId);
        if (it != pending.end())
        {
          it->second.timer->cancel();
          cb = it->second.resolve;
          pending.erase(it);
        }
      }
      if (cb)
        cb(f);
    }
  }

  void on_message(std::string_view message) {
    std::string line{message};
    std::cout << __func__ << "RX<<<" << line << "\n";
    try{
        json j = json::parse(line);
        OcppFrame f = parse_frame(j);
        on_frame(f);
    } catch ( const std::exception &e ) {
        std::cerr << "Failed to parse incoming message: " << e.what() << "\n";
    }
  }

  //TODO: shouldn't there be a on_connected() here? that indicates a new client
  //has connected and we need to create a session for it?
  void on_close()
  {
    state = State::Disconnected;
    // you dump everything into a vector and then clear from there.
    // i dont want to call resolve inside the fucking lock_guard
    // pair(id, resolve)
    std::vector<std::pair<std::string, std::function<void(const OcppFrame &)>>> to_close;
    {
      std::lock_guard<std::mutex> lock(pending_mtx);
      for (auto &[id, p] : pending)
      {
        to_close.emplace_back(id, p.resolve);
        p.timer->cancel();
      }
      pending.clear();
    }
    for (auto &v : to_close)
    {
      CallError err{4, v.first, "ConnectionClosed", "Connection closed before reply", json::object()};
      if (v.second)
        v.second(OcppFrame{err});
    }
  }
};
