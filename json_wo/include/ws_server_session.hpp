#include <boost/beast/websocket.hpp>
#include <boost/beast/core.hpp>
#include <boost/asio.hpp>
#include "transport.hpp"
#include "router.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

struct WsServerSession : Transport, std::enable_shared_from_this<WsServerSession> {
  websocket::stream<tcp::socket> ws_;
  beast::flat_buffer buffer_;
  std::function<void(std::string_view)> on_msg_;
  Router router;

  WsServerSession(tcp::socket s) : ws_(std::move(s)) {
    router.addHandler<BootNotification>(BootNotificationHandler);
    router.addHandler<Authorize>(AuthorizeHandler);
    router.addHandler<HeartBeat>(HeartBeatHandler);
  }
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
        return;
      }
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
    ws_.async_write(boost::asio::buffer(*buf), [this,self,buf](auto ec, std::size_t len){
      if (ec) {
        std::cerr << "WebSocket write error: " << ec.message() << "\n";
      }
      else {
        std::cout << "Bytes sent: " << len << "\n";
      }
    });
  }
  void close() override {
    auto self = shared_from_this();
    ws_.async_close(websocket::close_code::normal, [this,self](auto){
        std::cout << "WebSocket closed\n";
    });
  }
};