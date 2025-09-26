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
    Router router;
    router.addHandler<BootNotification>(BootNotificationHandler);
    router.addHandler<Authorize>(AuthorizeHandler);
    router.addHandler<HeartBeat>(HeartBeatHandler);

    std::cout << "Server listening on port 12345...\n";

    std::function<void()> do_accept;
    do_accept = [&]{
        acc.async_accept([&](boost::system::error_code ec, tcp::socket s){
        if (!ec) {
            std::cout << "New client connected from " << s.remote_endpoint() << "\n";
            auto ss = std::make_shared<WsServerSession>(std::move(s));

            ss->on_call([&router](const Call &c, std::function<void(const OcppFrame& f)> cb){
                    std::cout << __func__ << "Received Call: " << c.action << "\n";
                    OcppFrame f = router.route(c);
                    cb(f);
            });
            ss->on_message([ss, &router](std::string_view msg){
                std::cout << "Rx<--- " << msg << "\n";
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
