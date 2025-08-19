// tcp_server.cpp
#include <boost/asio.hpp>
#include "router.hpp"
#include "ocpp_model.hpp"
using boost::asio::ip::tcp;

struct ServerSession : std::enable_shared_from_this<ServerSession> {
  tcp::socket sock;
  boost::asio::streambuf buf;
  Router& router;

  ServerSession(tcp::socket s, Router& r) : sock(std::move(s)), router(r) {
    router.addHandler<BootNotification>(BootNotificationHandler);
    router.addHandler<Authorize>(AuthorizeHandler);
  }
  void start() { do_read(); }

  void do_read() {
    auto self = shared_from_this();
    boost::asio::async_read_until(sock, buf, '\n',
      [this,self](boost::system::error_code ec, std::size_t len){
        if (ec){
            std::cerr << "Read failed: " << ec.message() << "\n";
            return; // TODO: how to close the session?
        }
        std::istream is(&buf);
        std::string line; std::getline(is, line);
        json j = json::parse(line);
        OcppFrame frame = parse_frame(j);
        if( std::holds_alternative<Call>(frame) ) {
            auto call = std::get<Call>(frame);
            std::cout << "Received Call: " << call.action << "\n";
            OcppFrame reply = router.route(call);
            json j_r = reply;
            std::string out = j_r.dump() + "\n";
            do_write(out);
        }
        else {
            std::cerr << "Received unexpected frame type\n";
            return;
        }
      });
  }

  void do_write(std::string msg) {
    auto self = shared_from_this();
    boost::asio::async_write(sock, boost::asio::buffer(msg),
      [this,self](boost::system::error_code ec, std::size_t len){
        if (ec){
            std::cerr << "["<< __func__ <<"]" << "Write failed: " << ec.message() << "\n";
        }
        do_read();
      });
  }
};

// int main() {
//   boost::asio::io_context io;
//   Router router; // TODO: register handlers
//   tcp::acceptor acc(io, {tcp::v4(), 12345});
//   std::function<void()> do_accept;
//   do_accept = [&]{
//     acc.async_accept([&](boost::system::error_code ec, tcp::socket s){
//       if (!ec) std::make_shared<ServerSession>(std::move(s), router)->start();
//       do_accept();
//     });
//   };
//   do_accept();
//   io.run();
// }
