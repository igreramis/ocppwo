Great call. Here’s a focused plan to move from raw TCP to **WebSockets** using **Boost.Beast** (built on Asio). We’ll keep your Router/Session logic intact by putting WebSocket I/O behind a small transport interface. I’ll give you *checklists + tiny scaffolding*, not full solutions.

---

# Overall Shape

* Define an abstract **Transport** (`send(std::string)`, `on_message(std::function<void(std::string_view)>)`, `start()`, `close()`).
* Implement **WsServerSession** (server-side) and **WsClient** (client-side) using `boost::beast::websocket::stream<tcp::socket>`.
* Keep your **Router** (server) and **Session** (client) unchanged: they just see text messages.

---

# 0) Dependencies

* Beast is header-only (ships with Boost). You already link `-lboost_system -pthread`.
* If you add **TLS** later: also `-lssl -lcrypto`.

---

# 1) Define a Transport Interface

## Checklist

* [ ] Create `transport.hpp` with:

  * `struct Transport { virtual void start() = 0; virtual void send(std::string) = 0; virtual void on_message(std::function<void(std::string_view)>) = 0; virtual void close() = 0; virtual ~Transport(){} };`
* [ ] In client code, have `Session` depend on `Transport&` (not a socket).
* [ ] In server code, `ServerSession` owns a `Transport` and forwards incoming text to `Router`.

### Tiny stub

```cpp
// transport.hpp
struct Transport {
  virtual void start() = 0;
  virtual void send(std::string text) = 0;
  virtual void on_message(std::function<void(std::string_view)> cb) = 0;
  virtual void close() = 0;
  virtual ~Transport() = default;
};
```

---

# 2) WebSocket Server (Beast)

## Checklist

* [ ] Accept TCP with `async_accept` (you already do this).
* [ ] Upgrade to WebSocket: `websocket::stream<tcp::socket> ws{std::move(sock)}; ws.async_accept(...)`.
* [ ] Implement read loop with `async_read` into a `flat_buffer`.
* [ ] On read: get text via `beast::buffers_to_string(buffer.data())`, call message callback.
* [ ] On send: ensure `ws.text(true); async_write(buffer)`.
* [ ] Handle `ping/pong` and close on error.

### Tiny scaffold

```cpp
// ws_server_session.hpp (sketch)
#include <boost/beast/websocket.hpp>
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

struct WsServerSession : Transport, std::enable_shared_from_this<WsServerSession> {
  websocket::stream<tcp::socket> ws_;
  beast::flat_buffer buffer_;
  std::function<void(std::string_view)> on_msg_;

  WsServerSession(tcp::socket s) : ws_(std::move(s)) {}
  void on_message(std::function<void(std::string_view)> cb) override { on_msg_ = std::move(cb); }
  void start() override {
    auto self = shared_from_this();
    ws_.async_accept([this,self](auto ec){
      if (ec) return; // TODO: log
      do_read();
    });
  }
  void do_read() {
    auto self = shared_from_this();
    ws_.async_read(buffer_, [this,self](auto ec, std::size_t){
      if (ec) return; // TODO: log/close
      std::string text = beast::buffers_to_string(buffer_.data());
      buffer_.consume(buffer_.size());
      if (on_msg_) on_msg_(text);
      do_read();
    });
  }
  void send(std::string text) override {
    ws_.text(true);
    auto buf = std::make_shared<std::string>(std::move(text));
    auto self = shared_from_this();
    ws_.async_write(boost::asio::buffer(*buf), [this,self,buf](auto ec, std::size_t){
      if (ec) { /* TODO: log */ }
    });
  }
  void close() override {
    auto self = shared_from_this();
    ws_.async_close(websocket::close_code::normal, [this,self](auto){});
  }
};
```

**Server wiring:** when you accept a socket, create `auto sess = std::make_shared<WsServerSession>(std::move(sock));`, set `on_message(...)` to parse → route → send, then `sess->start()`.

---

# 3) WebSocket Client (Beast)

## Checklist

* [ ] `async_connect` TCP, then `ws.async_handshake(host, target)` (e.g. host = "127.0.0.1:12345", target = "/").
* [ ] Implement `async_read` loop; dispatch frames to `Session::on_message`.
* [ ] `send()` wraps `async_write` with `ws.text(true)`.

### Tiny scaffold

```cpp
// ws_client.hpp (sketch)
#include <boost/beast/websocket.hpp>
struct WsClient : Transport, std::enable_shared_from_this<WsClient> {
  tcp::resolver res_;
  websocket::stream<tcp::socket> ws_;
  beast::flat_buffer buffer_;
  std::function<void(std::string_view)> on_msg_;
  std::string host_, port_;

  WsClient(boost::asio::io_context& io, std::string host, std::string port)
    : res_(io), ws_(io), host_(std::move(host)), port_(std::move(port)) {}

  void on_message(std::function<void(std::string_view)> cb) override { on_msg_ = std::move(cb); }

  void start() override {
    auto self = shared_from_this();
    res_.async_resolve(host_, port_, [this,self](auto ec, auto results){
      if (ec) return;
      boost::asio::async_connect(ws_.next_layer(), results, [this,self](auto ec, auto){
        if (ec) return;
        ws_.async_handshake(host_, "/", [this,self](auto ec){
          if (ec) return;
          do_read();
        });
      });
    });
  }

  void do_read() {
    auto self = shared_from_this();
    ws_.async_read(buffer_, [this,self](auto ec, std::size_t){
      if (ec) return;
      std::string text = beast::buffers_to_string(buffer_.data());
      buffer_.consume(buffer_.size());
      if (on_msg_) on_msg_(text);
      do_read();
    });
  }

  void send(std::string text) override {
    ws_.text(true);
    auto buf = std::make_shared<std::string>(std::move(text));
    auto self = shared_from_this();
    ws_.async_write(boost::asio::buffer(*buf), [this,self,buf](auto ec, std::size_t){ /* TODO */ });
  }

  void close() override {
    auto self = shared_from_this();
    ws_.async_close(websocket::close_code::normal, [this,self](auto){});
  }
};
```

---

# 4) Plug into your existing Router (server) and Session (client)

## Server checklist

* [ ] In accept loop: wrap socket in `WsServerSession`.
* [ ] `sess->on_message([&](std::string_view text){ parse json → OcppFrame; if Call: reply = router.route(call); send(json(reply).dump()); })`.
* [ ] Start the session.

## Client checklist

* [ ] Construct `auto ws = std::make_shared<WsClient>(io, "127.0.0.1", "12345");`
* [ ] `ws->on_message([&](sv){ session.on_wire_message(sv); })`
* [ ] In `Session`, replace direct socket writes with `transport.send(line)`.
* [ ] Boot: `session.send_call("BootNotification", BootNotification{...}, on_reply)`; after Accepted, **start heartbeat** as before.

*(Your `Session` stays the same conceptually; the only change is using `Transport` instead of raw socket.)*

---

# 5) Acceptance checks

* [ ] **Server** upgrades and echoes OCPP text frames (no `\n` needed; WebSocket frames already delimit messages).
* [ ] **Client** connects, handshakes, sends BootNotification, receives CallResult.
* [ ] **Unknown action** → server returns `CallError(NotImplemented)`.
* [ ] **Heartbeat** runs periodically (server handler returns current time).
* [ ] **Timeout** logic in `Session` still works (no response → local CallError).

---

# 6) Common pitfalls (watch for these)

* **Text mode**: always call `ws.text(true)` before `async_write`.
* **Back-pressure**: if you allow multiple concurrent sends, add a small write queue (one write at a time).
* **Strands**: if you use multi-threaded `io_context`, put the WebSocket stream on a strand (`boost::asio::strand`) to serialize handlers.
* **UTF-8**: OCPP uses UTF-8 JSON; ensure your strings are valid.
* **Close flow**: handle `async_close` on both sides; cancel timers & clear pending maps.

---

# 7) Minimal build changes

* Beast is header-only → no extra libs.
* Keep: `-lboost_system -pthread`.
* For TLS later: add `-lssl -lcrypto`.

---

If you want, I can review your `transport.hpp`, `ws_server_session.hpp`, and `ws_client.hpp` once you draft them, keeping comments minimal so you retain ownership of the implementation.
