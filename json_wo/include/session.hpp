// session.hpp
#pragma once

#include <unordered_map>
#include <functional>
#include <boost/asio/steady_timer.hpp>
#include "ocpp_model.hpp"
#include "transport.hpp"

//Purpose of Session class: handles OCPP message correlation, state, timers
struct Session {
  enum class State { Disconnected, Connected, Booting, Ready };
  State state = State::Disconnected;
  boost::asio::io_context& io;
  std::shared_ptr<Transport> transport;
  struct Pending {
    std::unique_ptr<boost::asio::steady_timer> timer;
    std::function<void(const OcppFrame&)> resolve;//callback to resolve the pending call
  };
  std::unordered_map<std::string, Pending> pending;

  Session(boost::asio::io_context& io_, std::shared_ptr<Transport> t) : io(io_), transport(t) {
    transport->on_message([this](std::string_view sv){
        this->on_message(sv);
    });
  }

    std::unique_ptr<boost::asio::steady_timer> timer;

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
    auto [it, ok] = pending.emplace(id, std::move(pend));


    // start timer
    it->second.timer->async_wait([this,id](auto ec){
      if (ec) return; // canceled = got reply
      // synthesize timeout error and resolve
      CallError err{4, id, "Timeout", "Request timed out", json::object()};
      auto p = pending.find(id);
      if (p != pending.end()) { p->second.resolve(OcppFrame{err}); pending.erase(p); }
    });

    transport->send(line);
  }

  void on_frame(const OcppFrame& f) {
    if (std::holds_alternative<CallResult>(f)) {
      const auto& r = std::get<CallResult>(f);
      auto it = pending.find(r.messageId);
      if (it != pending.end()) {it->second.timer->cancel(); it->second.resolve(f); pending.erase(it); }
    } else if (std::holds_alternative<CallError>(f)) {
      const auto& e = std::get<CallError>(f);
      auto it = pending.find(e.messageId);
      if (it != pending.end()) { it->second.timer->cancel(); it->second.resolve(f); pending.erase(it); }
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

  void on_close(){
    state = State::Disconnected;
    for (auto& [id, p] : pending) {
        CallError err{4, id, "ConnectionClosed", "Connection closed before reply", json::object()};
        p.resolve(OcppFrame{err});
        p.timer->cancel();
    }
    pending.clear();
  }
};
