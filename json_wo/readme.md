Awesome—yep, I can use your context file and mirror how libocpp actually wires messages → handlers.

First, quick reality-check from upstream:

* libocpp encodes/decodes OCPP frames as JSON arrays and defines `MessageTypeId`, `MessageId`, and the exact array indices for type/action/payload in `include/ocpp/common/call_types.hpp`. That file shows how `Call<T>`, `CallResult<T>` and `CallError` are serialized (e.g., CALL puts `[2, <id>, <action>, <payload>]`; CALLRESULT is `[3, <id>, <payload>]`). ([GitHub][1])
* The 1.6 “entry point” is the `ocpp::v16::ChargePoint` class; the docs point to `v16/charge_point.hpp` as the surface for hooking app logic, i.e., the place that ultimately routes messages. ([GitHub][2])

And yes—your next step (Milestone 2) is exactly “typed handler registration and dynamic dispatch, decode request into typed structs, return `CallResult`/`CallError`,” per your context. 

# Milestone 2 exercise — “Router: action → typed handler”

## Goal

Build a **type-safe Router** that:

1. maps `action` → handler,
2. parses incoming `Call` frames,
3. deserializes the payload into the correct request struct,
4. executes the handler,
5. returns either a properly formatted `CallResult` or `CallError` frame.

Design and tests should mirror libocpp’s JSON array layout & message IDs. ([GitHub][1])

---

## Deliverables

1. `router.hpp/.cpp` with:

   * `Router::register_handler<ActionTag>(HandlerFn)`
   * `Router::handle_incoming(std::string_view frame, SendFn send)`
     where `SendFn` sends a string frame back to the peer.
2. New unit tests (GTest) proving dispatch & errors.
3. A tiny “message model” header with `struct BootNotificationReq/Conf`, `AuthorizeReq/Conf` (min fields).
4. Server harness glue in your existing `WsServerSession` to call `Router::handle_incoming`.

---

## Required API (suggested)

```cpp
// message_tags.hpp
struct BootNotificationTag { static constexpr const char* action = "BootNotification"; };
struct AuthorizeTag       { static constexpr const char* action = "Authorize"; };

// messages.hpp (trim to what you need)
struct BootNotificationReq { std::string chargePointModel; std::string chargePointVendor; };
struct BootNotificationConf { std::string status; int interval; };
struct AuthorizeReq { std::string idTag; };
struct AuthorizeConf { std::string idTagInfo_status; };

// router.hpp
class Router {
public:
  using Json = nlohmann::json;

  template <typename Tag, typename Req, typename Conf>
  using Handler = std::function<tl::expected<Conf, /*CallError spec*/ std::string>(const Req&)>;

  template <typename Tag, typename Req, typename Conf>
  void register_handler(Handler<Tag, Req, Conf> h);

  // Parse frame, dispatch, and invoke send(result_frame)
  void handle_incoming(std::string_view frame, std::function<void(std::string&&)> send);

private:
  // action -> type-erased invoker
  using Invoker = std::function<void(const std::string& uniqueId, const Json& payload,
                                     std::function<void(std::string&&)> send)>;
  std::unordered_map<std::string, Invoker> table_;
};
```

Key points your implementation must follow (grounded in libocpp’s layout):

* Extract `MESSAGE_TYPE_ID`, `MESSAGE_ID`, `CALL_ACTION`, `CALL_PAYLOAD` from the JSON array using the **exact indices** libocpp uses. If it’s not a CALL (2), **ignore** or **error**. ([GitHub][1])
* For unknown `action`, reply with a **CallError** using OCPP error code `NotImplemented`. Format: `[4, "<same id>", "NotImplemented", "Unknown action", {}]`. (The tuple order and the fact there are 5 elements belong to the same file.) ([GitHub][1])
* On success, return `CallResult` as `[3, "<same id>", <response-payload>]`. ([GitHub][1])
* If payload JSON → struct fails, return `CallError("FormationViolation", "...")`. (Choice of code is up to you; libocpp raises on bad JSON fields and has open issues around validating/catching JSON exceptions—your router should *not* crash on `.at()`.) ([GitHub][3])

---

## Step-by-step tasks

### 1) Type-erased registration

Implement `register_handler<Tag, Req, Conf>(h)` to insert an `Invoker` into `table_` keyed by `Tag::action`.

* The invoker should:

  * `Req req = payload.get<Req>();` (use `from_json` overloads you write)
  * Call the concrete handler → `tl::expected<Conf, std::string>`
  * If `ok`, build `CallResult` array `[3, id, json(conf)]`
  * If `err`, build `CallError` array `[4, id, "InternalError", err, {}]` (or propagate handler-provided code if you model it)
  * `send(json(...).dump())`

Use the **same array shape** libocpp uses to keep compatibility with your client tests. ([GitHub][1])

### 2) Frame parsing & validation

`handle_incoming` should:

* Parse JSON into an array and verify `arr[0] == 2` (CALL).
* Extract `id = arr[1].get<std::string>()`, `action = arr[2].get<std::string>()`, `payload = arr[3]`.
* Lookup `table_[action]`. If missing → `CallError(NotImplemented, ...)`.
* Guard all `.at()`/`get<>()` with try/catch and convert **any** exception into `CallError("FormationViolation", "Bad JSON", {...})` (to avoid the crash patterns reported upstream). ([GitHub][3])

### 3) Message models & (de)serialization

Provide minimal `from_json/to_json` for your `BootNotificationReq/Conf` and `AuthorizeReq/Conf`.
Keep them tiny; the goal here is the **dispatch**, not full spec coverage.

### 4) Wire it into your server

In `WsServerSession::on_read`, when you detect a CALL frame, delegate to `router.handle_incoming(...)`, passing a lambda that enqueues the string on your **serialized write queue** (you already have this in Milestone 1). 

### 5) Tests (GTest)

Create `test_router.cpp` with these cases:

1. **BootNotification dispatches**

   * Register `BootNotificationTag` handler that returns `{"status":"Accepted","interval":10}`.
   * Send a real frame: `[2, "abc", "BootNotification", {"chargePointModel":"X","chargePointVendor":"Y"}]`
   * Assert server replies with `[3, "abc", {"status":"Accepted","interval":10}]` (ignore key order).

2. **Authorize dispatches**

   * Register `AuthorizeTag` handler; return a minimal `{"idTagInfo_status":"Accepted"}`.
   * Check `[3, "<same id>", {...}]`.

3. **Unknown action → NotImplemented**

   * No registration for `"Foo"`.
   * Expect `[4, "<id>", "NotImplemented", "Unknown action", {}]`.

4. **Malformed payload → FormationViolation (no crash)**

   * Send `[2, "id", "BootNotification", {"chargePointVendor":123}]` or missing field.
   * Handler is never called; expect `[4, "id", "FormationViolation", "...", {}]`.
   * This explicitly protects you against the uncaught JSON exceptions noted in libocpp issues. ([GitHub][3])

5. **Handler throws → InternalError**

   * Make handler throw; router catches and returns `[4, "id", "InternalError", "...", {}]`.

6. **Back-to-back dispatches keep order**

   * Fire N=100 calls rapidly; ensure replies match request IDs and order isn’t violated by your write queue (you already serialize writes on a strand). 

---

## Acceptance checklist

* [ ] Router compiles without exposing template bloat to users (keep most logic in `.cpp` via type-erasure).
* [ ] All tests above pass against your loopback server.
* [ ] No unhandled exceptions from `nlohmann::json` reach the process boundary (prove with a test). ([GitHub][3])
* [ ] Reply frames strictly follow the array shape/indices defined in libocpp. ([GitHub][1])

---

## Hints that line up with libocpp

* Use the **exact** indices `MESSAGE_TYPE_ID=0`, `MESSAGE_ID=1`, `CALL_ACTION=2`, `CALL_PAYLOAD=3` when parsing; it keeps your wire format compatible with libocpp’s helpers and with your existing client. ([GitHub][1])
* Keep the Router **transport-agnostic**. Your `WsServerSession` should just provide `send(string)` and feed raw frames into `handle_incoming`. That mirrors how libocpp separates transport from message logic in its charge point implementation docs. ([GitHub][2])

---

If you want, I can sketch the `Invoker` lambda and one `register_handler` instantiation next.

[1]: https://github.com/EVerest/libocpp/blob/main/include/ocpp/common/call_types.hpp?utm_source=chatgpt.com "libocpp/include/ocpp/common/call_types.hpp at main · EVerest ... - GitHub"
[2]: https://github.com/EVerest/libocpp/blob/main/doc/common/getting_started.md?utm_source=chatgpt.com "libocpp/doc/common/getting_started.md at main · EVerest/libocpp"
[3]: https://github.com/EVerest/libocpp/issues/1117?utm_source=chatgpt.com "Uncaught exception while accessing JSON data when input is ... - GitHub"
