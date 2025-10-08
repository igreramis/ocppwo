#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <chrono>
#include <thread>
#include <string>
#include "test_server.hpp"
#include "ws_client.hpp"
#include "session.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;
using namespace std;

struct ClientUnderTest{
    //provide a way to start the client pointing to ws:127.0.0.1:port
    //and a way to stop it. This structd is a placeholder.
    std::shared_ptr<WsClient> client_;
    std::shared_ptr<Session> ss_;
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

TEST(SmokeTest, Timeout_NoServerReply_LocalTimeoutError) {
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
    
    // put in logic for sending UnknownType message and checking for CallError response
    //  bool received_timeout = false;
    std::atomic<bool> received_timeout{false};
    std::promise<void> p;
    auto f = p.get_future();
    if (auto ss = cut.wss_.lock())
    {
        ss->send_call(Authorize{"TIMEOUT_TEST"}, [&received_timeout, &p](const OcppFrame &f)
                      {
                          if (std::holds_alternative<CallError>(f))
                          {
                              auto ce = std::get<CallError>(f);
                              p.set_value();
                          }
                          else
                          {
                              FAIL() << "Expected local Timeout response but got Server reply";
                          } }, std::chrono::seconds(10));
    }

    auto status = f.wait_for(std::chrono::seconds(11));
    ASSERT_EQ(status, std::future_status::ready) << "Did not receive expected Timeout response";


    //Cleanup
    cut.stop();
    server.stop();
    ioc.stop();
    if(io_thread.joinable() ) io_thread.join();
}

TEST(SmokeTest, BackPressure_ThousandSmallFrames_SerializedWrites) {
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
    
    int N = 1000;
    for( int i = 0; i < N; i++ ) {
        json frame = json::array({2, std::to_string(i), "Ping", json::object({{"seq", i }})});
        cut.client_->send(frame.dump());
    }

    std::this_thread::sleep_for(3s);

    EXPECT_LE(cut.transport_max_writes_in_flight(), 1u) << "Transprot allowed concurrent writes; must serialize writes.";

    EXPECT_GT(cut.transport_max_write_queue_depth(), 0u) << "Never observed queue growth; test may not have stressed the writer.";

    EXPECT_EQ(cut.transport_current_write_queue_depth(), 0u) << "Never observed current queue depth; test may not have stressed the writer.";

    auto frames = server.received();
    std::vector<int> seqs;

    for( auto &frame : frames )
    {
        //how to convert string to json
        auto a = json::parse(frame.text);
        if( a.is_array() && a.size() > 3 && a[2] == "Ping" ) {
            seqs.push_back(a[3].value("seq", -1));
        }
    }
    ASSERT_EQ(seqs.size(), N) << "Server did not receive all Ping messages";

    bool monotonic = false;
    for( int i=1; i < seqs.size(); i++ ){
        if( seqs[i] != seqs[i-1] + 1 ) {
            monotonic = false;
            break;
        }
    }
    ASSERT_FALSE(monotonic) << "Ping messages received out of order";

    //Cleanup
    cut.stop();
    server.stop();
    ioc.stop();
    if(io_thread.joinable() ) io_thread.join();
}

TEST(SmokeTest, CloseFlow_ServerClose_ResovlesPendings) {
    using namespace std::chrono_literals;
    boost::asio::io_context ioc;

    //Pick an ephemeral port
    unsigned short port = 0;
    {
        tcp::acceptor tmp(ioc, {tcp::v4(), 0});
        port = tmp.local_endpoint().port();
    }

    TestServer server(ioc, port);
    server.set_boot_conf("boot_msg_id", 10);
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
    
    json payload = json::object({{"idTag", "PENDCLOSE"}});
    json frame = json::array({2, "idTag1234", "Authorize", json::object({{"idTag", "PENDCLOSE"}})});
    auto sp = std::make_shared<std::promise<void>>();
    auto fu = sp->get_future();
    cut.ss_->send_call(Authorize{"PENDCLOSE"}, [sp](const OcppFrame& f) mutable{
        if( std::holds_alternative<CallResult>(f) ) {
            std::cout<<"Authorize response received in test\n";
            sp->set_value();
        }
    }, std::chrono::seconds(3));

    server.stop();

    auto status = fu.wait_for(std::chrono::seconds(4));
    EXPECT_FALSE(status == std::future_status::ready);
    EXPECT_TRUE(server.is_client_disconnected());


    cut.ss_->send_call(HeartBeat{}, [sp](const OcppFrame& f) mutable{
        if( std::holds_alternative<CallError>(f) ) {
            sp->set_value();
        }
    }, std::chrono::seconds(2));
    status = fu.wait_for(std::chrono::seconds(3));
    EXPECT_TRUE(status == std::future_status::ready);

    //Cleanup
    cut.stop();
    ioc.stop();
    if(io_thread.joinable() ) io_thread.join();
}