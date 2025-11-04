#include <gtest/gtest.h>
#include <chrono>
#include "test_server.hpp"
#include "reconnect.hpp"
#include "ws_client.hpp"
#include "session.hpp"
#include "reconnect_glue.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;
using namespace std;

struct ClientUnderTest{
    //provide a way to start the client pointing to ws:127.0.0.1:port
    //and a way to stop it. This structd is a placeholder.
    std::shared_ptr<WsClient> client_;
    std::shared_ptr<Session> ss_;
    std::shared_ptr<ReconnectGlue> rcg_;
    std::weak_ptr<Session> wss_;
    unsigned transport_max_writes_in_flight() const {
        return client_->transport_max_writes_in_flight();
    }

    unsigned transport_max_write_queue_depth() const {
        return client_->transport_max_write_queue_depth();
    }

    unsigned transport_current_write_queue_depth() const {
        return client_->transport_current_write_queue_depth();
    }

    void start(boost::asio::io_context& io, std::string host, std::string port)
    {
        client_ = std::make_shared<WsClient>(io, host, port);
        ss_ = std::make_shared<Session>(io, client_);
        // rcg_ = std::make_shared<ReconnectGlue>(client_, ss_);
        wss_= ss_;

        GTEST_LOG_(INFO) << "WebSocket client starting...";
        
        client_->on_close([this, host, port](){
            if( auto ss = wss_.lock() ) {
                ss->on_close();
            }
            // rcg_->rc.start(host+port);
            //rc->start(host+port);
            //update rc state to disconnectd
            //update rc error as per reason extracted out of the client
            //through some magical way, rc is able to take next action steps
            //based on the change in state and the error condition
        });
        
        client_->on_connected([this](){
            GTEST_LOG_(INFO) << "WebSocket client connected...";
            ss_->send_call(BootNotification{"X100", "OpenAI"},
                    [this](const OcppFrame& f){
                        if( std::holds_alternative<CallResult>(f) ) {
                            auto r = std::get<CallResult>(f);
                            // std::cout << "BootNotificationResponse: "<< r.payload << "\n";
                            GTEST_LOG_(INFO) << "BootNotificationResponse: "<< r.payload << "\n";
                            BootNotificationResponse resp = r.payload;
                            if (resp.status == "Accepted") {
                                std::cout << "BootNotification accepted, current time: " << resp.currentTime << "\n";
                                if (auto ss = wss_.lock()) {
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

        ss_->start();
    };

    void reconnect(boost::asio::io_context& io, std::string host, std::string port)
    {
        client_ = std::make_shared<WsClient>(io, host, port);
        ss_ = std::make_shared<Session>(io, client_);
        rcg_ = std::make_shared<ReconnectGlue>(client_, ss_);
        rcg_->rc.start(host+port);
    }

    void stop()
    {
        client_->close();
    };
};

TEST(Reconnect, TypesCompile){
    ReconnectPolicy pol;
    ConnState s = ConnState::Disconnected;
    CloseReason r = CloseReason::Clean;
    ReconnectSignals sigs;

}

TEST(Reconnect, OpsAreAssignable){
    TransportOps ops;
    ops.async_connect = [&](const std::string& url, std::function<void(bool ok)> done){
        ;
    };

    ops.async_close = [&](std::function<void()> closed){
        ;
    };

    ops.post_after = [&](std::chrono::milliseconds d, std::function<void()> cb){
        ;
    };

    ops.now = [](){
        return std::chrono::steady_clock::now();
    };
}

TEST(Reconnect, StartLeadsToConnecting){
    using namespace std::chrono_literals;
    boost::asio::io_context ioc;

    //Pick an ephemeral port
    unsigned short port = 0;
    {
        tcp::acceptor tmp(ioc, {tcp::v4(), 0});
        port = tmp.local_endpoint().port();
    }

    TestServer server(ioc, port);
    server.set_boot_conf("boot_msg_id", /*heartbeat time in s*/ 2);
    server.start();

    ClientUnderTest cut;
    cut.start(ioc, "127.0.0.1", std::to_string(port));
    // Rcg.rc.start();

    std::thread io_thread([&]{ ioc.run(); });

    // ---- Wait for BootNotification to arrive at CSMS ----
    std::string boot_id;
    for( int i = 0; i < 50; i++ ) { // upto ~5s(50*100ms)
        std::this_thread::sleep_for(100ms);
        boot_id = server.last_boot_msg_id();
        if( !boot_id.empty() ) break;
    }
    ASSERT_FALSE(boot_id.empty()) << "Client never sent BootNotification";

    // server.stop();
    // std::this_thread::sleep_for(5s);
    // EXPECT_TRUE(server.is_client_disconnected());
    
    // Pick an ephemeral port
    {
        tcp::acceptor tmp(ioc, {tcp::v4(), 0});
        port = tmp.local_endpoint().port();
    }

    // cut.reconnect(ioc, "127.0.0.1", std::to_string(port));
    //Cleanup
    server.stop();
    std::this_thread::sleep_for(5s);
    cut.stop();
    ioc.stop();
    if(io_thread.joinable() ) io_thread.join();
}