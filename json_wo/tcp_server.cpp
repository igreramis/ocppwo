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

// int main() {
//     boost::asio::io_context io;
//     tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 12345));

//     std::cout << "Server listening on port 12345...\n";
//     tcp::socket socket(io);
//     acceptor.accept(socket);

//     boost::asio::streambuf buf;
//     boost::asio::read_until(socket, buf, '\n');

//     std::istream input(&buf);
//     std::string message;
//     std::getline(input, message);

//     std::cout << "[Server] Received: " << message << "\n";

//     json msg_json = json::parse(message);
//     OcppFrame frame = parse_frame(msg_json);

// #if 1
//     Router router;
//     router.addHandler<BootNotification>(BootNotificationHandler);
//     router.addHandler<Authorize>(AuthorizeHandler);
//     if( std::holds_alternative<Call>(frame) )
//     {
//         OcppFrame f = router.route(std::get<Call>(frame));
//         json j;
//         if( std::holds_alternative<CallResult>(f) )
//         {
//             j = std::get<CallResult>(f);
//         }
//         else if( std::holds_alternative<CallError>(f) )
//         {
//             j = std::get<CallError>(f);
//         }
//         else
//         {
//             throw std::runtime_error("Unknown OcppFrame type");
//         }
//         message = j.dump();
//     }
// #endif
//     // std::cout << "[Server] Replying: " << message << "\n";
//     std::cout << "[Server] Replying: " << message << "\n";
//     boost::asio::write(socket, boost::asio::buffer(message + "\n"));

//     return 0;
// }

// inside do_read() after getline:
    // try {
    //     auto j = json::parse(line);
    //     OcppFrame frame = j.get<OcppFrame>(); // assuming from_json overloads

    //     std::string out;

    //     if (std::holds_alternative<Call>(frame)) {
    //         const auto& call = std::get<Call>(frame);
    //         OcppFrame reply = router.route(call);
    //         out = json(reply).dump() + "\n";
    //     } else {
    //         // If a client sends CallResult/CallError to server, decide your behavior
    //         out = "{\"warning\":\"unexpected frame\"}\n";
    //     }

    //     do_write(std::move(out));
    // } catch (const std::exception& e) {
    //     do_write(std::string{"{\"error\":\"parse failure: "} + e.what() + "\"}\n");
    // }
