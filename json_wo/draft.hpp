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
