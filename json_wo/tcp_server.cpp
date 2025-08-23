// tcp_server.cpp
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include "ocpp_model.hpp"
#include "router.hpp"
#include "server.hpp"
using boost::asio::ip::tcp;

OcppFrame _handle_call(const Call &c)
{
    if( c.action == "BootNotification" )
    {
        BootNotification boot = c.payload;
        BootNotificationResponse res = {
            "2025-07-16T12:00:00Z",
            300,
            "Accepted"
        };
        return CallResult {
            3,
            c.messageId,
            res// c.payload//res
        };
    }
    else if( c.action == "Authorize" )
    {
        Authorize authorize = c.payload;
        AuthorizeResponse res = {
            "ID Tag Authorized"
        };
        return CallResult {
            3,
            c.messageId,
            res
        };
    }
    else
    {
        ;
        std::string error_detail = R"([
            Error Detail...
        ])";
        return CallError {
            3,
            "CallError",
            "404",
            "Command not found",
            json::parse(error_detail)
        };
    }
}

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
