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
  std::function<void(std::string_view)> on_msg_;
  std::function<void()> on_closed_;
  std::function<void(const Call&, std::function<void(const OcppFrame&)>)> on_call_;
  std::deque<std::shared_ptr<std::string>> write_queue_;
  bool write_in_progress_ = false;

  WsServerSession(tcp::socket s) : ws_(std::move(s)) {
  }
  
  void on_call(std::function<void(const Call&, std::function<void(const OcppFrame&)>)> cb){on_call_ = std::move(cb);}
  void on_close(std::function<void()> cb) { on_closed_ = std::move(cb); }
  void on_message(std::function<void(std::string_view)> cb) override { on_msg_ = std::move(cb); }
  void start() override {
    auto self = shared_from_this();
    ws_.async_accept([this,self](auto ec){
      if (ec) {
        std::cerr << "WebSocket accept error: " << ec.message() << "\n";
        return;
      }
      std::cout<<"WebSocket handshake accepted"<<std::endl;
      do_read();
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

  void close() override {
    auto self = shared_from_this();
    ws_.async_close(websocket::close_code::normal, [this,self](auto){
        std::cout << "WebSocket closed\n";
        if( on_closed_ ) on_closed_();
    });
  }
};