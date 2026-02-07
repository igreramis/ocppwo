#pragma once
#include <cstdint>
#include <atomic>

struct MetricsSnapshot {
    //transport
    // Gauge: number of outbound messages currently outstanding (enqueued but not yet fully
    // completed). This typically includes the message currently being written (if any).
    // 1:1 with WsClient's probe gauge: current_write_queue_depth_.
    uint64_t current_write_queue_depth = 0;

    // Gauge (max-so-far): the highest observed value of current_write_queue_depth since start.
    // Useful as a backpressure/queueing peak indicator.
    // Conceptually 1:1 with WsClient's probe max: max_write_queue_depth_.
    uint64_t max_depth_observed = 0;

    // Gauge (max-so-far): maximum number of concurrent writes observed "in flight" at once
    // (async write started, completion handler not yet run). With correct serialization,
    // this should usually be <= 1.
    // 1:1 with WsClient's probe max: max_writes_in_flight_.
    uint64_t writes_in_flight_max_observed = 0;

    // Counter: total number of outbound writes/messages enqueued for sending. Monotonic.
    uint64_t writes_enqueued_total = 0;

    // Counter: total number of outbound writes/messages whose async write completion ran.
    // Monotonic.
    uint64_t writes_completed_total = 0;

    // Counter: number of transport close-type events observed (explicit close or I/O failure).
    uint64_t close_events_total = 0;

    //session
    uint64_t pending_max = 0;
    uint64_t timeouts_total = 0;
    uint64_t connection_closed_failures_total = 0;

    //protocol
    uint64_t calls_sent = 0;
    uint64_t callresults_received = 0;
    uint64_t callerrors_received = 0;

    //reconnect
    // Counter: every connect attempt started by the reconnect loop.
    uint64_t connect_attempts_total = 0;

    // Counter: connect attempts that reached a successful connection.
    uint64_t successful_connects_total = 0;

    // Counter: reconnect attempts caused by close/failure.
    uint64_t reconnect_attempts_total = 0;

    // Counter: number of transitions into the "online" state.
    uint64_t online_transitions_total = 0;

    // Gauge: last backoff delay scheduled by the reconnect loop (milliseconds).
    uint64_t last_backoff_ms = 0;

    // Gauge: last observed time from connect attempt start -> online transition (milliseconds).
    uint64_t time_to_online_last_ms = 0;

    //server
    // Counter: total websocket/transport frames received by the server.
    uint64_t server_frames_received_total = 0;

    // Counter: total OCPP CALL messages received by the server.
    uint64_t server_calls_received_total = 0;

    // Counter: total replies sent by the server (CALLRESULT + CALLERROR).
    uint64_t server_replies_sent_total = 0;

    // Counter: total times the server force-closed a connection (test hook).
    uint64_t server_force_closes_total = 0;
};

struct Metrics {
    MetricsSnapshot snapshot() const;
    void update_max(std::atomic<uint64_t>& target, uint64_t value);
    void transport_on_write_enqueued(uint64_t depth_after_enqueue);
    void transport_on_write_completed(uint64_t depth_after_completion);
    void transport_observe_writes_in_flight(uint64_t writes_in_flight);
    void transport_on_close_event();

    void timeouts_total_increment();
    void connection_closed_failures_total_increment();
    void connection_closed_failures_total_add(uint64_t n);

    void reconnect_connect_attempts_total_increment();
    void reconnect_successful_connects_total_increment();
    void reconnect_reconnect_attempts_total_increment();
    void reconnect_online_transitions_total_increment();
    void reconnect_set_last_backoff_ms(uint64_t ms);
    void reconnect_set_time_to_online_last_ms(uint64_t ms);

    void server_frames_received_total_increment();
    void server_calls_received_total_increment();
    void server_replies_sent_total_increment();
    void server_force_closes_total_increment();


    private:
    std::atomic<uint64_t> current_write_queue_depth_{0};
    std::atomic<uint64_t> max_depth_observed_{0};
    std::atomic<uint64_t> writes_in_flight_max_observed_{0};
    std::atomic<uint64_t> writes_enqueued_total_{0};
    std::atomic<uint64_t> writes_completed_total_{0};
    std::atomic<uint64_t> close_events_total_{0};

    std::atomic<uint64_t> pending_max_{0};
    std::atomic<uint64_t> timeouts_total_{0};
    std::atomic<uint64_t> connection_closed_failures_total_{0};
    std::atomic<uint64_t> calls_sent_{0};
    std::atomic<uint64_t> callresults_received_{0};
    std::atomic<uint64_t> callerrors_received_{0};
    //reconnect
    std::atomic<uint64_t> connect_attempts_total_{0};
    std::atomic<uint64_t> successful_connects_total_{0};
    std::atomic<uint64_t> reconnect_attempts_total_{0};
    std::atomic<uint64_t> online_transitions_total_{0};
    std::atomic<uint64_t> last_backoff_ms_{0};
    std::atomic<uint64_t> time_to_online_last_ms_{0};

    //server
    std::atomic<uint64_t> server_frames_received_total_{0};
    std::atomic<uint64_t> server_calls_received_total_{0};
    std::atomic<uint64_t> server_replies_sent_total_{0};
    std::atomic<uint64_t> server_force_closes_total_{0};
};