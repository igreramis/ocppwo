#include "metrics.hpp"

MetricsSnapshot Metrics::snapshot() const {
    return MetricsSnapshot{
        .current_write_queue_depth = current_write_queue_depth_.load(std::memory_order_relaxed),
        .max_depth_observed = max_depth_observed_.load(std::memory_order_relaxed),
        .writes_in_flight_max_observed = writes_in_flight_max_observed_.load(std::memory_order_relaxed),
        .pending_count_max_observed = pending_count_max_observed_.load(std::memory_order_relaxed),
        .timeouts_count = timeouts_count_.load(std::memory_order_relaxed),
        .connection_closed_count = connection_closed_count_.load(std::memory_order_relaxed),
        .calls_sent = calls_sent_.load(std::memory_order_relaxed),
        .callresults_received = callresults_received_.load(std::memory_order_relaxed),
        .callerrors_received = callerrors_received_.load(std::memory_order_relaxed),
        .connect_attempts = connect_attempts_.load(std::memory_order_relaxed),
        .reconnect_attempts = reconnect_attempts_.load(std::memory_order_relaxed),
        .online_transitions = online_transitions_.load(std::memory_order_relaxed)
    };
}