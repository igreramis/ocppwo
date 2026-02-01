#pragma once
#include <cstdint>
#include <atomic>

struct MetricsSnapshot {
    //transport
    uint64_t current_write_queue_depth = 0;
    uint64_t max_depth_observed = 0;
    uint64_t writes_in_flight_max_observed = 0;

    //session
    uint64_t pending_count_max_observed = 0;
    uint64_t timeouts_count = 0;
    uint64_t connection_closed_count = 0;

    //protocol
    uint64_t calls_sent = 0;
    uint64_t callresults_received = 0;
    uint64_t callerrors_received = 0;

    //reconnect
    uint64_t connect_attempts = 0;
    uint64_t reconnect_attempts = 0;
    uint64_t online_transitions = 0;
};

struct Metrics {
    MetricsSnapshot snapshot() const;
    private:
    std::atomic<uint64_t> current_write_queue_depth_{0};
    std::atomic<uint64_t> max_depth_observed_{0};
    std::atomic<uint64_t> writes_in_flight_max_observed_{0};
    std::atomic<uint64_t> pending_count_max_observed_{0};
    std::atomic<uint64_t> timeouts_count_{0};
    std::atomic<uint64_t> connection_closed_count_{0};
    std::atomic<uint64_t> calls_sent_{0};
    std::atomic<uint64_t> callresults_received_{0};
    std::atomic<uint64_t> callerrors_received_{0};
    std::atomic<uint64_t> connect_attempts_{0};
    std::atomic<uint64_t> reconnect_attempts_{0};
    std::atomic<uint64_t> online_transitions_{0};
};