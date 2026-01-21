# Milestone 5 — End-to-End Behavior & Reconnect Loop (Exercises)

## Exercise 1 — Introduce a “ClientLoop” / “ReconnectLoop” coordinator

**Goal:** One object owns the lifecycle: connect, create session wiring, react to close, schedule reconnect, re-init handshake.

**Touch:**

* `client_loop.hpp/.cpp` (new)
* `client_under_test.hpp/.cpp` (or wherever you currently glue `WsClient + Session`)

**Implement (design constraints):**

* Single owner of transport callbacks (`on_open`, `on_close`, `on_message`)
* Holds:

  * `ReconnectController`
  * current `WsClient` (or transport)
  * current `Session` (or pointer/resettable)
* Exposes minimal probes for tests:

  * `state()` (e.g., Offline/Connecting/Online)
  * `connect_attempts()`
  * `online_transitions()`

**Write test first (FAIL):**

* `TEST(ClientLoop, StartsOffline)`

**Acceptance:** compiles + basic state visibility.

**Hints:** keep it “thin”; no OCPP logic beyond sequencing.

---

## Exercise 2 — Ensure “Boot on connect” is re-run after every reconnect

**Goal:** On each successful connect, send BootNotification again (re-authenticate).

**Touch:**

* `ClientLoop::on_connected()`
* `ClientUnderTest` logic (if it currently does the boot)

**Write test first (FAIL):**

* `TEST(E2E, ReconnectTriggersNewBootNotification)`

**Test behavior:**

1. Connect → observe Boot sent
2. Force close
3. Reconnect → observe Boot sent again

**Acceptance:**

* BootNotification is sent once per connection establishment, not just once per process.

**Hints:** Don’t rely on “already booted” flag unless it is per-connection.

---

## Exercise 3 — Heartbeats only run while Online, and resume after reconnect

**Goal:** Heartbeats start only after Boot Accepted, stop on disconnect, resume after reconnect+Accepted.

**Touch:**

* `Session` heartbeat timer control (start/stop)
* `ClientLoop` state transitions: Online/Offline
* `ClientUnderTest` probes

**Write test first (FAIL):**

* `TEST(E2E, HeartbeatsStopOnCloseAndResumeAfterReconnect)`

**Test behavior:**

1. Connect, server replies Boot Accepted
2. Advance fake time → see N heartbeats
3. Force close
4. Advance time while offline → **no** heartbeats
5. Reconnect, Boot Accepted
6. Advance time → heartbeats resume

**Acceptance:**

* no heartbeats while disconnected
* heartbeats resume without manual pokes

**Hints:** put the “start heartbeats” gate behind “Boot accepted”.

---

## Exercise 4 — ConnectionClosed cleanup is proven end-to-end (no in-flight leaks)

**Goal:** When the server closes, all pending Calls resolve with ConnectionClosed, and the loop keeps running.

**Touch:**

* `Session::on_close()` path
* `ClientLoop::on_close()` triggers reconnect scheduling

**Write test first (FAIL):**

* `TEST(E2E, ServerCloseFailsPendingCallsAndReconnects)`

**Test behavior:**

1. Send a Call (e.g., Heartbeat) but delay server reply
2. Server forces close
3. Assert pending resolves with ConnectionClosed
4. Assert reconnect attempt happens afterward

**Acceptance:**

* Pending map empties
* No hung promises/callbacks
* Reconnect starts automatically

**Hints:** This validates Milestone 4 behavior under real close events.

---

## Exercise 5 — “Resume delay” is enforced before sending any queued/resumed traffic

**Goal:** After reconnect, wait `resume_delay` before resuming periodic tasks (like heartbeats).

**Touch:**

* `ReconnectController` integration point (where it signals “connected” vs “online”)
* `Session` heartbeat start timing

**Write test first (FAIL):**

* `TEST(E2E, ResumeDelayDelaysHeartbeatAfterReconnect)`

**Test behavior:**

1. Reconnect occurs at time T
2. Assert no heartbeats until T + resume_delay (even if Boot is accepted quickly)
3. After resume_delay, heartbeats begin

**Acceptance:** heartbeat scheduling respects policy.

**Hints:** use a deterministic timer pump; avoid sleeps.

---

## Exercise 6 — Backoff growth is visible at the E2E level

**Goal:** Prove reconnect backoff is applied across repeated failures.

**Touch:**

* `ReconnectController` ↔ `ClientLoop` scheduling

**Write test first (FAIL):**

* `TEST(E2E, ReconnectBackoffSchedulesIncreasingDelaysUntilCap)`

**Test behavior:**

* Force connect failures N times
* Capture scheduled reconnect delays
* Assert:

  * non-collapsing progression (allowing jitter)
  * never exceeds max_backoff

**Acceptance:** E2E demonstrates policy compliance, not just unit tests.

**Hints:** Don’t assert exact delays; assert bounds/properties.

---

## Exercise 7 — Prevent duplicate callback wiring across reconnects

**Goal:** Each reconnect should not accumulate extra `on_message` handlers (classic bug: double-processing frames).

**Touch:**

* `ClientLoop` transport wiring
* `WsClient` callback model

**Write test first (FAIL):**

* `TEST(E2E, MessageHandlerIsNotDuplicatedAcrossReconnects)`

**Test behavior:**

1. Connect
2. Force close
3. Reconnect
4. Inject one inbound frame
5. Assert it is processed exactly once (counter increments once)

**Acceptance:** No “fan-out leak”.

**Hints:** libocpp-style: one subscriber, internal propagation.

---

## Exercise 8 — “Boot Accepted gate” survives weird ordering

**Goal:** If heartbeats fire early (timer already scheduled), they must not send until boot is accepted.

**Touch:**

* `Session` heartbeat tick function
* gating logic

**Write test first (FAIL):**

* `TEST(E2E, HeartbeatDoesNotSendBeforeBootAccepted)`

**Test behavior:**

1. Connect but delay Boot response
2. Advance time beyond one heartbeat period
3. Assert 0 heartbeats sent
4. Now reply Boot Accepted
5. Assert heartbeats begin after acceptance (and resume_delay if applicable)

**Acceptance:** no protocol spam prior to being online.

---

## Exercise 9 — Forced-close loop: “connect → boot → heartbeat → close → reconnect → boot → heartbeat” (the milestone demo)

**Goal:** The canonical Milestone 5 scenario works end-to-end.

**Touch:** everything above; this is your “golden path + reconnect”.

**Write test first (FAIL):**

* `TEST(E2E, FullLifecycleReconnectAndResumeHeartbeats)`

**Acceptance criteria:**

* At least 1 heartbeat before close
* At least 1 heartbeat after reconnect
* Exactly one Boot per connection
* No pending leaks
* No double handler processing

---

# Definition of Done for Milestone 5

You’re done when you have tests proving:

* reconnect loop is automatic and stable
* boot re-runs on every reconnect
* heartbeats pause offline and resume online
* resume_delay is honored
* repeated failures back off
* close cleans up pending calls
* no callback duplication across reconnects 

---

## Quick next step (so you can start coding immediately)

Start with **Exercise 1 + Exercise 2**, because they force you to create the correct “single owner” structure. Once that’s in, the rest become straightforward incremental hardening.

If you want, paste your current `ClientUnderTest` (or the file that glues `WsClient + Session + ReconnectController`) and I’ll map each exercise to **your exact function names and existing test harness hooks**—still as exercises, not solutions.
