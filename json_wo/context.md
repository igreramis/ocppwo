Here are the contents of **`context.md`**, which defines your OCPP development roadmap and milestone prompts:

---

# OCPP Development Roadmap Context and Prompts

This file provides the **Universal Context Block** and the **milestone-specific prompts (2–6)** for continuing structured OCPP development chats.

---

## 🟦 Universal Context Block (for GPT)

I’m building a **minimal but realistic OCPP 1.6 client–server stack in modern C++**, inspired by **EVerest/libocpp**, and structured to support incremental milestones.
I’ve completed **Milestone 1 (Quick Smoke Tests)** — my tests cover Boot → Reply → Heartbeat, Unknown Action → CallError(NotImplemented), Timeout → Local CallError, Back-Pressure stress (~1 k frames), and Server-Initiated Close → ConnectionClosed cleanup.

The codebase uses **Boost.Asio / Boost.Beast** for async networking and **GoogleTest** for integration testing.
I build and run everything in **VS Code on Linux**, via `tasks.json` or CMake.

---

### ⚙️ Current Architecture

#### 🧩 Client Side

* **`WsClient`** (`ws_client.hpp`)
  Implements the WebSocket transport using Boost.Beast.
  All reads and writes run on a **strand**, guaranteeing single-writer semantics.
  It maintains:

  * A **write queue** (`std::deque<std::shared_ptr<std::string>>`)
  * Atomic counters for `writes_in_flight_`, `max_write_queue_depth_`, etc., used in smoke tests
    It exposes callbacks:
  * `on_message(std::function<void(std::string_view)>)`
  * `on_connected(std::function<void()>)`
  * `on_close(std::function<void()>)`
    The client performs async connect → handshake → read loop → dispatch to `on_message_()` and ensures all outgoing frames are serialized through `async_write`.

* **`Session`** (`session.hpp`)
  Implements OCPP message correlation and timeout logic.
  Each outbound `Call` gets a `Pending` entry containing a `steady_timer` and a resolve callback.
  Responsibilities:

  * Serialize and send `Call` frames through the transport
  * Correlate `CallResult` / `CallError` responses to their message IDs
  * Synthesize `CallError(Timeout)` if the timer expires
  * Cancel all timers and synthesize `CallError(ConnectionClosed)` on disconnect
  * Manage heartbeat timers (periodic `send_call(HeartBeat{})`)

* **`ClientUnderTest`** (used in `test_happy_path.cpp`)
  A thin harness that composes `WsClient` + `Session` for integration tests.
  On connect, it sends `BootNotification`; upon an “Accepted” reply, it starts periodic heartbeats.
  Exposes probes used in tests:
  `transport_max_writes_in_flight()`, `transport_max_write_queue_depth()`, `transport_current_write_queue_depth()`.

---

#### 🖥️ Server Side

* **`WsServerSession`** (`ws_server_session.hpp`)
  Handles one client WebSocket session.
  Performs async accept, read, and write; parses incoming OCPP frames and forwards `Call` frames to a registered `on_call` handler with a callback for sending a reply.
  Maintains its own serialized write queue.

* **`TestServer`** (`tcp_server.cpp` + `test_server.cpp`)
  Owns a TCP acceptor that spawns `WsServerSession` instances.
  Internally uses a Router to dispatch BootNotification, Authorize, and Heartbeat actions.
  It records every frame received and can:

  * Reply `Accepted` with configurable heartbeat intervals
  * Return `CallError(NotImplemented)` for unknown actions
  * Ignore requests to simulate timeouts
  * Force connection close to simulate disconnections
    Exposes accessors for tests (`received()`, `heartbeats()`, `last_boot_msg_id()`, etc.).

---

#### 🧪 Integration Tests

The smoke tests (Milestone 1) use `ClientUnderTest` ↔ `TestServer` over loopback WebSocket to verify:

1. **BootAcceptedThenHeartbeat** — boot → heartbeat cadence
2. **UnknownAction_Yields_NotImplemented** — CallError handling
3. **Timeout_NoServerReply_LocalTimeoutError** — local timeout synthesis
4. **BackPressure_ThousandSmallFrames_SerializedWrites** — write queue behavior
5. **CloseFlow_ServerClose_ResolvesPendings** — cleanup on server close

---

### ✅ Current Status

All five Milestone 1 tests pass.
The stack now supports full-duplex WebSocket communication, request–reply correlation, timeouts, heartbeats, serialized writes, and graceful shutdown handling.
The next milestones (2–6) will progressively introduce dispatch logic, reconnect state, correlation tracking, reconnection loops, and metrics — following the structure of `libocpp`.

---

## 🚩 Milestone 2 — Dispatch and Router

**Goal:** Add typed handler registration and dynamic dispatch (mirroring `Router` in libocpp).
Focus: mapping action → callable, correct deserialization, and unit tests proving each action dispatches to the right handler.

**Prompt to start chat:**

> In this chat we’ll implement **Milestone 2 (Dispatch and Router)** from my OCPP roadmap.
> I want step-by-step exercises and guidance to design a type-safe `Router` class that registers handlers for actions like `BootNotification`, `Authorize`, etc., decodes incoming `Call` payloads into the correct structs, and returns `CallResult` or `CallError` frames.
> Please recall the context block above while generating all explanations and examples.

---

## 🚩 Milestone 3 — Session Reconnect

**Goal:** Teach the client to reconnect after a drop, preserving pending state or resetting safely.

**Prompt to start chat:**

> In this chat we’ll cover **Milestone 3 (Session Reconnect)**.
> The goal is to detect disconnections from the transport, call `on_closed_`, and perform a clean reconnect loop (no message loss or double send).
> I’d like structured exercises: designing reconnect state machine, re-establishing WebSocket, and testing reconnection behavior with the `CsmsStub`.

---

## 🚩 Milestone 4 — Call Correlation & Reply Tracking

**Goal:** Implement a correlation map between `Call` message IDs and their matching `CallResult`/`CallError`.

**Prompt to start chat:**

> This chat focuses on **Milestone 4 (Call Correlation and Reply Tracking)**.
> We’ll design the pending map for messageId → promise, ensure correct resolution when a reply arrives, and test edge cases (duplicate IDs, mismatched types, and timeouts).
> Please base the exercises on the context above and align with libocpp’s approach.

---

## 🚩 Milestone 5 — End-to-End Behavior & Reconnect Loop

**Goal:** Integrate everything into a stable loop that can disconnect, reconnect, re-authenticate, and continue heartbeats.

**Prompt to start chat:**

> Let’s work on **Milestone 5 (End-to-End Behavior and Reconnect Loop)**.
> I want to test full lifecycle scenarios: connect → boot → heartbeat → forced close → automatic reconnect → resume heartbeats.
> Please guide me through incremental exercises and design notes referencing libocpp’s session loop patterns.

---

## 🚩 Milestone 6 — Integration & Metrics

**Goal:** Add observability: counters for messages, queue depth, heartbeats, reconnect attempts, etc.

**Prompt to start chat:**

> This chat is for **Milestone 6 (Integration and Metrics)**.
> I want to instrument the OCPP client with metrics (timing, counters, error tallies) and expose them to tests or logs.
> Please structure the work in steps and relate it to how libocpp surfaces metrics internally.

---
