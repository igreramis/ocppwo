// session.hpp
#pragma once

#include <unordered_map>
#include <functional>
#include <boost/asio/steady_timer.hpp>
#include "ocpp_model.hpp"
#include "transport.hpp"
#include <mutex>


/*
 * Session
 *
 * Purpose:
 *   Manage OCPP message correlation, per-request timeouts, heartbeat scheduling
 *   and minimal session state (Disconnected / Connected / Booting / Ready).
 *
 * Inputs (events/operations the Session expects):
 *   - transport->on_message -> Session::on_message(std::string_view)
 *   - transport->on_close   -> Session::on_close()
 *   - Caller APIs: start(), start_heartbeat(), send_call(...), on_transport_connected()
 *
 * Outputs (what Session notifies / delivers):
 *   - Per-request completion via the on_reply callback passed to send_call:
 *       invoked exactly once with a CallResult or a CallError (timeout/close).
 *   - Logs / side-effects (console output). No global observer callbacks are exposed
 *     by Session itself — higher-level glue observes connection lifecycle via Transport.
 *
 * Public methods / hooks for integrators:
 *   - start()                         : start underlying transport.
 *   - start_heartbeat(int interval)   : begin recurring HeartBeat calls.
 *   - send_call(Payload, on_reply, t) : send a request and register per-call resolver.
 *   - on_message(std::string_view)    : parse raw payload and forward to on_frame (testable).
 *   - on_frame(const OcppFrame&)      : deliver already-parsed frame (testable).
 *   - on_close()                      : handle transport close, cancel pending and notify callers.
 *   - on_transport_connected()        : notify Session that transport is connected. (start boot).
 *
 * Concurrency / execution model:
 *   - Callers must coordinate usage on the same io_context/strand or otherwise
 *     synchronize; Session protects the pending map with pending_mtx.
 *   - Timers and transport callbacks run on the session's io context; callbacks
 *     are invoked outside locks to avoid reentrancy.
 *
 * Notes for integrators:
 *   - Session is focused on correlation/timeouts; higher-level protocol flows
 *     (boot sequence, transitions to Ready, reconnect orchestration) are the
 *     responsibility of the owning glue/controller/test harness.
 */
struct SessionSignals {
    std::function<void()> on_boot_accepted;
};

struct Session {
  enum class State { Disconnected, Connected, Booting, Ready };
  State state = State::Disconnected;
  boost::asio::io_context& io;
  std::shared_ptr<Transport> transport;
  std::shared_ptr<SessionSignals> session_signals;
  std::mutex pending_mtx;
  struct Pending {
    std::unique_ptr<boost::asio::steady_timer> timer;
    std::function<void(const OcppFrame&)> resolve;//callback to resolve the pending call
    std::string expected_tag;
    std::function<bool(const json& payload, std::string& err)> validate_payload;
  };
  std::unordered_map<std::string, Pending> pending;

  //method definitions
  int heartbeat_interval_s{0};

  Session(boost::asio::io_context& io_, std::shared_ptr<Transport> t, std::shared_ptr<SessionSignals> e) : io(io_), transport(t), session_signals(e) {
    // transport->on_message([this](std::string_view sv){
    //     this->on_message(sv);
    // });
    // transport->on_close([this](){
    //     this->on_close();
    // });
    //if we are passing it transport, then it should get the connected signal or not? and if it is getting the connected signals then the applkication shoulds
    //just waiut for the session boot notification accepted signal
  }

    //for now, what should this method do?
    // void reset() {
    //   std::lock_guard<std::mutex> lg
    // }

    std::unique_ptr<boost::asio::steady_timer> timer;

    /*
    * start()
    *
    * Brief:
    *   Begin the session's transport activity. This calls the underlying
    *   Transport::start() so the socket/websocket is opened (or connect attempts
    *   are initiated) and incoming/outgoing message handling becomes active.
    *
    * Trigger:
    *   Called by higher-level code (glue, harness, or tests) when you want the
    *   session to become active and start exchanging frames.
    *
    * Purpose:
    *   - Start the transport so the previously-installed on_message/on_close
    *     callbacks (wired in the Session constructor) can receive events.
    *   - Put the session into a running state so send_call, heartbeats, etc.
    *     may be used (state transition responsibilities may be handled elsewhere).
    *
    * Difference from related methods:
    *   - start() only starts the transport; it does not send protocol-level
    *     messages (use send_call/start_heartbeat) or handle incoming frames
    *     (on_message/on_frame).
    *
    * Concurrency / lifecycle notes:
    *   - Should be invoked on the session's io context or otherwise synchronized
    *     with transport usage. It is a lightweight operation that delegates to
    *     Transport; any higher-level state transitions should be coordinated by
    *     the caller(the code that owns/creates the Session (glue, controller, tests, harness)
    *     must perform those higher-level steps in the right order and on the right execution context).
    */
   //TODO: is this needed given ReconnectController now implements a similar actuation?
    void start(void)
    {
      ;//
        // transport->start();
    }

    /*
    * start_heartbeat(int interval)
    *
    * Brief:
    *   Start a repeating heartbeat timer that sends a HeartBeat Call every
    *   `interval` seconds. Each timer expiration invokes send_call(HeartBeat)
    *   and restarts the heartbeat timer.
    *
    * Trigger:
    *   Called by higher-level session code (e.g. after connection/boot completes)
    *   or tests to begin periodic liveness checks.
    *
    * Purpose:
    *   - Keep the peer informed (and detect liveness) by sending periodic HeartBeat
    *     requests and processing their replies via the usual send_call -> on_frame path.
    *   - Automatically reschedule itself so heartbeats continue until the timer
    *     is cancelled or the session is closed.
    *
    * Concurrency / lifecycle notes:
    *   - Timer callbacks run on the session's io context; the timer is owned by the Session
    *     (the `timer` unique_ptr). Cancel or reset the timer (timer->cancel() or reset pointer)
    *     to stop heartbeats (e.g. on close).
    *   - Handler checks the ec provided to async_wait and returns on cancellation to avoid
    *     sending extra heartbeats during shutdown.
    */
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

  /*
  * send_call(const Payload& p, std::function<void(const OcppFrame&)> on_reply, std::chrono::seconds timeout)
  *
  * Brief:
  *   Send an OCPP Call built from `p`, register a completion callback, and arrange
  *   a per-call timeout. The provided on_reply is invoked exactly once with either
  *   a CallResult (successful reply) or a CallError (timeout or connection closed).
  *
  * Trigger:
  *   Called by session logic (heartbeats, boot, tests, user code) when the session
  *   needs to send a request and receive an asynchronous reply.
  *
  * Purpose:
  *   - Create a new message id and wire a Call JSON string for transport.
  *   - Store a Pending entry (resolve callback + steady_timer) in the pending map.
  *   - Start a timer that will deliver a synthetic CallError if no reply arrives.
  *   - Send the serialized Call over transport.
  *
  * Parameters:
  *   - p: payload to be wrapped into a Call (must be serializable via json(c)).
  *   - on_reply: completion callback invoked on reply, timeout, or connection close.
  *   - timeout: duration after which a synthetic CallError is generated (defaults to 10s).
  *
  * Return:
  *   - void. Completion is delivered asynchronously via on_reply.
  *
  * Concurrency & safety:
  *   - Protects the pending map with pending_mtx; timers and transport callbacks may run
  *     on the IO thread so access is synchronized.
  *   - Timers cancel the pending entry under the lock, and callbacks are invoked
  *     outside the lock to avoid reentrancy and deadlocks.
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
    using Resp = typename ExpectedResponse<Payload>::type;
    pend.expected_tag = ExpectedResponse<Payload>::value;
    pend.validate_payload = [](const json& payload, std::string& err) -> bool {
      try {
        payload.get<Resp>();
        return true;
      } catch (const std::exception &e) {
        err = e.what();
        return false;
      }

    };
    {
      std::lock_guard<std::mutex> lock(pending_mtx);
      auto [it, ok] = pending.emplace(id, std::move(pend));
    

      // start timer
      it->second.timer->async_wait([this,id](auto ec){
        if (ec) return; // canceled = got reply

        CallError err{4, id, "Timeout", "Request timed out", json::object()};
        std::function<void(const OcppFrame &)> cb;
        {
          std::lock_guard<std::mutex> lock(pending_mtx);
          auto p = pending.find(id);
          if (p != pending.end()) { cb = std::move(p->second.resolve); pending.erase(p); }
        }
        if(cb) cb(OcppFrame{err}); });
    }

    transport->send(line);
  }

  /* 
  * on_message(std::string_view message)
  *
  * Brief:
  *   Accepts a raw incoming transport payload (text frame), parses it as JSON
  *   into an OCPP frame, and forwards the parsed frame to on_frame() for handling.
  *
  * Trigger:
  *   Called by the Transport layer (via the transport->on_message callback) whenever
  *   a new text/websocket message arrives.
  *
  * Purpose:
  *   Centralize parse + validation + error reporting for incoming wire data, and
  *   convert raw messages into the internal OcppFrame form used by the session logic.
  *
  * Difference from similar methods:
  *   - on_message handles the raw string payload and JSON parsing.
  *   - on_frame operates on an already-parsed OcppFrame and implements correlation/response logic.
  */
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

  /*
  * on_frame(const OcppFrame& f)
  *
  * Brief:
  *   Consume a parsed OCPP frame and perform session-level handling:
  *   correlate replies with outstanding calls, cancel pending timeouts, and
  *   invoke the stored completion callback with either CallResult or CallError.
  *
  * Trigger:
  *   Called by on_message() after JSON parsing (or directly by tests/tools that
  *   already have an OcppFrame).
  *
  * Purpose:
  *   - Match incoming CallResult/CallError to the pending map by messageId.
  *   - Cancel the associated timer and invoke the resolve callback exactly once.
  *   - Remove the pending entry and perform any cleanup needed for that request.
  *
  * Difference from on_message():
  *   - on_message: accepts raw transport payload, parses JSON and builds OcppFrame.
  *   - on_frame: accepts an already-parsed OcppFrame and implements correlation,
  *     timeout/cancellation logic, and delivery to request-specific callbacks.
  *
  * Concurrency note:
  *   on_frame must protect access to the pending table (uses pending_mtx) because
  *   timers and transport callbacks may run on the IO thread.
  */
  void on_frame(const OcppFrame &f)
  {
    if (std::holds_alternative<CallResult>(f))
    {
      const auto &r = std::get<CallResult>(f);
      std::function<void(const OcppFrame &)> cb;
      std::function<bool(const json& payload, std::string& err)> validate_;
      std::string expected_tag;
      {
        std::lock_guard<std::mutex> lock(pending_mtx);
        auto it = pending.find(r.messageId);
        if (it != pending.end())
        {
          it->second.timer->cancel();
          cb = std::move(it->second.resolve);
          validate_ = std::move(it->second.validate_payload);
          expected_tag = std::move(it->second.expected_tag);
          pending.erase(it);
        }
        else{
          unmatched_replies_.fetch_add(1, std::memory_order_relaxed);
          return;        
        }
      }

      if (!cb)
        return;
      
      //validate payload
      if(validate_){
        std::string err;
        if(!validate_(r.payload, err)){
          //invalid payload, generate CallError
          CallError cerr{4, r.messageId, "ProtocolError", "Reply payload unexpected type", json::object({{"expected", expected_tag}, {"detail", err}})};
          cb(OcppFrame{cerr});
          return;
        }
      }
      cb(f);
      return;
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
        else{
          unmatched_replies_.fetch_add(1, std::memory_order_relaxed);
          return;        
        }
      }
      if (cb)
        cb(f);
    }
  }
  
  /*
  * on_close()
  *
  * Brief:
  *   Handle transport-level connection closure: mark session disconnected,
  *   cancel outstanding pending timers, and deliver a CallError for each
  *   pending request so every send_call is resolved exactly once.
  *
  * Trigger:
  *   Called by the Transport layer's on_close callback when the socket/websocket
  *   is closed (graceful or unexpected), or by higher-level shutdown logic.
  *
  * Purpose:
  *   - Prevent leaked pending requests by cancelling timers and notifying callers.
  *   - Move callbacks out of the locked section and invoke them afterwards to
  *     avoid reentrancy/deadlocks.
  *
  * Concurrency note:
  *   Locks pending_mtx to snapshot and clear pending entries, cancels timers
  *   under the lock, then invokes callbacks outside the lock.
  */
  void on_close()
  {
    state = State::Disconnected;
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

  //   Inbound lifecycle hook from the transport/reconnect layer indicating the underlying
  //   WebSocket/TCP transport is now connected and ready to carry OCPP frames.
  //
  // Trigger:
  //   Called by owning glue (e.g., ReconnectGlue / TestClient) when the transport reports
  //   a successful connection (typically after WsClient handshake completes and
  //   ReconnectController emits on_connected).
  //
  // Purpose:
  //   - Transition the Session state machine into the "booting" phase.
  //   - Initiate the OCPP boot sequence by sending BootNotification (via send_call),
  //     so the Session can later become Ready once boot is accepted.
  //
  // Outputs / side effects:
  //   - Sends a BootNotification request on the transport.
  //   - On BootNotification accepted: may invoke session_signals->on_boot_accepted (if set)
  //     and/or update internal state/heartbeat interval depending on implementation.
  //
  // Notes:
  //   - This method is intentionally separated from Transport::on_connected so that
  //     reconnect logic can decide when a "connected transport" should translate into
  //     a protocol-level boot attempt.
  void on_transport_connected()
  {
    state = State::Booting;
    send_boot();
  }

  //   Initiate the OCPP BootNotification flow by sending a BootNotification Call and
  //   installing a reply handler that drives the Session into the correct post-boot state.
  //
  // Trigger:
  //   Typically called from on_transport_connected() (or equivalent glue) once the
  //   underlying transport is connected and the session is ready to begin protocol
  //   negotiation with the CSMS.
  //
  // Purpose:
  //   - Move the session into State::Booting (if not already).
  //   - Send BootNotification using send_call(...) so the Session can correlate the
  //     CallResult/CallError response and act on it.
  //
  // Outputs / side effects:
  //   - Outbound message: BootNotification request frame written to transport.
  //   - On accepted CallResult:
  //     - transitions Session toward State::Ready (implementation-dependent),
  //     - may start or configure heartbeat interval,
  //     - may notify higher layers via session_signals->on_boot_accepted (if provided).
  //   - On CallError / timeout / close:
  //     - keeps or returns session to a non-ready state and relies on reconnect logic.
  //
  // Concurrency / lifetime notes:
  //   - The reply callback runs on the io_context that processes inbound frames; it should
  //     be quick and should avoid blocking. Any shared state updates should follow the
  //     Session’s locking/strand rules.
  void send_boot()
  {
    send_call(BootNotification{"X100", "OpenAI"},
    [this](const OcppFrame &f) {
      if (std::holds_alternative<CallResult>(f))
      {
        const auto &r = std::get<CallResult>(f);
        BootNotificationResponse resp = r.payload;
        if (resp.status == "Accepted")
        {
          std::cout << "BootNotification accepted, current time: " << resp.currentTime << "\n";
          state = State::Ready;
          heartbeat_interval_s = resp.interval;
          start_heartbeat(resp.interval);
          if (session_signals && session_signals->on_boot_accepted)
          {
            session_signals->on_boot_accepted();
          }
        }
        else if (resp.status == "Pending")
        {
          std::cout << "BootNotification pending, interval: " << resp.interval << "\n";
        }
        else
        {
          std::cout << "BootNotification rejected\n";
        }
      }
      else if (std::holds_alternative<CallError>(f))
      {
        const auto &e = std::get<CallError>(f);
        std::cerr << "BootNotification Error: " << e.errorDescription << "\n";
      }
    });
  }

  uint64_t unmatched_replies() const {
    return unmatched_replies_.load(std::memory_order_relaxed);
  }

private:
// why is this atomic? because, callbacks/methods in Session.hpp could run on multiple
//different threads in parallel. this happens when a given io_context is run on xX
//threads for increasing throughput for apps
  std::atomic<uint64_t> unmatched_replies_{0};
};
