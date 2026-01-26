#pragma once
#include <boost/beast/websocket.hpp>
#include <boost/beast/core.hpp>
#include <boost/asio.hpp>
#include <deque>
#include "transport.hpp"
#include "router.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

struct WsServerSession : Transport, std::enable_shared_from_this<WsServerSession> {
  websocket::stream<tcp::socket> ws_;
  beast::flat_buffer buffer_;
  std::function<void(std::string_view)> on_message_;
  std::function<void()> on_open_;
  std::function<void()> on_closed_;
  std::function<void(const Call&, std::function<void(const OcppFrame&)>)> on_call_;
  std::deque<std::shared_ptr<std::string>> write_queue_;
  bool write_in_progress_ = false;
  enum class WsServerSessionState {Disconnected, Connected} state_{WsServerSessionState::Disconnected};

  WsServerSession(tcp::socket s) : ws_(std::move(s)) {
  }

  //   Register a callback invoked for each inbound OCPP Call frame received on this
  //   WebSocket session.
  //
  // Behavior:
  //   - Stores cb into on_call_ (single slot). A later call overwrites the previous handler.
  //   - Invocation happens inside do_read() after:
  //     - parsing the inbound text as JSON,
  //     - converting JSON into an OcppFrame,
  //     - and confirming it holds a Call variant.
  //
  // Callback contract:
  //   - Signature: cb(const Call& call, std::function<void(const OcppFrame&)> reply)
  //   - `reply(frame)` serializes the provided OcppFrame to JSON and sends it back on the
  //     same session via send(). (It is a convenience wrapper; the user may call it once
  //     or multiple times, but typical OCPP usage is exactly one response per call.)
  //
  // Threading / ordering:
  //   - Called from the ws_.async_read completion handler (i.e., on the underlying
  //     executor for this websocket stream).
  void on_call(std::function<void(const Call&, std::function<void(const OcppFrame&)>)> cb){on_call_ = std::move(cb);}
  
  void on_open(std::function<void()> cb) { on_open_ = std::move(cb); };

  //   Register a callback invoked when the server-side WebSocket session transitions
  //   to closed, or when a read error causes the session to stop processing.
  //
  // Behavior:
  //   - Stores cb into on_closed_ (single slot). A later call overwrites the previous handler.
  //
  // When it is invoked:
  //   - do_read(): on async_read error, if state_ != Disconnected, invokes on_closed_().
  //   - close(): after async_close completes, sets state_ = Disconnected and invokes on_closed_().
  //
  // Threading / ordering:
  //   - Invoked from async_read / async_close completion handlers on ws_.get_executor().
  void on_close(std::function<void()> cb) { on_closed_ = std::move(cb); }

  //   Register a callback invoked with the raw inbound WebSocket text payload for
  //   each successfully received message.
  //
  // Behavior:
  //   - Stores cb into on_message_ (single slot). A later call overwrites the previous handler.
  //   - Invocation happens inside do_read() immediately after converting the Beast buffer
  //     to std::string and consuming the buffer, before OCPP parsing/dispatch.
  //
  // Notes:
  //   - This is a low-level "raw text" hook; higher-level OCPP Call routing is handled
  //     by on_call() after parsing.
  //
  // Threading / ordering:
  //   - Called from the ws_.async_read completion handler on ws_.get_executor().
  void on_message(std::function<void(std::string_view)> cb) override { on_message_ = std::move(cb); }
  

  //   Accept the inbound WebSocket handshake on an already-accepted TCP socket and
  //   transition the session to Connected. To be called externally by API user
  //
  // Behavior:
  //   - Calls ws_.async_accept(...).
  //   - On success: sets state_ = Connected, logs, and begins the read loop via do_read().
  //   - On failure: logs and returns; no further I/O is started.
  //
  // Output notification:
  //   - Does not directly invoke callbacks; subsequent reads will invoke on_message_/on_call_.
  void start() override {
    auto self = shared_from_this();
    ws_.async_accept([this,self](auto ec){
      if (ec) {
        std::cerr << "WebSocket accept error: " << ec.message() << "\n";
        return;
      }
      state_ = WsServerSessionState::Connected;
      std::cout<<"WebSocket handshake accepted"<<std::endl;
      
      if( on_open_ ) on_open_();

      do_read();
    });
  }

  //   Internal API, running the continuous async read loop for inbound messages.
  //
  // Behavior:
  //   - Issues ws_.async_read(buffer_, ...).
  //   - On success:
  //     - Converts buffer_ to text, consumes it.
  //     - If on_message_ is set: invokes on_message_(text).
  //     - Parses JSON and, if it is an OCPP Call frame and on_call_ is set:
  //         invokes on_call_(call, reply_fn)
  //       where reply_fn serializes an OcppFrame to JSON and sends it using send().
  //     - Calls do_read() again to continue reading.
  //   - On read error:
  //     - Logs error.
  //     - If session was not already disconnected, invokes on_closed_ (if set) and stops reading.
  //
  // Threading / ordering:
  //   - Runs on ws_.get_executor() (the socket's associated executor).
  void do_read() {
    auto self = shared_from_this();
    ws_.async_read(buffer_, [this,self](auto ec, std::size_t){
      if (ec) {
        std::cerr << "Server WebSocket read error: " << ec.message() << "\n";
        if( (state_ != WsServerSessionState::Disconnected) && on_closed_ ) on_closed_();
        return;
      }
      std::string text = beast::buffers_to_string(buffer_.data());
      buffer_.consume(buffer_.size());
      if (on_message_) on_message_(text);

      json j = json::parse(text);
      OcppFrame f = parse_frame(j);
      if( std::holds_alternative<Call>(f) ) {
        auto call = std::get<Call>(f);
        if( on_call_ ) {
            auto cb = [this](const OcppFrame &f) {
                json j = f;
                std::string s = j.dump();
                this->send(s);
            };
            on_call_(call, cb);
        }
      }

      do_read();
    });
  }

  //   External API for sending a message as a text.
  //   The message is queued as an outbound text message and ensures a write is in progress.
  //
  // Behavior:
  //   - Moves `text` into a shared buffer and posts a task onto ws_.get_executor() to:
  //     - Drop if write_queue_ is over a hard limit (1000).
  //     - Push the buffer into write_queue_.
  //     - If no write is in progress, start the write loop via do_write().
  //
  // Notes:
  //   - This is the correct method to call from tests/diagnostics to transmit a message. 
  void send(std::string text) override {
    auto buf = std::make_shared<std::string>(std::move(text));
    auto self = shared_from_this();
    boost::asio::post(ws_.get_executor(), [this, self, buf]() {
        if( write_queue_.size() > 1000 ) {
            std::cerr << "Write queue overflow, dropping message\n";
            return;
        }
        write_queue_.push_back(buf);
        if(!write_in_progress_) {
            do_write();
        }
    });

  }

  // Internal API to dump queued outbound messages by issuing async_write operations sequentially.
  //
  // Behavior:
  //   - If queue empty: marks write_in_progress_=false and returns.
  //   - Otherwise:
  //     - Marks write_in_progress_=true.
  //     - async_write() the front message.
  //   - On completion:
  //     - Logs error (if any).
  //     - Pops the sent message.
  //     - If more queued: calls do_write() again; else marks write_in_progress_=false.
  //
  // Ordering:
  //   - Preserves FIFO ordering for queued messages (front-to-back).
  void do_write(){
    if( write_queue_.empty() ) {
        write_in_progress_ = false;
        return;
    }
    write_in_progress_ = true;

    auto &buf = *write_queue_.front();
    auto self = shared_from_this();
    ws_.async_write(boost::asio::buffer(buf), [this,self](auto ec, std::size_t len){
        if (ec) {
            std::cerr << "WebSocket write error: " << ec.message() << "\n";
        }
        write_queue_.pop_front();
        if( !write_queue_.empty()){
            do_write(); //initiate next write
        }
        else {
            write_in_progress_ = false;
        }
    });
  }

  // External API for initiating an orderly WebSocket close handshake and notifying observers.
  //
  // Behavior:
  //   - Calls ws_.async_close(normal, ...).
  //   - On completion:
  //     - Sets state_ = Disconnected.
  //     - Logs "Server WebSocket closed".
  //     - Invokes on_closed_ (if set).
  void close() override {
    auto self = shared_from_this();
    ws_.async_close(websocket::close_code::normal, [this,self](auto){
        state_ = WsServerSessionState::Disconnected;
        std::cout << "WS Server Session: Server WebSocket closed\n";
        if( on_closed_ ) on_closed_();
    });
  }
};