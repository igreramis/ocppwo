#pragma once
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <chrono>
#include "ws_client.hpp"
#include "session.hpp"
#include "reconnect_glue.hpp"

enum class TestClientState {
    Disconnected,
    Connecting,
    Connected,
    Booting,
    Ready
};

class TestClient{
public:
    TestClient(boost::asio::io_context& io, std::string host, std::string port);
    void start();
    void close();
    void trigger_boot();
    bool is_online() const;
    void enable_heartbeats(bool enable);
    bool get_backoff_time(int64_t& time_ms);
    void send_boot(std::function<void(const OcppFrame&)> cb);
    void send_authorize(std::function<void(const OcppFrame&)> cb);
    TestClientState state() const;
    uint64_t connect_attempts() const;
    uint64_t online_transitions() const;
    // std::function<void(int time_ms)> rescheduled_with_backoff;
// private:
    boost::asio::io_context& io_;
    std::string host_;
    std::string port_;
    std::shared_ptr<WsClient> client_;
    std::shared_ptr<Session> ss_; std::shared_ptr<SessionSignals> sS_;
    std::weak_ptr<Session> wss_;
    std::shared_ptr<ReconnectGlue> rcg_;
    bool online_{false};
    int heartbeat_interval_s{0};
    int64_t backoff_in_ms{0};
    bool backoff_scheduled_{false};

    unsigned transport_max_writes_in_flight() const;
    unsigned transport_max_write_queue_depth() const;
    unsigned transport_current_write_queue_depth() const;

    //why are the following atomics? 
    // they can be used in a multithreading scenario:
    //  -the async_callbacks where these variables are set could be running in different threads or they are
    //  all running in one thread and the test is running in a different thread which would mean that these variables are
    //  being read from a different thread.
    // if used in a multithreading scenario without atomic or lock protection, we could have
    //  -data race situation
    //    -reads might get half updated and half old values(torn reads/writes)
    //    -one thread might not see the updated value written by another thread(visibility issues)
    //commong pattern in ASIO tests
    //run io.poll()/io.run_for() in the same thread as assertions
    //only check probes onces you've run the above.
    //if tests read probes while the event loop might still be running elsewhere: keep atomic
    //if everything is strictly single-threaded and you only read after pumping IO: non-atomic is OK. 
    std::atomic<uint64_t> connect_attempts_{0};
    std::atomic<uint64_t> online_transitions_{0};
    std::atomic<TestClientState> state_{TestClientState::Disconnected};
};