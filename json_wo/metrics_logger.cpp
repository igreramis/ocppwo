#include "metrics_logger.hpp"

#include <ostream>
#include <algorithm>

#include "metrics.hpp"

void MetricsLogger::log_metrics(const MetricsSnapshot& s, std::ostream& os) {
    // Stable, grep-friendly format: one key=value per line.
    struct KV{ std::string_view k; uint64_t v; };
    std::array<KV, 18> items {{
        {"protocol.calls_sent", s.calls_sent},
        {"protocol.callresults_received", s.callresults_received},
        {"protocol.callerrors_received", s.callerrors_received},

        {"reconnect.connect_attempts_total", s.connect_attempts_total},
        {"reconnect.successful_connects_total", s.successful_connects_total},
        {"reconnect.reconnect_attempts_total", s.reconnect_attempts_total},
        {"reconnect.online_transitions_total", s.online_transitions_total},
        {"reconnect.last_backoff_ms", s.last_backoff_ms},
        {"reconnect.time_to_online_last_ms", s.time_to_online_last_ms},

        {"session.pending_max", s.pending_max},
        {"session.timeouts_total", s.timeouts_total},
        {"session.connection_closed_failures_total", s.connection_closed_failures_total},

        {"transport.current_write_queue_depth", s.current_write_queue_depth},
        {"transport.max_depth_observed", s.max_depth_observed},
        {"transport.writes_in_flight_max_observed", s.writes_in_flight_max_observed},
        {"transport.writes_enqueued_total", s.writes_enqueued_total},
        {"transport.writes_completed_total", s.writes_completed_total},
        {"transport.close_events_total", s.close_events_total},
    }};

    std::sort(items.begin(), items.end(), [](const KV&a, const KV&b){ return a.k < b.k; });
    for( const auto &item : items ) {
        os << item.k << "=" << item.v << "\n";
    }
    // os << "transport.current_write_queue_depth=" << s.current_write_queue_depth << "\n";
    // os << "transport.max_depth_observed=" << s.max_depth_observed << "\n";
    // os << "transport.writes_in_flight_max_observed=" << s.writes_in_flight_max_observed << "\n";
    // os << "transport.writes_enqueued_total=" << s.writes_enqueued_total << "\n";
    // os << "transport.writes_completed_total=" << s.writes_completed_total << "\n";
    // os << "transport.close_events_total=" << s.close_events_total << "\n";

    // os << "session.pending_max=" << s.pending_max << "\n";
    // os << "session.timeouts_total=" << s.timeouts_total << "\n";
    // os << "session.connection_closed_failures_total=" << s.connection_closed_failures_total << "\n";

    // os << "protocol.calls_sent=" << s.calls_sent << "\n";
    // os << "protocol.callresults_received=" << s.callresults_received << "\n";
    // os << "protocol.callerrors_received=" << s.callerrors_received << "\n";

    // os << "reconnect.connect_attempts_total=" << s.connect_attempts_total << "\n";
    // os << "reconnect.successful_connects_total=" << s.successful_connects_total << "\n";
    // os << "reconnect.reconnect_attempts_total=" << s.reconnect_attempts_total << "\n";
    // os << "reconnect.online_transitions_total=" << s.online_transitions_total << "\n";
    // os << "reconnect.last_backoff_ms=" << s.last_backoff_ms << "\n";
    // os << "reconnect.time_to_online_last_ms=" << s.time_to_online_last_ms << "\n";
}