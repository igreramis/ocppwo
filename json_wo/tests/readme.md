You’re right—my bad. The first milestone is **“## Quick smoke tests (do these first)”** from your archive. Below are **code-free, step-by-step exercises** you can run to prove each smoke test, aligned with your mini-stack and mirroring libocpp behaviors.&#x20;

---

# Exercises — Quick smoke tests (do these first)

## 1) Happy path — boot → reply → heartbeat

**Goal:** Client boots, server replies, client starts heartbeat.&#x20;

**Tasks**

* Spin up a local WS server and your client against it.
* Have client send `BootNotification`; have server reply with a valid `CallResult` (Accepted + heartbeat interval).
* Start the client heartbeat scheduler after the accepted reply.

**Prove it (acceptance checks)**

* Logs include a correlated pair: `[2,"<id>","BootNotification",…]` then `[3,"<same id>",…]`.
* Heartbeat messages start within 1 interval of the returned value and repeat (allow small jitter).
* No pending request remains after the boot reply; no timers leak (introspect your correlation map + timer count).

**Artifacts to produce**

* Test case name suggestion: `Smoke.HappyPath_Boot_Then_Heartbeat`.
* A minimal timeline diagram (connect → boot call → boot result → heartbeat N→ heartbeat N+1).

---

## 2) Unknown action — server returns CallError(NotImplemented)

**Goal:** Client sends a bogus action; server responds with `CallError` = `NotImplemented`.&#x20;

**Tasks**

* Make the client send a `Call` with action `"TotallyMadeUp"`.
* Server handler returns a `CallError` with code `NotImplemented` (OCPP error space).

**Prove it**

* The `CallError` correlates to the same `messageId`.
* Client resolves the pending entry to an error state and does **not** retry automatically.
* Connection remains open; heartbeat (if already running) is unaffected.

**Artifacts**

* Test name: `Smoke.UnknownAction_Yields_NotImplemented`.
* A metrics snippet: `call_errors{code="NotImplemented"} == 1`.

---

## 3) Timeout — server drops a response; client synthesizes local CallError(Timeout)

**Goal:** When a reply is never received, client emits a local timeout error.&#x20;

**Tasks**

* Configure the client’s per-request timer (keep it short for test).
* Send a `Call` from client; configure server to **ignore** that `messageId`.
* Let the request timer expire.

**Prove it**

* Pending entry is completed with a locally produced `CallError(Timeout)` (not a wire frame).
* Correlation map is cleared; no retry unless your design says so.
* Transport remains open; subsequent calls still work.

**Artifacts**

* Test name: `Smoke.Timeout_NoServerReply_LocalTimeoutError`.
* Log assertion that clearly distinguishes “local synthesized error” vs “received error”.

---

## 4) Back-pressure — \~1k small messages; serialized writes; no crash

**Goal:** Stress the write path to verify a single writer and a bounded queue.&#x20;

**Tasks**

* Enqueue \~1000 small frames (e.g., heartbeats or tiny pings) as fast as possible.
* Ensure your Transport uses a **single in-flight write** with a queue (no concurrent writes).

**Prove it**

* No exceptions or assertions; process completes and the connection stays healthy.
* A counter (or probe) shows max one async write active at any time.
* Peak queue length is observed and eventually returns to zero.
* Server receives all messages in order (spot check with a monotonic sequence tag you add to payloads during the test).

**Artifacts**

* Test name: `Smoke.BackPressure_ThousandSmallFrames_SerializedWrites`.
* Collected metrics: `max_write_queue_depth`, `writes_in_flight≤1` invariant.

---

## 5) Close flow — server closes; client on\_closed\_ fires; Session resolves pendings

**Goal:** On server-initiated close, your client signals upper layers and resolves all pendings with `ConnectionClosed`.&#x20;

**Tasks**

* Establish connection; start one long-running or outstanding request.
* From server side, initiate a normal WS close (and optionally a hard TCP drop as a second case).
* Ensure client Transport invokes `on_closed_`; bubble that to `Session::on_close()`.

**Prove it**

* All timers are cancelled.
* All pending requests complete with `CallError(ConnectionClosed)` (or equivalent in your error taxonomy).
* No further callbacks fire after close; new sends are rejected until reconnected (reconnect is **not** required in this milestone).

**Artifacts**

* Test name: `Smoke.CloseFlow_ServerClose_ResolvesPendings`.
* A final invariant dump: `pending_count==0`, `active_timers==0`.

---

## Cross-cutting guidance (for all five)

* **Observability:** Include structured log fields `{messageId, action, frame_type, outcome}` and a counter for `call`, `call_result`, `call_error`, `timeouts`, `closes`.
* **Clocking:** Use a controllable clock or generous timeouts to make tests deterministic.
* **Isolation:** Provide a `MockTransport` or loopback harness for (3), (4), (5) so you can inject drops, stalls, and closes deterministically later (you’ll formalize this in a later milestone).
* **Assertions:** Always assert both (a) wire behavior (what was sent/received) and (b) internal state (maps, timers, queue depth).

---

If you want, I can turn this into a **checklist you can paste into your repo’s TESTING.md** with pass/fail boxes and metrics you should capture per run.
