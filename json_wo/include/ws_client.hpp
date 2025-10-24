#include <boost/beast/websocket.hpp>
#include <boost/beast/core.hpp>
#include <boost/asio.hpp>
#include <deque>
#include "transport.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

struct WsClient : Transport, std::enable_shared_from_this<WsClient> {
  tcp::resolver res_;
  websocket::stream<tcp::socket> ws_;
  // boost::asio::strand<boost::asio::io_context::executor_type> strand_{ws_.get_executor()};
  boost::asio::strand<websocket::stream<tcp::socket>::executor_type> strand_;
  beast::flat_buffer buffer_;
  std::function<void(std::string_view)> on_msg_;
  std::function<void()> on_connected_;
  std::function<void()> on_closed_;
  std::string host_, port_;
  std::deque<std::shared_ptr<std::string>> write_queue_;
  bool write_in_progress_ = false;
  std::atomic<unsigned> writes_in_flight_{0}, max_writes_in_flight_{0};
  std::atomic<unsigned> max_write_queue_depth_{0}, current_write_queue_depth_{0};


  WsClient(boost::asio::io_context& io, std::string host, std::string port)
    : res_(io),
      ws_(io),
      host_(std::move(host)),
      port_(std::move(port)),
      strand_(ws_.get_executor()) {}

  void on_message(std::function<void(std::string_view)> cb) override { on_msg_ = std::move(cb); }
  void on_connected(std::function<void()> cb) { on_connected_ = std::move(cb); }
  void on_close(std::function<void()> cb) { on_closed_ = std::move(cb); }

  unsigned transport_max_writes_in_flight() const {
    return max_writes_in_flight_.load();
  }

  unsigned transport_max_write_queue_depth() const {
    return max_write_queue_depth_.load();
  }

  unsigned transport_current_write_queue_depth() const {
    return current_write_queue_depth_.load();
  }

  //connect to server(via tcp and upgrade to ws). start reading the connection
  void start() override {
    auto self = shared_from_this();
    res_.async_resolve(host_, port_, [this,self](auto ec, auto results){
      if (ec) {
        std::cerr << "WebSocket resolve error: " << ec.message() << "\n";
        return;
      }

      std::cout << "Resolved.\n";

      boost::asio::async_connect(ws_.next_layer(), results, [this,self](auto ec, auto){
        if (ec) {
          std::cerr << "WebSocket connect error: " << ec.message() << "\n";
          return;
        }

        std::cout<<"Connected.\n";

        ws_.async_handshake(host_, "/", [this,self](auto ec){
          if (ec) {
            std::cerr << "WebSocket handshake error: " << ec.message() << "\n";
            return;
          }
          
          std::cout << "Handshake complete!\n";

          ws_.text(true);
          
          //connect and boot
        //   send("BootNotification or your initial message");
          if( on_connected_ ) on_connected_();

          do_read();
        });
      });
    });
  }

  void do_read() {
    auto self = shared_from_this();
    ws_.async_read(buffer_, [this,self](auto ec, std::size_t){
      if (ec) {
        std::cerr << "WebSocket read error: " << ec.message() << "\n";
        
        if( on_closed_ ) on_closed_();
        
        return;
      }
      std::string text = beast::buffers_to_string(buffer_.data());
      buffer_.consume(buffer_.size());
      if (on_msg_) on_msg_(text);
      do_read();
    });
  }

  void send(std::string text) override {
    auto buf = std::make_shared<std::string>(std::move(text));
    auto self = shared_from_this();
    boost::asio::post(boost::asio::bind_executor(strand_, [this, self, buf]() {
        if( write_queue_.size() > 1000 ) {
            std::cerr << "Write queue overflow, dropping message\n";
            return;
        }
        write_queue_.push_back(buf);
        // writes_in_flight_++; 
        current_write_queue_depth_++;
        max_write_queue_depth_ = std::max(max_write_queue_depth_.load(), current_write_queue_depth_.load());
        // max_writes_in_flight_ = std::max(max_writes_in_flight_.load(), writes_in_flight_.load());
        if(!write_in_progress_) {
            do_write();
        }
    }));

  }

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
    ws_.async_write(boost::asio::buffer(buf), boost::asio::bind_executor(strand_,[this,self](auto ec, std::size_t len){
        if (ec) {
            std::cerr << "WebSocket write error: " << ec.message() << "\n";
        }
        write_queue_.pop_front();

        writes_in_flight_--; current_write_queue_depth_--;
        max_writes_in_flight_ = std::max(max_writes_in_flight_.load(), writes_in_flight_.load());

        if( !write_queue_.empty()){
            do_write(); //initiate next write
        }
        else {
            write_in_progress_ = false;
        }
    }));
  }

  void close() override {
    auto self = shared_from_this();
    ws_.async_close(websocket::close_code::normal, [this,self](auto){
        std::cout << "WebSocket closed\n";
        if( on_closed_ ) on_closed_();
    });
  }
};