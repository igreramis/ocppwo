#ifndef WS_CLIENT_HPP
#define WS_CLIENT_HPP

#include <boost/beast/websocket.hpp>
#include <boost/beast/core.hpp>
#include <boost/asio.hpp>
#include <deque>
#include "transport.hpp"
#include "signals.hpp"
#include "metrics.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

/*
 * WsClient
 *
 * Purpose:
 *   Concrete Transport implementation backed by Boost.Beast WebSocket over TCP.
 *   Manages connect/handshake, continuous read loop, serialized write queue, and
 *   an orderly close handshake.
 *
 * Inputs (public methods / hooks the user provides):
 *   - start()                          : begin async resolve/connect/handshake and start read loop.
 *   - send(std::string text)           : enqueue a text message to be written (thread-safe via strand_).
 *   - close()                          : initiate WebSocket close handshake.
 *   - on_message(cb)                   : register inbound message callback (single slot; overwrites).
 *   - on_connected(cb)                 : register "connected/handshake complete" callback (single slot; overwrites).
 *   - on_close(cb)                     : register "closed" callback (single slot; overwrites).
 *
 * Outputs (notifications / observable effects):
 *   - on_message_(std::string_view)    : delivered for each successfully read text frame.
 *   - on_connected_()                  : delivered once after successful WebSocket handshake.
 *   - on_closed_()                     : delivered when the connection closes or a read error triggers close path.
 *   - transport_* metrics              : queue/in-flight write telemetry for diagnostics/backpressure monitoring.
 *   - Logging to stdout/stderr         : connection progress and error messages.
 *
 * Concurrency / ordering notes:
 *   - Writes (send/do_write) are serialized on strand_ to preserve FIFO ordering and avoid concurrent async_write.
 *   - Read/connect/close callbacks are invoked from io_context completion handlers and are not explicitly
 *     dispatched onto strand_. If io_context is run on multiple threads, owning glue should re-post callbacks
 *     onto an application/Session/ReconnectController strand to avoid concurrent higher-level state mutation.
 */
struct WsClient : Transport, std::enable_shared_from_this<WsClient> {
  tcp::resolver res_;
  websocket::stream<tcp::socket> ws_;
  // boost::asio::strand<boost::asio::io_context::executor_type> strand_{ws_.get_executor()};
  boost::asio::strand<websocket::stream<tcp::socket>::executor_type> strand_;
  beast::flat_buffer buffer_;
  std::function<void(std::string_view)> on_message_;
  std::function<void()> on_connected_;
  std::function<void()> on_closed_;
  std::function<void(std::string_view)> on_ping_; //optional diagnostics hook
  std::string host_, port_;
  std::deque<std::shared_ptr<std::string>> write_queue_;
  bool write_in_progress_ = false;
  std::atomic<unsigned> writes_in_flight_{0}, max_writes_in_flight_{0};
  std::atomic<unsigned> max_write_queue_depth_{0}, current_write_queue_depth_{0};
  enum class WsClientState {Disconnected, Connected, Connecting} state_{WsClientState::Disconnected};
  Metrics &metrics_;
  bool close_pending_{false}, close_started_{false};
  WsClient(boost::asio::io_context& io, std::string host, std::string port, Metrics &metrics)
    : res_(io),
      ws_(io),
      host_(std::move(host)),
      port_(std::move(port)),
      strand_(ws_.get_executor()),
      metrics_(metrics)
      {}

  // - Public API to register a callback for inbound WebSocket text messages.
  // - The callback is stored in on_message_ (single slot). A later call overwrites
  //   the previous callback (no accumulation).
  // - Invocation: do_read() calls on_message_(text) after each successful async_read,
  //   where `text` is the decoded payload from the Beast flat_buffer.
  // - Delivery semantics:
  //   - Called once per received frame that is read successfully.
  //   - Called only when `do_read()` completes without error; on read error the close
  //     path may run instead (on_closed_).
  // - Threading/ordering: invoked from the io_context handler that completes async_read.
  //   It is not explicitly dispatched onto strand_. If io_context is run on multiple
  //   threads, the callback may run concurrently with other non-strand handlers unless
  //   the owning glue re-posts it onto a strand.
  void on_message(std::function<void(std::string_view)> cb) override { on_message_ = std::move(cb); }
  
  // - Public API to register a callback for the "connected" event (post-handshake).
  // - Current behavior (as implemented now): stores cb into on_connected_ (single slot).
  //   A later call overwrites the previous callback (no accumulation).
  // - Invocation: start() calls on_connected_() once the WebSocket handshake completes.
  // - Threading/ordering: the callback is invoked from the io_context handler that
  //   completes the handshake (not explicitly dispatched onto strand_). If you run
  //   io_context on multiple threads, callers should assume this callback may run
  //   concurrently with other non-strand handlers unless the glue re-posts it onto
  //   a strand.
  void on_connected(std::function<void()> cb) {
    on_connected_ = std::move(cb);
  };

  //   Register a callback that fires when the WebSocket connection transitions to
  //   "closed" (or a read/write failure is treated as a close).
  //
  // Behavior:
  //   - Stores cb into on_closed_ (single slot). A later call overwrites the previous
  //     callback (no accumulation).
  //
  // When it is invoked:
  //   - do_read(): on async_read error, if state_ is not Disconnected, invokes on_closed_().
  //   - close(): after async_close completes, sets state_ = Disconnected and invokes on_closed_().
  //
  // Threading / ordering:
  //   - Invoked from the io_context handlers for async_read / async_close.
  //   - Not explicitly dispatched onto strand_. If io_context is run on multiple threads,
  //     this callback may run concurrently with other non-strand handlers unless the
  //     owning glue re-posts it onto a strand.
  void on_close(std::function<void()> cb) { 
    on_closed_ = std::move(cb); 
  }

  // on_ping(cb)
  // - Optional hook invoked when a WS Ping control frame is received.
  // - Single slot; later calls overwrite.  
  void on_ping(std::function<void(std::string_view)> cb) { 
    on_ping_ = std::move(cb);
  }

  //   Begin establishing the WebSocket connection and start the read loop.
  //
  // Behavior (async chain):
  //   1) async_resolve(host_, port_) to obtain TCP endpoints.
  //   2) async_connect() the underlying TCP socket.
  //   3) async_handshake(host_, "/") to upgrade to WebSocket.
  //   4) On handshake success:
  //      - set state_ = Connected
  //      - ws_.text(true) (text mode)
  //      - invoke on_connected_() if set
  //      - call do_read() to begin continuous async_read loop.
  //
  // Error handling:
  //   - On resolve/connect/handshake error: logs to stderr and returns without retry.
  //     (No on_closed_ notification is emitted for these pre-connected failures.)
  //
  // Threading / ordering:
  //   - All completion handlers run on the io_context associated with this WsClient.
  //   - Not explicitly serialized onto strand_ (strand_ is currently used for writes).
  //     If io_context is run on multiple threads, these handlers may run concurrently
  //     with other non-strand handlers unless the owning glue re-posts onto a strand.
  void start() override {
    auto self = shared_from_this();
    res_.async_resolve(host_, port_, [this,self](auto ec, auto results){
      if (ec) {
        std::cerr << "WebSocket resolve error: " << ec.message() << "\n";
        return;
      }

      std::cout << "Ws Client: Resolved.\n";

      boost::asio::async_connect(ws_.next_layer(), results, [this,self](auto ec, auto){
        if (ec) {
          std::cerr << "WebSocket connect error: " << ec.message() << "\n";
          return;
        }

        std::cout<<"Ws Client: Connected.\n";
        state_  = WsClientState::Connecting;

        ws_.async_handshake(host_, "/", [this,self](auto ec){
          if (ec) {
            std::cerr << "WebSocket handshake error: " << ec.message() << "\n";
            metrics_.transport_on_close_event();
            on_closed_(); // notify close on handshake failure
            return;
          }

          state_ = WsClientState::Connected;
          std::cout << "Ws Client: Handshake complete!\n";

          ws_.control_callback([self = this->shared_from_this()](websocket::frame_type kind, beast::string_view payload) {
            if (kind == websocket::frame_type::ping) {
              if (self->on_ping_) self->on_ping_(std::string_view(payload.data(), payload.size()));

              //reply with pong(echo payload). use async+strand to serialize with other writes
              // auto data = std::make_shared<std::string>(payload.data(), payload.size());
              websocket::ping_data pd;
              pd.assign(payload);
              
              boost::asio::post(self->strand_, [self, pd]{
                self->ws_.async_pong(pd,
                  boost::asio::bind_executor(self->strand_, [pd](boost::system::error_code){
                    //ignore errors; close path will handle disconnects
                  })
                );
              });
            }
          });
          ws_.text(true);
          
        //connect and boot
        //   send("BootNotification or your initial message");
          if( on_connected_ ) on_connected_();

          do_read();
        });
      });
    });
  }

  //   Run the WebSocket receive loop. Issues an async_read and, on success, delivers the
  //   decoded text payload to on_message_ then immediately schedules the next read.
  //
  // Behavior:
  //   - Calls ws_.async_read(buffer_, handler).
  //   - On success:
  //     - Converts buffer_ -> std::string (buffers_to_string)
  //     - Consumes buffer_
  //     - Invokes on_message_(text) if set
  //     - Recursively calls do_read() to continue reading.
  //   - On error:
  //     - Logs error
  //     - If state_ != Disconnected, invokes on_closed_() (if set) and stops the loop.
  //
  // Threading / ordering:
  //   - Handler runs on the io_context associated with ws_.
  //   - Not explicitly dispatched onto strand_ (unlike writes). If io_context is run on
  //     multiple threads, this handler may run concurrently with other non-strand handlers
  //     unless the owning glue re-posts onto a strand.
  void do_read() {
    auto self = shared_from_this();
    ws_.async_read(buffer_, [this,self](auto ec, std::size_t){
      if (ec) {
        std::cerr << "Ws Client: Client WebSocket read error: " << ec.message() << "\n";

        // Treat read failures as a close-type event for transport metrics.
        metrics_.transport_on_close_event();

        if( (state_ != WsClientState::Disconnected) && on_closed_ ) {
          std::cout<<"Ws Client: callign on_closed_()"<<"\n";
          on_closed_();
        }

        return;
      }
      std::string text = beast::buffers_to_string(buffer_.data());
      buffer_.consume(buffer_.size());
      if (on_message_) on_message_(text);
      do_read();
    });
  }

  //   Public API intended to be called by external code (higher level modules, tests, diagnostics)
  //   to transmit a WebSocket text frame.
  //
  // Behavior:
  //   - Moves `text` into a shared buffer and posts a task onto strand_ to:
  //     - Drop if write_queue_ is over a hard limit (1000).
  //     - Push the buffer into write_queue_.
  //     - Update queue depth metrics.
  //     - If no write is in progress, call do_write() to start sending.
  //
  // Threading / ordering:
  //   - All queue mutation happens on strand_ (via post(bind_executor(strand_, ...))).
  //   - Preserves FIFO ordering of writes: messages are written in the order enqueued.
  void send(std::string text) override {   
    auto buf = std::make_shared<std::string>(std::move(text));
    auto self = shared_from_this();
    boost::asio::post(boost::asio::bind_executor(strand_, [this, self, buf]() {
        if( close_pending_ )
          return;

        if( write_queue_.size() > 1000 ) {
            std::cerr << "Write queue overflow, dropping message\n";
            return;
        }
        write_queue_.push_back(buf);
        // writes_in_flight_++; 
        current_write_queue_depth_++;
        max_write_queue_depth_ = std::max(max_write_queue_depth_.load(), current_write_queue_depth_.load());
        metrics_.transport_on_write_enqueued(static_cast<uint64_t>(current_write_queue_depth_.load()));

        // max_writes_in_flight_ = std::max(max_writes_in_flight_.load(), writes_in_flight_.load());
        if(!write_in_progress_) {
            do_write();
        }
    }));

  }

  //   Internal send helper that maintains odering/backpressure and enqueuing send operations on 
  //   the strand_.
  //
  // Behavior:
  //   - If write_queue_ is empty: marks write_in_progress_=false and returns.
  //   - Otherwise:
  //     - Marks write_in_progress_=true, increments writes_in_flight_.
  //     - Issues ws_.async_write(buffer(front), handler) bound to strand_.
  //   - On completion:
  //     - Logs any error
  //     - Pops the sent item from write_queue_
  //     - Updates metrics (writes_in_flight_, current_write_queue_depth_, maxima)
  //     - If more queued: calls do_write() again; else marks write_in_progress_=false.
  //
  // Threading / ordering:
  //   - Completion handler is bound to strand_, so queue state changes and the write chain
  //     are serialized (safe even if io_context is multi-threaded).
  void do_write(){
    //retreive buffer from ll(access and then remove)
    if( write_queue_.empty() ) {
        write_in_progress_ = false;      
        return;
    }
    write_in_progress_ = true;

    auto &buf = *write_queue_.front();
    auto self = shared_from_this();
    writes_in_flight_++;
    metrics_.transport_observe_writes_in_flight(static_cast<uint64_t>(writes_in_flight_.load()));
    ws_.async_write(boost::asio::buffer(buf), boost::asio::bind_executor(strand_,[this,self](auto ec, std::size_t len){
        if (ec) {
            std::cerr << "WebSocket write error: " << ec.message() << "\n";
        }
        write_queue_.pop_front();

        writes_in_flight_--; current_write_queue_depth_--;
        metrics_.transport_on_write_completed(static_cast<uint64_t>(current_write_queue_depth_.load()));
        metrics_.transport_observe_writes_in_flight(static_cast<uint64_t>(writes_in_flight_.load()));
        max_writes_in_flight_ = std::max(max_writes_in_flight_.load(), writes_in_flight_.load());

        if( !write_queue_.empty()){
            do_write(); //initiate next write
        }
        else {
            write_in_progress_ = false;
            tentative_close(shared_from_this());
        }
    }));
  }

  // close()
  //
  // Purpose:
  //   Request an orderly WebSocket close handshake.
  //
  // Key constraint (Boost.Beast):
  //   ws_.async_close(...) must not overlap with an in-flight ws_.async_write(...).
  //   Starting close while a write is outstanding can trigger a Beast soft_mutex assertion.
  //
  // Behavior:
  //   - Posts onto strand_ and marks close_pending_ = true.
  //   - Calls tentative_close(self) to attempt to start ws_.async_close.
  //   - If the write pipeline is still busy, this call is non-blocking; close will be
  //     attempted again when the write pipeline drains (see do_write() completion).
  //
  // Threading / ordering:
  //   - This method is safe to call from any thread: it funnels state changes onto strand_.
  //   - The actual async_close initiation is gated by tentative_close() and serialized
  //     with the write pipeline.
  void close() override {
    
    auto self = shared_from_this();
    
    boost::asio::post(strand_, [this, self]{
      close_pending_ = true;
      tentative_close(self);        
      });
    
    return;
  }

  // tentative_close(self)
  //
  // Purpose:
  //   Internal helper for close(): attempts to initiate ws_.async_close(...) once the outbound
  //   write pipeline is fully idle.
  //
  // Key constraint (Boost.Beast):
  //   ws_.async_close(...) must not overlap with an in-flight ws_.async_write(...).
  //   This method enforces that by gating close initiation on queue/in-flight state.
  //
  // Behavior:
  //   - No-op unless close_pending_ is true and close_started_ is false.
  //   - If write_queue_ is empty, write_in_progress_ is false, and writes_in_flight_ == 0:
  //       - Sets close_started_ = true (idempotence)
  //       - Starts ws_.async_close(normal, ...)
  //       - On completion: marks Disconnected, updates metrics, and calls on_closed_() if set.
  //   - Otherwise returns immediately (non-blocking). Callers re-invoke it when the pipeline drains.
  //
  // Threading / ordering:
  //   - Intended to be invoked on strand_ (from close()'s posted handler and from the write completion
  //     path when the queue becomes empty).
  void tentative_close(std::shared_ptr<WsClient> self) {
    if( close_pending_ && !close_started_ ) {
      if( write_queue_.empty() && !write_in_progress_ && (writes_in_flight_.load() != 0))
      {
        close_started_ = true;
        boost::asio::post(strand_, [this, self]{
          self->ws_.async_close(websocket::close_code::normal,
          boost::asio::bind_executor(self->strand_, [this, self](boost::system::error_code){
            state_ = WsClientState::Disconnected;
            std::cout << "WebSocket closed\n";
            metrics_.transport_on_close_event();
            if( on_closed_ ) {
              on_closed_();
            }        
          }));
          return;
        });
      }
    }
  }

  // force_drop(graceful)
  //
  // Test hook: programmatically drop the connection.
  // -graceful=true : perform a normal WebSocket close handshake.
  // -graceful=false: abortively close the underlying TCP socket (simulates network drop).
  //
  // External API: intended for tests/diagnostics only.
  void force_drop(bool graceful = false) {
    auto self = shared_from_this();
    boost::asio::post(strand_, [self, graceful] {
      boost::system::error_code ec;
      if (graceful) {
        self->ws_.async_close(websocket::close_code::normal,
          boost::asio::bind_executor(self->strand_, [](boost::system::error_code){}));
          return;
      }

      // Abortive drop: no WS close handshake.
      auto& sock = beast::get_lowest_layer(self->ws_);
      sock.cancel(ec);
      sock.shutdown(tcp::socket::shutdown_both, ec);
      sock.close(ec);
    });
  }

  // the following methods expose lightweight runtime metrics about the client's outbound write
  // pipeline. There purpose is for diagnostics, debugging and backpressure monitoring.
  //

  // transport_max_writes_in_flight()
  // - Returns the maximum number of concurrent async_write operations observed at any time.
  // - With correct write serialization on strand_, this should stay at 1 (or 0 if no writes
  //   ever started).
  unsigned transport_max_writes_in_flight() const {
    return max_writes_in_flight_.load();
  }

  // transport_max_write_queue_depth()
  // - "queue depth" counts messages that have been enqueued via send() but not yet fully
  //   completed; this includes the message currently being written (if any).
  // - Returns the maximum observed write queue depth (see queue depth definition above).
  // - A value > 0 indicates the producer (send()) got ahead of the socket writer at least
  //   once, i.e., backpressure/queueing was observed under load.
  unsigned transport_max_write_queue_depth() const {
    return max_write_queue_depth_.load();
  }

  // transport_current_write_queue_depth()
  // - "max" values are the maximum observed since this WsClient was created / last reset
  //   (there is currently no explicit reset API).
  // - Returns the current write queue depth: number of enqueued outbound messages that have
  //   not yet completed their write.
  // - Under steady-state / after drains, this should return 0.
  unsigned transport_current_write_queue_depth() const {
    return current_write_queue_depth_.load();
  }

  //for unit tests

  //to use in tests:
  // auto f = client->start_future();
  //ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  //ASSERT_TRUE(f.get());//start ok
  std::future<bool> start_future(){
    auto p =std::make_shared<std::promise<bool>>();
    auto f = p->get_future();
    auto done = std::make_shared<std::atomic<bool>>(false);

    auto prev_connected_ = on_connected_;
    auto prev_closed_ = on_closed_;

    on_connected_ = [prev_connected_, p, done](){
      if(prev_connected_) prev_connected_();
      if(!done->exchange(true)) p->set_value(true);
    };
    on_closed_ = [prev_closed_, p, done](){
      if(prev_closed_) prev_closed_();
      if(!done->exchange(true)) p->set_value(false);
    };
    this->start();
    return f;
  }

  //how to use in unit tests
  //auto cf = client_->close_future();
  //ASSERT_EQ(cf.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  std::future<bool> close_future(){
    auto p = std::make_shared<std::promise<bool>>();
    auto f = p->get_future();
    auto done = std::make_shared<std::atomic<bool>>(false);

    auto prev_closed_ = on_closed_;
    on_closed_ = [prev_closed_, p, done](){
      if(prev_closed_) prev_closed_();
      if(!done->exchange(true)) p->set_value(true);
    };
    this->close();
    return f;
  }

  // what should this method do?
  // void reset() {
  //   ;//
  // }
};

#endif // WS_CLIENT_HPP

//todo
//-see queued up packets at the transport layer are cleared if server closes connection
//-determine argument for clearing these or not.