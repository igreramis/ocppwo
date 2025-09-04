// tcp_server.cpp
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include "ocpp_model.hpp"
#include "router.hpp"
#include "server.hpp"
#include "ws_server_session.hpp"

using boost::asio::ip::tcp;

int main() {
    boost::asio::io_context io;
    tcp::acceptor acc(io, {tcp::v4(), 12345});

    std::cout << "Server listening on port 12345...\n";

    std::function<void()> do_accept;
    do_accept = [&]{
        acc.async_accept([&](boost::system::error_code ec, tcp::socket s){
        if (!ec) {
            std::cout << "New client connected from " << s.remote_endpoint() << "\n";
            auto ss = std::make_shared<WsServerSession>(std::move(s));
            ss->on_message([ss](std::string_view msg){
                std::cout << "Rx<--- " << msg << "\n";
                std::string s(msg);
                json j = json::parse(s);
                OcppFrame f = parse_frame(j);
                if( std::holds_alternative<Call>(f) ) {
                    auto call = std::get<Call>(f);
                    std::cout << __func__ << "Received Call: " << call.action << "\n";
                    OcppFrame f = ss->router.route(call);
                    std::cout<< "After router.route(call)" << "\n";
                    json jr = f;
                    std::string sr = jr.dump();
                    ss->send(sr);
                }
            });
            ss->start();
        }
        do_accept();
        });
    };
    do_accept();

    // auto ws = std::make_shared<WsServerSession>(tcp::socket(io));
    // ws->on_message([](std::string_view sv){
    //     std::cout << "RX<---- " << sv << "\n";
    // });
    // ws->start();
    io.run();
}

#if 0
int main() {
  boost::asio::io_context io;
  Router router;
  router.addHandler<BootNotification>(BootNotificationHandler);
  router.addHandler<Authorize>(AuthorizeHandler);
  router.addHandler<HeartBeat>(HeartBeatHandler);
  tcp::acceptor acc(io, {tcp::v4(), 12345});

  std::cout << "Server listening on port 12345...\n";

  std::function<void()> do_accept;
  do_accept = [&]{
    acc.async_accept([&](boost::system::error_code ec, tcp::socket s){
      if (!ec) std::make_shared<ServerSession>(std::move(s), router)->start();
      do_accept();
    });
  };
  do_accept();
  io.run();
}
#endif
