# Milestone 6 — Integration & Metrics (Exercise Set)

## Exercise 1 — Define a minimal metrics surface: `MetricsSnapshot`

**Goal:** One struct that represents “what we care about right now”, easy to read from tests.

**Touch:**

* `include/ocppwo/metrics.hpp` (new)
* `src/metrics.cpp` (optional)

**Implement:**
Create:

* `struct MetricsSnapshot { ... };`
* `class Metrics { public: MetricsSnapshot snapshot() const; };`

Include at least:

* **transport**: current write queue depth, max depth observed, writes in flight max observed
* **session**: pending_count max observed, timeouts count, connection_closed count
* **protocol**: calls_sent, callresults_received, callerrors_received
* **reconnect**: connect_attempts, reconnect_attempts, online_transitions

**Test first (FAIL):**

* `TEST(Metrics, StartsAtZero)`

**Acceptance:** snapshot starts all counters at 0 and gauges sane.

**Hints:** Counters are usually `uint64_t`; gauges can be `size_t`.

---

## Exercise 2 — Add a `Metrics` object to the client composition (single owner)

**Goal:** Plumb one `Metrics&` into `WsClient`, `Session`, and your Milestone 5 coordinator (`ClientLoop` or similar).

**Touch:**

* `WsClient` ctor signature
* `Session` ctor signature
* `ClientUnderTest` / `ClientLoop` wiring

**Test first (FAIL):**

* `TEST(E2E, CanReadMetricsFromClientUnderTest)`

**Acceptance:** integration tests can call `client.metrics().snapshot()`.

**Hints:** Prefer dependency injection over globals.

---

## Exercise 3 — Instrument `WsClient`: write-queue + write lifecycle counters

**Goal:** Make the existing transport probes “official metrics”.

You already have write queue depth probes in `ClientUnderTest` per context. 
Now codify them:

**Touch:** `ws_client.hpp/.cpp`, `metrics.hpp`

**Implement counters/gauges:**

* `transport.writes_enqueued_total`
* `transport.writes_completed_total`
* `transport.write_queue_depth_current` (gauge)
* `transport.write_queue_depth_max`
* `transport.writes_in_flight_max`
* `transport.close_events_total`

**Test first (FAIL):**

* `TEST(E2E, BackPressureUpdatesTransportMetrics)`

  * reuse your “ThousandSmallFrames” stress
  * assert `write_queue_depth_max > 0`
  * assert `writes_enqueued_total == writes_completed_total` at end (or ≥, depending on timing)

**Acceptance:** metrics match observable behavior.

**Hints:** update gauges at enqueue/dequeue; counters monotonic (atomics).

---

## Exercise 4 — Instrument `Session`: correlation and failure tallies

**Goal:** Count what matters for reply tracking reliability.

**Touch:** `session.hpp/.cpp`

**Add counters:**

* `session.calls_sent_total`
* `session.pending_max`
* `session.timeouts_total`
* `session.connection_closed_failures_total`
* `session.unmatched_replies_total`
* `session.protocol_errors_total` (decode/type mismatch)

**Test first (FAIL):**

* `TEST(E2E, TimeoutIncrementsTimeoutMetric)`

  * configure server to ignore a call → timeout occurs
  * assert `timeouts_total == 1`
* `TEST(E2E, ServerCloseIncrementsConnectionClosedMetric)`

  * force close with pending → assert `connection_closed_failures_total == N`

**Acceptance:** counts are correct and stable.

**Hints:** Increment *once* at the resolution point (exactly-once).

---

## Exercise 5 — Instrument reconnect loop: attempts, outcomes, and durations

**Goal:** Make Milestone 5 behavior measurable.

**Touch:** `ReconnectController` or `ClientLoop`

**Add counters:**

* `reconnect.connect_attempts_total` (every connect attempt)
* `reconnect.reconnect_attempts_total` (attempts caused by close/failure)
* `reconnect.successful_connects_total`
* `reconnect.online_transitions_total`
* `reconnect.last_backoff_ms` (gauge)
* `reconnect.time_to_online_last_ms` (duration gauge)

**Test first (FAIL):**

* `TEST(E2E, ForcedCloseIncrementsReconnectAttempt)`

  * connect → force close → reconnect → online
  * assert reconnect_attempts_total increments
  * assert online_transitions_total == 2 (first online + second online)

**Acceptance:** you can “see” reconnect loops.

**Hints:** For “time_to_online”, start timer at attempt start, stop at “online”.

---

## Exercise 6 — Add a simple metrics sink: periodic logging (optional but valuable)

**Goal:** Provide a low-effort “export”: print snapshot on demand or periodically.

**Touch:**

* `metrics_logger.hpp/.cpp` (new) OR integrate into `ClientLoop`

**Implement:**

* `void log_metrics(const MetricsSnapshot&, std::ostream&)`
* Optional timer: every X seconds while running, dump snapshot (in real run mode, not tests).

**Test first (FAIL):**

* `TEST(Metrics, LogFormatContainsKeyFields)`

  * call `log_metrics(snapshot, oss)`
  * assert output contains expected keys

**Acceptance:** stable log format for debugging.

**Hints:** Keep it deterministic (sorted keys) for tests.

---

## Exercise 7 — Server-side metrics (TestServer / WsServerSession)

**Goal:** Instrument server too—useful to debug client behavior.

**Touch:**

* `ws_server_session.hpp/.cpp`
* `test_server.cpp`

**Add counters:**

* `server.frames_received_total`
* `server.calls_received_total`
* `server.replies_sent_total`
* `server.force_closes_total`

**Test first (FAIL):**

* `TEST(E2E, ServerMetricsMatchTraffic)`

  * run boot + 3 heartbeats
  * assert calls_received_total >= 4
  * replies_sent_total >= 4

**Acceptance:** the test server is also observable.

---

## Exercise 8 — “No double handler” regression via metrics

**Goal:** Use metrics to catch the classic reconnect bug: duplicate message handler wiring.

**Touch:** `ClientLoop` callback wiring

**Implement metric:**

* `session.frames_processed_total` (increment each inbound frame processed)

**Test first (FAIL):**

* `TEST(E2E, AfterReconnectOneInboundFrameIncrementsProcessedOnce)`

  * connect → force close → reconnect
  * inject one inbound frame
  * assert `frames_processed_total` increments by exactly 1

**Acceptance:** prevents callback fan-out leaks.

---

## Exercise 9 — End-to-end “golden run” asserts metrics invariants

**Goal:** Create one high-value E2E test that asserts *multiple* metrics together after a lifecycle.

**Write test first (FAIL):**

* `TEST(E2E, FullLifecycleProducesSaneMetrics)`

  * connect → boot accepted → N heartbeats → force close → reconnect → M heartbeats
  * Assert invariants:

    * `online_transitions_total == 2`
    * `timeouts_total == 0`
    * `connection_closed_failures_total == 0` (if you close cleanly) **or** equals pending at close (if you force-close mid-flight)
    * `calls_sent_total == (boot + heartbeats...)`
    * transport `write_queue_depth_max` within expected range (non-zero under stress)

**Acceptance:** a single test gives strong confidence + excellent debugging signal when it fails.

---

# Definition of Done for Milestone 6

You’re done when:

* The client exposes a **single `MetricsSnapshot`** interface.
* `WsClient`, `Session`, and reconnect loop all update metrics correctly.
* Tests assert metrics for:

  * timeout
  * server close cleanup
  * reconnect lifecycle
  * backpressure stress
  * no-duplicate-handler regression
* Optional: you can log/print metrics for human debugging.

---

## Suggested build order (fastest path)

1. Exercises 1–3 (MetricsSnapshot + plumb + WsClient gauges)
2. Exercise 4 (Session tallies)
3. Exercise 5 (Reconnect metrics)
4. Exercise 9 (Golden invariants test)
   Then fill in 6–8 as hardening.

If you want, paste your current **ClientLoop/ClientUnderTest** glue file (where you wire callbacks) and I’ll map each metric increment to the exact hook points you already have—still as exercises, not a code dump.
