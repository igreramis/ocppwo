// tcp_server.cpp
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include "ocpp_model.hpp"
#include "router.hpp"
#include "server.hpp"
#include "ws_server_session.hpp"

using boost::asio::ip::tcp;

#if 1
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

    io.run();
}
#endif
