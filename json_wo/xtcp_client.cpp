// tcp_client.cpp
#include <boost/asio.hpp>
#include <iostream>
#include <string>

#include <fstream>
#include <json.hpp>
#include <variant>
#include <random>

#include "ocpp_model.hpp"
#include "session.hpp"
#include "client.hpp"
#include "ws_client.hpp"
using json = nlohmann::json;
using namespace std::string_literals;
using namespace nlohmann::literals;
using boost::asio::ip::tcp;

#if 0
int main() {
    boost::asio::io_context io;//this is the event loop
    Client client(io);
    client.connect_and_boot(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 12345));
    io.run();

    return 0;
}
#endif

int main() {
    boost::asio::io_context io;//this is the event loop
    auto ws = std::make_shared<WsClient>(io, "127.0.0.1", "12345");
    auto ss = std::make_shared<Session>(io, ws);
    std::weak_ptr<Session> wss = ss;
    std::cout<<"Connecting to ws://" << "\n";
    ws->on_close([wss](){
        if( auto ss = wss.lock() ) {
            ss->on_close();
        }
    });

    ws->on_connected([&](){
        ss->send_call(BootNotification{"X100", "OpenAI"},
                  [wss](const OcppFrame& f){
                    if( std::holds_alternative<CallResult>(f) ) {
                        auto r = std::get<CallResult>(f);
                        std::cout << "BootNotificationResponse: "<< r.payload << "\n";
                        BootNotificationResponse resp = r.payload;
                        if (resp.status == "Accepted") {
                            std::cout << "BootNotification accepted, current time: " << resp.currentTime << "\n";
                            if (auto ss = wss.lock()) {
                                ss->state = Session::State::Ready;
                                // start_heartbeat(resp.interval);
                                ss->start_heartbeat(resp.interval);
                            }
                            else {
                                std::cerr << "Session already destroyed, cannot set state to Ready\n";
                            }
                        } else if (resp.status == "Pending") {
                            std::cout << "BootNotification pending, interval: " << resp.interval << "\n";
                        } else {
                            std::cout << "BootNotification rejected\n";
                        }
                    } else if (std::holds_alternative<CallError>(f)){
                        const auto& e = std::get<CallError>(f);
                        std::cerr << "BootNotification Error: " << e.errorDescription << "\n";
                    }
                  });
    });
    ws->on_message([wss](std::string_view sv){ 
        if (auto ss = wss.lock()) {
            ss->on_wire_message(sv);
        }
        else {
            std::cerr << "Session already destroyed, ignoring message\n";
        }
    });
    ws->start();
    io.run();

    return 0;
}