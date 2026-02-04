#include "metrics.hpp"

MetricsSnapshot Metrics::snapshot() const {
    return MetricsSnapshot{
        .current_write_queue_depth = current_write_queue_depth_.load(std::memory_order_relaxed),
        .max_depth_observed = max_depth_observed_.load(std::memory_order_relaxed),
        .writes_in_flight_max_observed = writes_in_flight_max_observed_.load(std::memory_order_relaxed),
        .writes_enqueued_total = writes_enqueued_total_.load(std::memory_order_relaxed),
        .writes_completed_total = writes_completed_total_.load(std::memory_order_relaxed),
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

void Metrics::update_max(std::atomic<uint64_t>& target, uint64_t value) {
    auto cur = target.load(std::memory_order_relaxed);
    while( cur < value && !target.compare_exchange_weak(cur, value, std::memory_order_relaxed, std::memory_order_relaxed) ) {
        //loop until successful
    };
    // compare_exchange_weak atomically updates target to value only if it has not changed since we last read it;
    // on failure (another thread updated it, or a spurious retry), cur is refreshed with the latest value and we try again.
    // note: Spurious (in compare_exchange_weak) means the operation may fail even though no other thread changed the
    // value — the CPU can simply ask us to retry, so we must use it in a loop.

}

void Metrics::transport_on_write_enqueued(uint64_t depth_after_enqueue) {
    writes_enqueued_total_.fetch_add(1, std::memory_order_relaxed);
    current_write_queue_depth_.store(depth_after_enqueue, std::memory_order_relaxed);
    update_max(max_depth_observed_, depth_after_enqueue);
}

void Metrics::transport_on_write_completed(uint64_t depth_after_completion) {
    writes_completed_total_.fetch_add(1, std::memory_order_relaxed);
    current_write_queue_depth_.store(depth_after_completion, std::memory_order_relaxed);
}

void Metrics::transport_observe_writes_in_flight(uint64_t writes_in_flight) {
    update_max(writes_in_flight_max_observed_, writes_in_flight);
}

void Metrics::transport_on_close_event() {
    close_events_total_.fetch_add(1, std::memory_order_relaxed);
}