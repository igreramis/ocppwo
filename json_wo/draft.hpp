#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <atomic>
#include <random>

namespace ocppwo {

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::milliseconds;

struct ReconnectPolicy {
  Ms    initial_backoff{500};
  Ms    max_backoff{30'000};
  float jitter_ratio{0.20f};          // ±20%
  Ms    resume_delay{250};            // wait after BootAccepted before resuming traffic
};

enum class ConnState {
  Disconnected,
  Connecting,
  Connected,
  Closing
};

enum class CloseReason {
  Clean,
  TransportError,
  ProtocolError
};

struct ReconnectSignals {
  // Lifecycle
  std::function<void()> on_connecting;         // attempting TCP/WS connect
  std::function<void()> on_connected;          // TCP/WS established
  std::function<void(CloseReason)> on_closed;  // transport closed
  // Online gating (after BootAccepted)
  std::function<void()> on_online;             // after BootAccepted + resume_delay
  std::function<void()> on_offline;            // immediately on close
  // Diagnostics
  std::function<void(Ms backoff)> on_backoff_scheduled;
};

struct TransportOps {
  // Must be serialized externally (e.g., strand) in your integration.
  std::function<void(const std::string& url,
                     std::function<void(bool ok)>)> async_connect;
  std::function<void(std::function<void()>)> async_close;
  // Timer facilities
  std::function<void(Ms delay, std::function<void()>)> post_after; // schedules a callback
  std::function<Clock::time_point()> now;                          // for tests/metrics
};

// A minimal controller that owns no threads; relies on provided async ops & timers.
class ReconnectController {
public:
  explicit ReconnectController(ReconnectPolicy policy,
                               TransportOps ops,
                               ReconnectSignals sigs = {})
      : policy_(policy), ops_(std::move(ops)), sigs_(std::move(sigs)) {}

  void start(const std::string& url) {
    url_ = url;
    if (state_ != ConnState::Disconnected) return;
    try_connect_();
  }

  void stop() {
    state_ = ConnState::Closing;
    online_ = false;
    if (sigs_.on_offline) sigs_.on_offline();
    if (ops_.async_close) {
      ops_.async_close([this] {
        state_ = ConnState::Disconnected;
        attempt_ = 0;
      });
    } else {
      state_ = ConnState::Disconnected;
      attempt_ = 0;
    }
  }

  // Transport hooks (call these from your WsClient callbacks)
  void on_transport_open() {
    state_ = ConnState::Connected;
    attempt_ = 0;
    if (sigs_.on_connected) sigs_.on_connected();
    // You should kick your BootNotification externally, then call on_boot_accepted().
  }

  void on_transport_close(CloseReason why) {
    online_ = false;
    if (sigs_.on_offline) sigs_.on_offline();
    if (sigs_.on_closed)  sigs_.on_closed(why);
    if (state_ == ConnState::Closing) {
      state_ = ConnState::Disconnected;
      return;
    }
    state_ = ConnState::Disconnected;
    schedule_reconnect_();
  }

  // Call when BootNotification is accepted.
  void on_boot_accepted() {
    // Gate resumption with a small delay to prevent thundering herds.
    if (ops_.post_after) {
      ops_.post_after(policy_.resume_delay, [this]{
        online_ = true;
        if (sigs_.on_online) sigs_.on_online();
      });
    } else {
      online_ = true;
      if (sigs_.on_online) sigs_.on_online();
    }
  }

  // Gate for your public send() path. If false: return local ConnectionClosed immediately.
  bool can_send() const { return state_ == ConnState::Connected && online_; }

  ConnState state()  const { return state_; }
  bool      online() const { return online_; }
  int       attempt()const { return attempt_; }

  // For tests/telemetry
  Ms current_backoff() const { return last_backoff_; }

private:
  void try_connect_() {
    state_ = ConnState::Connecting;
    if (sigs_.on_connecting) sigs_.on_connecting();
    if (!ops_.async_connect) return;
    const auto url_copy = url_;
    ops_.async_connect(url_copy, [this](bool ok){
      if (!ok) {
        state_ = ConnState::Disconnected;
        schedule_reconnect_();
        return;
      }
      on_transport_open();
    });
  }

  void schedule_reconnect_() {
    ++attempt_;
    last_backoff_ = compute_backoff_(attempt_);
    if (sigs_.on_backoff_scheduled) sigs_.on_backoff_scheduled(last_backoff_);
    if (ops_.post_after) {
      ops_.post_after(last_backoff_, [this]{ try_connect_(); });
    }
  }

  Ms compute_backoff_(int attempt) const {
    using std::min;
    const auto base = policy_.initial_backoff.count();
    const auto cap  = policy_.max_backoff.count();
    // exponential: base * 2^(attempt-1)
    auto exp = static_cast<int64_t>(base) << (attempt > 0 ? (attempt - 1) : 0);
    if (exp < 0) exp = cap;
    auto unclamped = static_cast<int64_t>(min<int64_t>(exp, cap));
    // jitter ±ratio
    const float r = jitter_(); // [0,1)
    const float signed_jitter = (r * 2.f - 1.f) * policy_.jitter_ratio;
    const auto jittered = static_cast<int64_t>(unclamped * (1.f + signed_jitter));
    return Ms{ jittered < 0 ? 0 : jittered };
  }

  float jitter_() const {
    thread_local std::mt19937 rng{0xC0FFEEu};
    std::uniform_real_distribution<float> dist(0.f, 1.f);
    return dist(rng);
  }

private:
  ReconnectPolicy  policy_;
  TransportOps     ops_;
  ReconnectSignals sigs_;
  std::string      url_;
  std::atomic<bool> online_{false};
  std::atomic<ConnState> state_{ConnState::Disconnected};
  std::atomic<int> attempt_{0};
  Ms last_backoff_{0};
};

} // namespace ocppwo

#include "gtest/gtest.h"
#include "reconnect.hpp"

// Include your real headers here:
// #include "ws_client.hpp"
// #include "session.hpp"
// #include "test_server.hpp"

using namespace std::chrono_literals;
using ocppwo::ReconnectController;
using ocppwo::ReconnectPolicy;
using ocppwo::TransportOps;
using ocppwo::ReconnectSignals;
using ocppwo::ConnState;
using ocppwo::CloseReason;

namespace {

struct FakeClock {
  ocppwo::Clock::time_point now{ocppwo::Clock::now()};
  ocppwo::Clock::time_point operator()() const { return now; }
};

struct TestHarness {
  // Replace these with your real test server/client.
  // Provide operations to: start server, force-close, capture heartbeats, etc.
  std::function<void()> server_start      = []{};
  std::function<void()> server_force_close= []{};
  std::function<void()> server_close_on_accept = []{};
  std::function<void()> server_close_after_first_msg = []{};
  std::function<void()> clear_telemetry   = []{};
  std::function<int()>  heartbeat_count   = []{ return 0; };
  std::function<void()> client_send_ping  = []{}; // or any trivial frame
  std::function<void()> trigger_boot      = []{}; // cause Session to send BootNotification
  std::function<void()> accept_boot       = []{}; // simulate CSMS sending BootAccepted
};

struct TimerPump {
  // A tiny manual scheduler for deterministic tests.
  struct Item { ocppwo::Ms delay; std::function<void()> cb; };
  std::vector<Item> q;

  void post_after(ocppwo::Ms d, std::function<void()> cb) { q.push_back({d, std::move(cb)}); }
  void run_all() { for (auto& it : q) it.cb(); q.clear(); }
  template<class Pred>
  void run_while(Pred p) {
    // In a real test, advance fake time and trigger due timers in order
    while (p() && !q.empty()) { auto it = q.front(); q.erase(q.begin()); it.cb(); }
  }
};

} // namespace

// 1) Reconnect_Basic
TEST(Reconnect, Basic) {
  TimerPump timers;
  FakeClock clock;
  TestHarness H;

  bool saw_connecting=false, saw_connected=false, went_online=false;
  ReconnectPolicy pol;
  pol.initial_backoff = 200ms;
  pol.resume_delay    = 100ms;

  TransportOps ops;
  ops.async_connect = [&](const std::string& url, std::function<void(bool)> done){
    // Simulate immediate success for first connect, then server will close and we’ll reconnect
    (void)url;
    done(true);
  };
  ops.async_close = [&](std::function<void()> done){ done(); };
  ops.post_after  = [&](ocppwo::Ms d, std::function<void()> cb){ timers.post_after(d, std::move(cb)); };
  ops.now         = [&]{ return clock.now; };

  ReconnectSignals sigs;
  sigs.on_connecting = [&]{ saw_connecting = true; };
  sigs.on_connected  = [&]{ saw_connected = true; /* trigger Boot flow */ };
  sigs.on_online     = [&]{ went_online = true; };

  ReconnectController rc{pol, ops, sigs};

  // Start + first connect
  rc.start("ws://localhost:12345");
  EXPECT_TRUE(saw_connecting);
  EXPECT_TRUE(saw_connected);
  EXPECT_EQ(rc.state(), ConnState::Connected);

  // Simulate BootAccepted → online after resume_delay
  rc.on_boot_accepted();
  timers.run_all();
  EXPECT_TRUE(went_online);
  EXPECT_TRUE(rc.can_send());

  // Now force server close → controller should schedule reconnect
  rc.on_transport_close(CloseReason::TransportError);
  EXPECT_EQ(rc.state(), ConnState::Disconnected);
  EXPECT_FALSE(rc.can_send());

  // Reconnect due after backoff
  // Simulate backoff timer firing:
  timers.run_all();
  // async_connect returns true again → on_connected()
  // Simulate BootAccepted again:
  rc.on_boot_accepted();
  timers.run_all();
  EXPECT_TRUE(rc.can_send());
}

// 2) NoDoubleSend_AcrossReconnect
TEST(Reconnect, NoDoubleSendAcrossReconnect) {
  TimerPump timers;
  FakeClock clock;
  TestHarness H;

  ReconnectPolicy pol;
  pol.initial_backoff = 50ms;
  pol.resume_delay    = 50ms;

  bool connected=false;
  TransportOps ops;
  ops.async_connect = [&](const std::string&, std::function<void(bool)> done){ connected=true; done(true); };
  ops.async_close   = [&](std::function<void()> done){ connected=false; done(); };
  ops.post_after    = [&](ocppwo::Ms d, std::function<void()> cb){ timers.post_after(d, std::move(cb)); };
  ops.now           = [&]{ return clock.now; };

  ReconnectController rc{pol, ops, {}};

  rc.start("ws://x");
  rc.on_boot_accepted();
  timers.run_all();
  ASSERT_TRUE(rc.can_send());

  // Emulate your Session/ClientUnderTest behavior:
  // Send N frames, then transport closes mid-stream.
  const int N = 10;
  // TODO: enqueue N writes via your client; ensure your implementation resolves all with ConnectionClosed on close.
  rc.on_transport_close(CloseReason::TransportError);

  // Assert your client cleared in-flight writes and did NOT auto-resend after reconnect:
  timers.run_all(); // reconnect
  rc.on_boot_accepted();
  timers.run_all();
  ASSERT_TRUE(rc.can_send());

  // TODO: verify no duplicates observed on server side post-reconnect (e.g., compare seq ids).
  SUCCEED();
}

// 3) Backoff_ExpJitter
TEST(Reconnect, BackoffExpJitter) {
  TimerPump timers;
  FakeClock clock;

  ReconnectPolicy pol;
  pol.initial_backoff = 100ms;
  pol.max_backoff     = 1600ms;
  pol.jitter_ratio    = 0.20f;

  std::vector<ocppwo::Ms> intervals;

  TransportOps ops;
  ops.async_connect = [&](const std::string&, std::function<void(bool)> done){
    // Fail a few times to drive backoff
    static int count=0;
    if (count++ < 4) { done(false); } else { done(true); }
  };
  ops.async_close = [&](std::function<void()> done){ done(); };
  ops.post_after  = [&](ocppwo::Ms d, std::function<void()> cb){ intervals.push_back(d); timers.post_after(d, std::move(cb)); };
  ops.now         = [&]{ return clock.now; };

  ReconnectController rc{pol, ops, {}};
  rc.start("ws://x");

  // Drain timers through the series of failures until we finally connect:
  for (int i=0; i<5; ++i) timers.run_all();

  ASSERT_GE(intervals.size(), 4u);
  // Expect roughly exponential growth, bounded, with jitter.
  // Loose checks: each interval should be >= previous*~0.8 and <= next cap.
  for (size_t i=1; i<intervals.size(); ++i) {
    EXPECT_LE(intervals[i-1], pol.max_backoff);
    EXPECT_LE(intervals[i],   pol.max_backoff);
    // Monotonic-ish: allow jitter to slightly reduce, but not collapse drastically.
    EXPECT_GE(intervals[i].count(), static_cast<int64_t>(intervals[i-1].count() * 0.6));
  }
}

// 4) ResumeDelay_RespectsPolicy
TEST(Reconnect, ResumeDelayRespectsPolicy) {
  TimerPump timers;
  FakeClock clock;

  ReconnectPolicy pol;
  pol.resume_delay = 300ms;

  bool online=false;
  TransportOps ops;
  ops.async_connect = [&](const std::string&, std::function<void(bool)> done){ done(true); };
  ops.async_close   = [&](std::function<void()> done){ done(); };
  ops.post_after    = [&](ocppwo::Ms d, std::function<void()> cb){ timers.post_after(d, std::move(cb)); };
  ops.now           = [&]{ return clock.now; };

  ocppwo::ReconnectSignals sigs;
  sigs.on_online = [&]{ online=true; };

  ReconnectController rc{pol, ops, sigs};
  rc.start("ws://x");

  // Immediately after connect, not online yet:
  EXPECT_FALSE(online);
  rc.on_boot_accepted();

  // Before resume_delay elapsed:
  EXPECT_FALSE(online);

  // Fire timers → on_online should run:
  timers.run_all();
  EXPECT_TRUE(online);
  EXPECT_TRUE(rc.can_send());
}

// glue.hpp
struct ReconnectGlue {
  boost::asio::io_context& io;
  std::shared_ptr<WsClient> ws;
  ReconnectController rc;
  std::unique_ptr<boost::asio::steady_timer> conn_timer;

  ReconnectGlue(boost::asio::io_context& io_,
                std::shared_ptr<WsClient> ws_,
                ReconnectPolicy pol)
    : io(io_), ws(std::move(ws_))
    , rc(pol,
         /* TransportOps */
         TransportOps{
           /* async_connect */
           [this](const std::string& url, std::function<void(bool)> done) {
             // (Optional) parse url into host/port if you create WsClient here.
             // Here: ws is already constructed with host/port.

             // One-shot latch so we call 'done' only once.
             auto completed = std::make_shared<bool>(false);

             // Bridge ws->on_connected to 'done(true)'
             ws->on_connected([this, done, completed](){
               if (*completed) return;
               *completed = true;
               if (conn_timer) conn_timer->cancel();
               done(true);
             });

             // Bridge ws->on_close to 'done(false)' if connect not yet completed.
             ws->on_close([this, done, completed](){
               if (*completed) return;
               *completed = true;
               if (conn_timer) conn_timer->cancel();
               done(false);
             });

             // Connection timeout as a fallback for resolve/handshake errors
             conn_timer = std::make_unique<boost::asio::steady_timer>(io, std::chrono::seconds(5));
             conn_timer->async_wait([done, completed](auto ec){
               if (ec) return; // canceled by success/close
               if (*completed) return;
               *completed = true;
               done(false);
             });

             // Kick off connect
             ws->start(); // this will resolve/connect/handshake and then call on_connected() on success
           },

           /* async_close */
           [this](std::function<void()> done) {
             auto completed = std::make_shared<bool>(false);
             // Ensure we call done() when closed
             auto prev = ws->on_closed_; // if you need to chain, keep prior
             ws->on_close([done, completed, prev](){
               if (prev) prev();
               if (*completed) return;
               *completed = true;
               done();
             });
             ws->close();
           },

           /* post_after */
           [this](Ms d, std::function<void()> cb) {
             auto t = std::make_shared<boost::asio::steady_timer>(io, d);
             t->async_wait([t, cb = std::move(cb)](auto ec){ if (!ec) cb(); });
           },

           /* now */
           []{ return Clock::now(); }
         },
         /* ReconnectSignals */
         ReconnectSignals{}
    )
  {
    // Feed WsClient events into the controller:
    ws->on_connected([this]{ rc.on_transport_open(); });
    ws->on_close([this]{ rc.on_transport_close(CloseReason::TransportError); });
  }
};

// after rc reports on_connected (via ReconnectSignals or by observing rc.state())
void start_ocpp_bringup(Session& session, ReconnectGlue& glue) {
  // 1) Send BootNotification
  session.send_call(BootNotification{/* fill your payload */},
    // on_reply:
    [&session, &glue](const OcppFrame& f) {
      if (std::holds_alternative<CallResult>(f)) {
        // 2) Mark controller online AFTER resume_delay
        glue.rc.on_boot_accepted();
        // 3) Start heartbeats (example: 30s)
        session.start_heartbeat(30);
      } else {
        // Boot rejected? You may schedule retry or treat as transient.
      }
    }
  );
}

boost::asio::io_context io;

// Build transport and session
auto ws = std::make_shared<WsClient>(io, "localhost", "8080");
auto session = std::make_unique<Session>(io, ws);  // Session uses the same Transport impl

// Build reconnect policy & glue
ReconnectPolicy pol;
pol.initial_backoff = Ms{500};
pol.max_backoff     = Ms{30'000};
pol.jitter_ratio    = 0.20f;
pol.resume_delay    = Ms{250};

ReconnectGlue glue{io, ws, pol};

// Optional: observe controller signals (for logs/metrics)
glue.rc = ReconnectController(
  pol,
  glue.rc/* existing ops */,
  ReconnectSignals{
    .on_connecting = []{ std::cout << "Connecting...\n"; },
    .on_connected  = [&]{ std::cout << "Connected (transport up)\n"; start_ocpp_bringup(*session, glue); },
    .on_online     = []{ std::cout << "Online (ok to send)\n"; },
    .on_offline    = []{ std::cout << "Offline (gate sends)\n"; },
    .on_closed     = [](CloseReason){ std::cout << "Closed\n"; },
    .on_backoff_scheduled = [](Ms d){ std::cout << "Reconnect in " << d.count() << " ms\n"; },
  }
);

// Kick off the controller (URL not used here; ws already has host/port)
glue.rc.start("ws://localhost:8080");

io.run();


// ClientLoop
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace boost::asio { class io_context; }

struct WsClient;
class Session;

class ClientLoop {
public:
    enum class State : std::uint8_t { Offline, Connecting, Online };

    struct Config {
        std::string host = "127.0.0.1";
        unsigned short port = 0;
        std::string url; // optional; if empty, a ws://host:port style URL can be built by caller
    };

    struct Factories {
        // Must create a NEW transport per attempt (prevents handler duplication across reconnects).
        std::function<std::shared_ptr<WsClient>(boost::asio::io_context&, const Config&)> make_transport;

        // Optional for Exercise 1: can return nullptr; later exercises will create a real Session here.
        std::function<std::unique_ptr<Session>(boost::asio::io_context&, WsClient&)> make_session;
    };

    ClientLoop(boost::asio::io_context& ioc, Config cfg, Factories f);
    ~ClientLoop();

    ClientLoop(const ClientLoop&) = delete;
    ClientLoop& operator=(const ClientLoop&) = delete;

    void start();
    void stop();

    // Probes for tests
    State state() const;
    std::uint64_t connect_attempts() const;
    std::uint64_t online_transitions() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

#include "client_loop.hpp"

#include "reconnect.hpp"
#include "ws_client.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

struct ClientLoop::Impl : std::enable_shared_from_this<ClientLoop::Impl> {
    boost::asio::io_context& ioc;
    Config cfg;
    Factories f;

    // Owned lifecycle pieces
    std::shared_ptr<ReconnectSignals> sigs = std::make_shared<ReconnectSignals>();
    std::shared_ptr<ReconnectController> rc;          // shared_ptr: controller uses weak_from_this()
    std::shared_ptr<WsClient> transport;              // recreated per attempt
    std::unique_ptr<Session> session;                 // recreated per successful connect (Exercise 2+)

    // Probe state
    std::atomic<State> st{State::Offline};
    std::atomic<std::uint64_t> attempts{0};
    std::atomic<std::uint64_t> online_edges{0};

    // Timer plumbing for TransportOps::post_after/cancel_after
    std::atomic<std::uint64_t> next_token{1};
    std::mutex timers_mtx;
    std::unordered_map<std::uint64_t, std::shared_ptr<boost::asio::steady_timer>> timers;

    // Close completion hook for TransportOps::async_close
    std::mutex close_mtx;
    std::function<void()> pending_close_done;

    explicit Impl(boost::asio::io_context& io, Config c, Factories factories)
        : ioc(io), cfg(std::move(c)), f(std::move(factories)) {}

    void set_state(State s) { st.store(s, std::memory_order_relaxed); }

    void reset_connection_objects() {
        session.reset();
        transport.reset();
    }

    TransportOps make_transport_ops() {
        std::weak_ptr<Impl> wk = weak_from_this();

        TransportOps ops;

        ops.async_connect = [wk](const std::string& /*url*/, std::function<void(bool ok)> done) {
            auto self = wk.lock();
            if (!self) { done(false); return; }

            self->reset_connection_objects();
            self->transport = self->f.make_transport(self->ioc, self->cfg);

            if (!self->transport) { done(false); return; }

            // Ensure completion is delivered exactly once.
            struct Attempt {
                std::atomic<bool> completed{false};
                std::atomic<bool> connected{false};
                std::function<void(bool)> done;
            };
            auto a = std::make_shared<Attempt>();
            a->done = std::move(done);

            // Single owner of callbacks: ClientLoop installs them on the fresh WsClient instance.
            self->transport->on_connected([wk, a]() {
                if (a->completed.exchange(true)) return;
                a->connected.store(true);
                if (auto self = wk.lock()) {
                    // Create per-connection session (can be nullptr for Exercise 1)
                    if (self->f.make_session) {
                        self->session = self->f.make_session(self->ioc, *self->transport);
                    }
                }
                a->done(true);
            });

            self->transport->on_close([wk, a]() {
                // If connect hasn't completed yet, treat as connect failure.
                if (!a->completed.exchange(true)) {
                    a->done(false);
                    return;
                }

                // If we were connected before, this is a real close event: tell controller.
                if (a->connected.load()) {
                    if (auto self = wk.lock()) {
                        if (self->rc) self->rc->on_transport_close(CloseReason::TransportError);

                        // Also satisfy async_close completion if one is pending.
                        std::function<void()> close_done;
                        {
                            std::lock_guard<std::mutex> lg(self->close_mtx);
                            close_done = std::move(self->pending_close_done);
                        }
                        if (close_done) close_done();
                    }
                }
            });

            // NOTE: Exercise 1 keeps on_message wiring thin; later exercises should forward to Session.
            // self->transport->on_message([wk](std::string_view msg){ ... });

            self->transport->start();
        };

        ops.async_close = [wk](std::function<void()> done) {
            auto self = wk.lock();
            if (!self) return;

            {
                std::lock_guard<std::mutex> lg(self->close_mtx);
                self->pending_close_done = std::move(done);
            }

            if (self->transport) self->transport->close();
            else {
                // Nothing to close; complete immediately.
                std::function<void()> cb;
                {
                    std::lock_guard<std::mutex> lg(self->close_mtx);
                    cb = std::move(self->pending_close_done);
                }
                if (cb) cb();
            }
        };

        ops.post_after = [wk](std::chrono::milliseconds delay, std::function<void()> cb) -> std::uint64_t {
            auto self = wk.lock();
            if (!self) return 0;

            const std::uint64_t id = self->next_token.fetch_add(1, std::memory_order_relaxed);
            auto t = std::make_shared<boost::asio::steady_timer>(self->ioc, delay);

            {
                std::lock_guard<std::mutex> lg(self->timers_mtx);
                self->timers.emplace(id, t);
            }

            t->async_wait([wk, id, cb = std::move(cb)](const boost::system::error_code& ec) mutable {
                auto self = wk.lock();
                if (!self) return;

                {
                    std::lock_guard<std::mutex> lg(self->timers_mtx);
                    self->timers.erase(id);
                }

                if (ec == boost::system::errc::success) cb();
            });

            return id;
        };

        ops.cancel_after = [wk](std::uint64_t id) -> bool {
            auto self = wk.lock();
            if (!self) return false;

            std::shared_ptr<boost::asio::steady_timer> t;
            {
                std::lock_guard<std::mutex> lg(self->timers_mtx);
                auto it = self->timers.find(id);
                if (it == self->timers.end()) return false;
                t = it->second;
                self->timers.erase(it);
            }
            t->cancel();
            return true;
        };

        ops.now = [] { return std::chrono::steady_clock::now(); };

        return ops;
    }

    void wire_signals() {
        std::weak_ptr<Impl> wk = weak_from_this();

        sigs->on_connecting = [wk] {
            if (auto self = wk.lock()) {
                self->attempts.fetch_add(1, std::memory_order_relaxed);
                self->set_state(State::Connecting);
            }
        };

        // Transport connected but not yet "Online" (boot accepted gating lives in controller)
        sigs->on_connected = [wk] {
            if (auto self = wk.lock()) self->set_state(State::Connecting);
        };

        sigs->on_offline = [wk] {
            if (auto self = wk.lock()) {
                self->reset_connection_objects();
                self->set_state(State::Offline);
            }
        };

        sigs->on_online = [wk] {
            if (auto self = wk.lock()) {
                self->online_edges.fetch_add(1, std::memory_order_relaxed);
                self->set_state(State::Online);
            }
        };

        // Optional: on_closed/on_backoff_scheduled can be used by later tests.
    }

    void start_controller() {
        wire_signals();

        ReconnectPolicy pol;
        auto ops = make_transport_ops();
        rc = std::make_shared<ReconnectController>(pol, std::move(ops), sigs);

        // Start in Offline; controller will move us to Connecting via on_connecting.
        set_state(State::Offline);

        const std::string url = !cfg.url.empty() ? cfg.url : "ws://" + cfg.host + ":" + std::to_string(cfg.port);
        rc->start(url);
    }

    void stop_controller() {
        // Cancel timers first to avoid callbacks after destruction.
        {
            std::lock_guard<std::mutex> lg(timers_mtx);
            for (auto& [_, t] : timers) t->cancel();
            timers.clear();
        }

        if (rc) rc->stop();
        reset_connection_objects();
        set_state(State::Offline);
    }
};

ClientLoop::ClientLoop(boost::asio::io_context& ioc, Config cfg, Factories f)
    : impl_(std::make_shared<Impl>(ioc, std::move(cfg), std::move(f))) {}

ClientLoop::~ClientLoop() {
    if (impl_) impl_->stop_controller();
}

void ClientLoop::start() { impl_->start_controller(); }
void ClientLoop::stop() { impl_->stop_controller(); }

ClientLoop::State ClientLoop::state() const { return impl_->st.load(std::memory_order_relaxed); }
std::uint64_t ClientLoop::connect_attempts() const { return impl_->attempts.load(std::memory_order_relaxed); }
std::uint64_t ClientLoop::online_transitions() const { return impl_->online_edges.load(std::memory_order_relaxed); }

// ...existing code...
void on_transport_close(CloseReason why){
    std::cout<<"on_transport_close() being called\n";

    metrics_.last_disconnect_reason = why;

    online_ = false; // <-- ADD: we are not logically online after any disconnect

    if( rSigs->on_closed )
    {
        rSigs->on_closed(why);
    }

    if( rSigs->on_offline )
    {
        rSigs->on_offline();
    }

    if( cS_ == ConnState::Closing )
    {
        cS_ = ConnState::Disconnected;
        return;
    }

    cS_ = ConnState::Disconnected;

    schedule_reconnect_();
};
// ...existing code...
void try_reconnect_(){
    if( cS_ != ConnState::Disconnected )
        return;

    online_ = false; // <-- ADD: each attempt starts "not online" until boot accepted

    std::cout<<"Reconnect starting..."<<"\n";
    // ...existing code...
}
// ...existing code...

// ...existing code...
void start() override {
  auto self = shared_from_this();
  res_.async_resolve(host_, port_, [this,self](auto ec, auto results){
    if (ec) {
      std::cerr << "WebSocket resolve error: " << ec.message() << "\n";
      state_ = WsClientState::Disconnected;
      if (on_closed_) on_closed_(); // <-- ADD
      return;
    }

    std::cout << "Resolved.\n";

    boost::asio::async_connect(ws_.next_layer(), results, [this,self](auto ec, auto){
      if (ec) {
        std::cerr << "WebSocket connect error: " << ec.message() << "\n";
        state_ = WsClientState::Disconnected;
        if (on_closed_) on_closed_(); // <-- ADD
        return;
      }

      std::cout<<"Connected.\n";
      state_  = WsClientState::Connecting;

      ws_.async_handshake(host_, "/", [this,self](auto ec){
        if (ec) {
          std::cerr << "WebSocket handshake error: " << ec.message() << "\n";
          state_ = WsClientState::Disconnected;
          if (on_closed_) on_closed_(); // <-- ADD
          return;
        }

        state_ = WsClientState::Connected;
        // ...existing code...
      });
    });
  });
}
// ...existing code...

#include <gtest/gtest.h>
#include <boost/asio/io_context.hpp>

#include "client_loop.hpp"
#include "ws_client.hpp"

TEST(ClientLoop, StartsOffline) {
    boost::asio::io_context ioc;

    ClientLoop::Config cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 0; // not used for this test

    ClientLoop::Factories f;
    f.make_transport = [](boost::asio::io_context& io, const ClientLoop::Config& c) {
        return std::make_shared<WsClient>(io, c.host, std::to_string(c.port));
    };
    f.make_session = nullptr; // Exercise 1: keep thin

    ClientLoop loop(ioc, cfg, std::move(f));

    EXPECT_EQ(loop.state(), ClientLoop::State::Offline);
    EXPECT_EQ(loop.connect_attempts(), 0u);
    EXPECT_EQ(loop.online_transitions(), 0u);
}