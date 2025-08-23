// session.hpp
#pragma once

#include <unordered_map>
#include <functional>
#include <boost/asio/steady_timer.hpp>
#include "ocpp_model.hpp"

struct Session {
  enum class State { Disconnected, Connected, Booting, Ready };
  State state = State::Disconnected;
  boost::asio::io_context& io;
  boost::asio::ip::tcp::socket& sock;

  struct Pending {
    std::unique_ptr<boost::asio::steady_timer> timer;
    std::function<void(const OcppFrame&)> resolve;//callback to resolve the pending call
  };
  std::unordered_map<std::string, Pending> pending;

  Session(boost::asio::io_context& io_, boost::asio::ip::tcp::socket& s)
    : io(io_), sock(s) {}

  template<typename Payload>
  void send_call(const std::string& action, const Payload& p,
                 std::function<void(const OcppFrame&)> on_reply,
                 std::chrono::seconds timeout = std::chrono::seconds(10))
  {
    auto id = generate_message_id();
    // Call c = create_call(id, action, OcppPayload{p}); // or your template overload
    Call c = create_call(id, p);
    auto line = json(c).dump() + "\n";
    // store pending
    Pending pend;
    pend.timer = std::make_unique<boost::asio::steady_timer>(io, timeout);
    pend.resolve = std::move(on_reply);
    pending.emplace(id, std::move(pend));

    // start timer
    auto it = pending.find(id);
    it->second.timer->async_wait([this,id](auto ec){
      if (ec) return; // canceled = got reply
      // synthesize timeout error and resolve
      CallError err{4, id, "Timeout", "Request timed out", json::object()};
      auto p = pending.find(id);
      if (p != pending.end()) { p->second.resolve(OcppFrame{err}); pending.erase(p); }
    });

    // write
    boost::asio::async_write(sock, boost::asio::buffer(line),
      [line](auto ec, std::size_t len){ 
        if( ec )
            std::cerr << "Write failed: " << ec.message() << "\n";
        else
            std::cout << "TX>>>" << line << "\n";
      });
  }

  void on_frame(const OcppFrame& f) {
    if (std::holds_alternative<CallResult>(f)) {
      const auto& r = std::get<CallResult>(f);
      auto it = pending.find(r.messageId);
      if (it != pending.end()) {it->second.timer->cancel(); it->second.resolve(f); pending.erase(it); }
      // TODO: if action == BootNotification, move to Ready and schedule heartbeat
    } else if (std::holds_alternative<CallError>(f)) {
      const auto& e = std::get<CallError>(f);
      auto it = pending.find(e.messageId);
      if (it != pending.end()) { it->second.timer->cancel(); it->second.resolve(f); pending.erase(it); }
    }
  }
};
