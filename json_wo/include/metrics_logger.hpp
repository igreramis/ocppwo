#pragma once

#include <iosfwd>

/**Note on design
 *  Keep this class as a simple formatting/output utility
 *  If we want things like periodic logging, make the root composite actuate that. Root is in a position
 *  to have access to timers and other scheduling mechansim that would otherwise needed to be plumbed down
 *  through layers who have otherwise no use of such mechanisms and have ended up acting like carrying
 *  vessels just so that MetricsLogger can have a timer of its own and act as an independent module.
 */
struct MetricsSnapshot;
class MetricsLogger {
    public:
        void log_metrics(const MetricsSnapshot& snapshot, std::ostream& oss);
};