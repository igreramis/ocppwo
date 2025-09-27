#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <chrono>
#include <thread>
#include "test_server.hpp"
#include "ws_client.hpp"
#include "session.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

struct ClientUnderTest{
    //provide a way to start the client pointing to ws:127.0.0.1:port
    //and a way to stop it. This structd is a placeholder.
    std::shared_ptr<WsClient> client_;
    std::shared_ptr<Session> ss_;
    std::weak_ptr<Session> wss_;
    void start(boost::asio::io_context& io, std::string host, std::string port)
    {
        client_ = std::make_shared<WsClient>(io, host, port);
        ss_ = std::make_shared<Session>(io, client_);
        wss_= ss_;

        GTEST_LOG_(INFO) << "WebSocket client starting...";
        
        client_->on_close([this](){
            if( auto ss = wss_.lock() ) {
                ss->on_close();
            }
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
    void stop()
    {
        client_->close();
    };
};

//this test runs in one thread. that is for gtest. to run io operations, 
//we need to spawn another thread.
//we create instances such that they are created on one thread, but they
//run their operations on another thread. that is totally allowed.
//but for varibles (and therefore memory) that ends up being shared by the two
//threads because of this arrangement, we need to use mutexex to protect them.
TEST(HappyPath, BootAcceptedThenHeartbeat) {
    using namespace std::chrono_literals;
    boost::asio::io_context ioc;

    //Pick an ephemeral port
    unsigned short port = 0;
    {
        tcp::acceptor tmp(ioc, {tcp::v4(), 0});
        port = tmp.local_endpoint().port();
    }

    TestServer server(ioc, port);
    server.set_boot_conf("boot_msg_id", 2);
    server.start();

    ClientUnderTest cut;
    cut.start(ioc, "127.0.0.1", std::to_string(port));

    std::thread io_thread([&]{ ioc.run(); });

    // io_thread.join();

    // ---- Wait for BootNotification to arrive at CSMS ----
    std::string boot_id;
    for( int i = 0; i < 50; i++ ) { // upto ~5s(50*100ms)
        std::this_thread::sleep_for(100ms);
        boot_id = server.last_boot_msg_id();
        if( !boot_id.empty() ) break;
    }
    ASSERT_FALSE(boot_id.empty()) << "Client never sent BootNotification";
    //assert <if not> false

    // ---- Observe atleast two heartbeats at ~2s cadence ----
    std::this_thread::sleep_for(5s);

    auto beats = server.heartbeats();
    ASSERT_GE(beats.size(), 2) << "Expected >= 2 heartbeats after BootAccepted";

    //check the cadence(very loose tolerance)
    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(beats[1].t - beats[0].t);
    EXPECT_NEAR(static_cast<double>(dt.count()), 2000.0, 600.0) << "Heartbeat cadence not ~2s";

    //Cleanup
    cut.stop();
    server.stop();
    ioc.stop();
    if(io_thread.joinable() ) io_thread.join();
}

TEST(SmokeTest, UnknownActionReturnsCallError) {
    using namespace std::chrono_literals;
    boost::asio::io_context ioc;

    //Pick an ephemeral port
    unsigned short port = 0;
    {
        tcp::acceptor tmp(ioc, {tcp::v4(), 0});
        port = tmp.local_endpoint().port();
    }

    TestServer server(ioc, port);
    server.set_boot_conf("boot_msg_id", 2);
    server.start();

    ClientUnderTest cut;
    cut.start(ioc, "127.0.0.1", std::to_string(port));

    std::thread io_thread([&]{ ioc.run(); });

    // ---- Wait for BootNotification to arrive at CSMS ----
    std::string boot_id;
    for( int i = 0; i < 50; i++ ) { // upto ~5s(50*100ms)
        std::this_thread::sleep_for(100ms);
        boot_id = server.last_boot_msg_id();
        if( !boot_id.empty() ) break;
    }
    ASSERT_FALSE(boot_id.empty()) << "Client never sent BootNotification";
    
    auto beats_before = server.heartbeats();
    
    //put in logic for sending UnknownType message and checking for CallError response
    if( auto ss = cut.wss_.lock() ) {
        ss->send_call(UnknownAction{},
                      [](const OcppFrame& f) {
                          if( std::holds_alternative<CallError>(f) ) {
                              auto ce = std::get<CallError>(f);
                              ASSERT_EQ(ce.errorCode, "NotImplemented");
                            }
                            else {
                                FAIL() << "Expected CallError response for UnknownAction";
                            }
                        });
                    }
                    
    std::this_thread::sleep_for(5s);
    
    auto beats_after = server.heartbeats();

    ASSERT_GT(beats_after.size(), beats_before.size() ) << "Heartbeats have unexpectedly stopped";

    //Cleanup
    cut.stop();
    server.stop();
    ioc.stop();
    if(io_thread.joinable() ) io_thread.join();
}