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

class TestClient{
public:
    TestClient(boost::asio::io_context& io, std::string host, std::string port);
    void start();
    void close();
    void trigger_boot();
    bool is_online() const;
    void enable_heartbeats(bool enable);
    bool get_backoff_time(int64_t& time_ms);
    // std::function<void(int time_ms)> rescheduled_with_backoff;
// private:
    boost::asio::io_context& io_;
    std::string host_;
    std::string port_;
    std::shared_ptr<WsClient> client_;
    std::shared_ptr<Session> ss_;
    std::weak_ptr<Session> wss_;
    std::shared_ptr<ReconnectGlue> rcg_;
    bool online_{false};
    int heartbeat_interval_s{0};
    int64_t backoff_in_ms{0};
    bool backoff_scheduled_{false};

    unsigned transport_max_writes_in_flight() const;
    unsigned transport_max_write_queue_depth() const;
    unsigned transport_current_write_queue_depth() const;
};