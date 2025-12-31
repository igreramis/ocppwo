# Milestone 4 exercise — “Call correlation map + exactly-once reply resolution”

## What you’re building (target behavior)

You will add a **pending correlation map**:

* On sending a `Call`, create a **Pending** entry keyed by `messageId`.
* When a `CallResult` / `CallError` arrives:

  * Look up the entry by `messageId`
  * Resolve the promise/callback **exactly once**
  * Remove it from the map
* Handle edge cases:

  * duplicate messageIds on send
  * reply for unknown id (late / wrong / duplicate)
  * mismatched types (decode failure / wrong expected payload)
  * timeout per pending entry
  * connection close → fail all pending with ConnectionClosed

This is the backbone of “request/response” correctness.

---

## Exercise 0 — Harness prep for deterministic reply delivery (10–20 min)

**Goal:** Make it trivial to inject inbound frames and capture outbound frames without real sockets.

**Touch:**

* `tests/` (new helper: `fake_transport.hpp` or extend existing CsmsStub harness)
* (optional) `session.hpp/.cpp` if you need small hooks

**Implement (signatures only):**

```cpp
// tests/fake_transport.hpp
struct SentFrame { std::string text; };

struct FakeTransport {
  std::vector<SentFrame> sent;
  std::function<void(std::string_view)> on_text; // session registers this

  void send_text(std::string_view s) { sent.push_back({std::string(s)}); }
  void inject_inbound(std::string_view s) { if(on_text) on_text(s); }
  void close() { /* call session on_close hook if you have one */ }
};
```

**Write this test first (should FAIL):**

* `TEST(Harness, CanInjectInboundAndCaptureOutbound)`

**Acceptance:** You can run Session unit tests without WebSocket/threads.

**Hints:** keep it dumb—no parsing in the harness.

---

## Exercise 1 — Add a Pending entry + map keyed by messageId (15–25 min)

**Goal:** When Session sends a Call, it registers a Pending by `messageId`.

**Touch:**

* `session.hpp/.cpp` (or a new `call_tracker.hpp/.cpp` if you want a seam)

**Implement (signatures only):**

```cpp
using MessageId = std::string;

struct Pending {
  // whatever you use: promise, callback, expected type tag, timer handle…
};

class Session {
  // ...
  std::unordered_map<MessageId, Pending> pending_;
};
```

**Write this test first (should FAIL):**

* `TEST(Session, SendingCallAddsPendingEntry)`

  * Call `send_call(...)`
  * Assert `pending_.size() == 1` (or expose `pending_count()`)

**Acceptance:** One outbound Call → exactly one pending entry.

**Hints:** if you don’t want to expose internals, add `size_t pending_count() const`.

---

## Exercise 2 — Enforce unique messageId (duplicate outbound IDs are rejected) (15–25 min)

**Goal:** Prevent overwriting an existing pending entry.

**Touch:** `session.*`

**Implement:**

* If `messageId` already exists in `pending_`, do one of:

  * **Option A (recommended):** return an immediate error (e.g., `CallError{InternalError,"duplicate messageId"}`)
  * Option B: throw (not great for async APIs)
  * Option C: generate a new id automatically (fine, but then *your id generator must guarantee uniqueness*)

**Write this test first (should FAIL):**

* `TEST(Session, DuplicateMessageIdIsRejectedOrNeverOverwrites)`

  * Force id generator to return `"A"` twice
  * Send two calls
  * Assert second call fails immediately OR pending map still has the first entry unchanged

**Acceptance:** duplicates never cause “lost” pendings.

**Hints:** use `unordered_map::emplace` return value.

---

## Exercise 3 — Correlate CallResult: resolve once, erase pending (20–35 min)

**Goal:** A valid `CallResult` with matching `messageId` resolves the pending and removes it.

**Touch:**

* `session.cpp` inbound message handler / decoder
* pending resolution mechanism (promise/callback)

**Implement:**

* Parse incoming frame → detect it’s a CallResult → extract `messageId`
* Find pending
* Resolve
* Erase
* Return/record “matched = true”
* If not found: “unmatched = true” (ignored but observable)

**Write this test first (should FAIL):**

* `TEST(Session, CallResultResolvesPendingExactlyOnce)`

  * Send a call (id = "A")
  * Inject inbound CallResult("A", payload)
  * Assert callback/promise resolved
  * Inject the same CallResult again
  * Assert resolved count still == 1 and pending_count()==0

**Acceptance:** exactly-once semantics.

**Hints:** “erase first then resolve” vs “resolve then erase” — pick one and ensure no reentrancy surprises.

---

## Exercise 4 — Correlate CallError: resolve once, erase pending (20–35 min)

**Goal:** Same as Exercise 3, but for CallError frames.

**Write this test first (should FAIL):**

* `TEST(Session, CallErrorResolvesPendingExactlyOnce)`

**Acceptance:** CallError behaves identically to CallResult in correlation lifecycle.

---

## Exercise 5 — Timeout per pending entry (25–45 min)

**Goal:** Each pending Call has a timeout; when it fires, you resolve with `Timeout` and erase.

**Touch:**

* `TimerPump` / fake timers (reuse from Milestone 3)
* `session.*`

**Implement (sketch):**

* When you create Pending, schedule a timer:

  * on fire: if still pending → resolve Timeout, erase

**Write this test first (should FAIL):**

* `TEST(Session, PendingCallTimesOutAndIsRemoved)`

  * Send Call "A" with timeout 100ms
  * Advance timers to 99ms → not resolved
  * Advance to 100ms → resolved with Timeout
  * pending_count()==0
  * Later injecting CallResult("A") is unmatched/ignored

**Acceptance:** timeout is deterministic and prevents late replies from double-resolving.

**Hints:** You need cancellation: when resolved normally, cancel/disable the timeout callback.

---

## Exercise 6 — Close / disconnect fails all pendings with ConnectionClosed (25–40 min)

**Goal:** When the transport closes, everything pending resolves with ConnectionClosed.

**Touch:**

* `Session::on_close()` or your reconnect glue callback path

**Implement:**

* On close: iterate all pending entries, resolve with ConnectionClosed, clear map
* Ensure their timeouts won’t later fire and re-resolve

**Write this test first (should FAIL):**

* `TEST(Session, CloseFailsAllPendingAndClearsMap)`

  * Send 3 calls: A,B,C
  * Trigger close
  * Assert 3 failures with ConnectionClosed
  * pending_count()==0
  * Run timers → no additional resolutions

**Acceptance:** no silent drops; no late timeouts after close.

---

## Exercise 7 — Unmatched replies are ignored but measurable (15–30 min)

**Goal:** If a reply arrives for an unknown id (never sent, timed out, already resolved), it doesn’t crash or mutate state.

**Touch:** `session.*`

**Implement:**

* If lookup fails:

  * ignore
  * optionally increment a counter `unmatched_replies_`

**Write this test first (should FAIL):**

* `TEST(Session, UnmatchedReplyDoesNotResolveAnything)`

  * Inject CallResult("NOPE")
  * Assert no resolution triggered; pending_count unchanged
  * Optionally assert `unmatched_replies_ == 1`

**Acceptance:** stable under duplicates/late/out-of-order frames.

**Hints:** this counter becomes a useful metric later.

---

## Exercise 8 — “Mismatched type” handling (decode/shape mismatch → ProtocolError) (30–60 min)

**Goal:** If you find a pending entry, but cannot decode the payload into the expected response type, resolve **ProtocolError** (or equivalent) and erase.

This is explicitly called out as a Milestone 4 edge case in your context.

**Touch:**

* response parsing/decoding path
* Pending must store “expected response type tag” (string/enum)

**Implement idea:**

```cpp
struct Pending {
  std::string expected_action; // or expected_response_tag
  // promise/callback
};
```

**Write this test first (should FAIL):**

* `TEST(Session, MismatchedReplyTypeResolvesProtocolError)`

  * Send Call "A" with expected response “BootNotification”
  * Inject CallResult("A") with payload shaped like some other response (or invalid json)
  * Assert resolved with ProtocolError (not Timeout/Closed)
  * pending_count()==0

**Acceptance:** wrong replies don’t wedge the system; they fail deterministically.

**Hints:** start shallow: “decode fails” is enough for mismatch in v1; strengthen later.

---

## Exercise 9 — Cancel timeout on successful resolution (15–25 min)

**Goal:** If CallResult arrives before timeout, the timeout must not fire later.

**Write this test first (should FAIL):**

* `TEST(Session, TimeoutIsCancelledOnCallResult)`

  * Send Call "A" with timeout 100ms
  * Inject CallResult("A") at 10ms
  * Advance timers past 100ms
  * Assert resolution count == 1 and no later Timeout

**Acceptance:** ensures “exactly-once” across all paths.

**Hints:** store a cancel flag/token in Pending; timeout callback checks it (or remove timer entry).

---

## Exercise 10 — Integrate with CsmsStub end-to-end (optional, 45–90 min)

**Goal:** Prove correlation works over the real stub server too.

**Touch:** your existing `CsmsStub` tests

**Write this test first (should FAIL initially):**

* `TEST(E2E, TwoOutstandingCallsResolveToCorrectRequest)`

  * Send Call A, then Call B before A resolves
  * Server replies B first, then A
  * Assert the right callbacks/promises resolve with the right payloads

**Acceptance:** out-of-order replies are handled correctly.

**Hints:** This is the “real world” validation: maps exist *because order isn’t guaranteed*.

---

# Definition of Done for Milestone 4

You’re done when you can prove (with tests):

* ✅ pending entries are created on send
* ✅ CallResult/CallError resolve **exactly once** and erase
* ✅ per-call timeouts resolve Timeout and erase
* ✅ disconnect resolves all pendings with ConnectionClosed
* ✅ unmatched replies are safe (ignored, optionally counted)
* ✅ decode/type mismatch produces ProtocolError (or equivalent) and erases
* ✅ out-of-order replies still correlate correctly

---

If you paste (or upload) the current `Session` API shape (how you represent a Call send and how replies come in), I’ll **adapt these exercises to your exact names/types** (promises vs callbacks, where TimerPump lives, how message frames are represented) — still without giving you full solutions.
