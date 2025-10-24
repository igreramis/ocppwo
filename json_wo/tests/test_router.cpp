#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "router.hpp"
#include "ocpp_model.hpp"
#include <future>
#include <boost/asio.hpp>
#include <chrono>
#include <thread>
#include <string>
#include "ws_client.hpp"
#include "test_server.hpp"
#include "session.hpp"
// #include <catch2/catch.hpp>

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

        // GTEST_LOG_(INFO) << "WebSocket client starting...";
        
        client_->on_close([this](){
            if( auto ss = wss_.lock() ) {
                ss->on_close();
            }
        });
        
        client_->on_connected([this](){
            // GTEST_LOG_(INFO) << "WebSocket client connected...";
            ss_->send_call(BootNotification{"X100", "OpenAI"},
                    [this](const OcppFrame& f){
                        if( std::holds_alternative<CallResult>(f) ) {
                            auto r = std::get<CallResult>(f);
                            // std::cout << "BootNotificationResponse: "<< r.payload << "\n";
                            // GTEST_LOG_(INFO) << "BootNotificationResponse: "<< r.payload << "\n";
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

TEST_CASE("Router dispatches to correct handler") {
    Router router;
    bool bootHandlerCalled = false;
    bool authHandlerCalled = false;

    auto bootHandler = [&](const BootNotification& b, const std::string& msgId) -> OcppFrame {
        bootHandlerCalled = true;
        BootNotificationResponse res = {
            "2025-07-16T12:00:00Z",
            300,
            "Accepted"
        };
        return CallResult {
            3,
            msgId,
            res
        };
    };

    auto bootHandler_v2 = [&](const BootNotification &b)->tl::expected<BootNotificationResponse, std::string>{
        bootHandlerCalled = true;
        authHandlerCalled = false;

        return BootNotificationResponse {
            "2025-07-16T12:00:00Z",
            10,
            "Accepted"
        };
    };

    auto authHandler = [&](const Authorize& a, const std::string& msgId) -> OcppFrame {
        authHandlerCalled = true;
        AuthorizeResponse res = {
            a.idTag + "OK",
        };
        return CallResult{
            3,
            msgId,
            res
        };
    };

    auto authHandler_v2 = [&](const Authorize &a)->tl::expected<AuthorizeResponse, std::string>{
        authHandlerCalled = true;
        return AuthorizeResponse{
            a.idTag+"OK"
        };
    };

    // router.registerHandler("BootNotification", bootHandler);
    // router.registerHandler("Authorize", authHandler);
    router.addHandler<BootNotification>(bootHandler);
    router.register_handler<OcppActionName<BootNotification>, BootNotification, BootNotificationResponse>(bootHandler_v2);
    router.register_handler<OcppActionName<Authorize>, Authorize, AuthorizeResponse>(authHandler_v2);
    router.addHandler<Authorize>(authHandler);

    SECTION("BootNotification dispatch") {
        Call call;
        call.action = "BootNotification";
        call.payload = nlohmann::json(BootNotification{"X100", "OpenAI"});
        OcppFrame frame = router.route(call);
        REQUIRE(bootHandlerCalled);
        REQUIRE_FALSE(authHandlerCalled);
        REQUIRE(std::holds_alternative<CallResult>(frame));
    }

    SECTION("BootNotification dispatch v2") {
        // Call call;
        // call.action = "BootNotification";
        // call.payload = nlohmann::json(BootNotification{"X100", "OpenAI"});
        // OcppFrame frame = router.route(call);
        std::string reply_;
        auto done = std::make_shared<std::promise<void>>();
        auto fut = done->get_future();

        std::string str = R"([2, "abc", "BootNotification", {"chargePointModel":"X","chargePointVendor":"Y"}])";
        
        auto j = nlohmann::json::parse(str);

        auto send_count = std::make_shared<std::atomic_int>(0);
        try{
            router.handle_incoming(str, [&reply_, done, send_count](std::string&& reply){
                int n = send_count->fetch_add(1, std::memory_order_relaxed) + 1;
                std::cout << "send callback called #" << n
                          << " reply=" << reply
                          << " done.ptr=" << done.get()
                          << " use_count=" << done.use_count()
                          << std::endl;
                reply_ = reply;
                std::cout<<"reply_:"<<reply_<<std::endl;
                if( n== 1) {
                    try {
                        done->set_value();
                    } catch (const std::future_error& e) {
                        std::cerr << "promise set_value() failed: " << e.what() << std::endl;
                    } catch(const std::exception &e){
                        std::cerr << "stdlib threw on promise set_value() threw: "<<e.what()<<std::endl;
                    }

                }
                else {
                    std::cerr << "send called multiple times: " << n << std::endl;
                }
                // done->set_value();
            });
        }catch(const std::exception &e){
            FAIL("Exception during handle_incoming: " << e.what());
        } catch(...){
            FAIL("Unknown exception during handle_incoming");
        }
        auto status = fut.wait_for(std::chrono::seconds(10));
        std::cout<<"after wait_for"<<std::endl;
        REQUIRE(status == std::future_status::ready);
        REQUIRE(bootHandlerCalled);
        REQUIRE_FALSE(authHandlerCalled);
        REQUIRE(reply_ == R"([3,"abc",{"currentTime":"2025-07-16T12:00:00Z","interval":10,"status":"Accepted"}])");
    }

    SECTION("Authorize dispatch") {
        Call call;
        call.action = "Authorize";
        call.payload = json(Authorize{"test_id"});
        OcppFrame frame = router.route(call);
        REQUIRE(authHandlerCalled);
        REQUIRE_FALSE(bootHandlerCalled);
        REQUIRE(std::holds_alternative<CallResult>(frame));
    }

    SECTION("Authorize dispatch v2") {
        std::string reply_;
        auto done = std::make_shared<std::promise<void>>();
        auto fut = done->get_future();

        std::string str = R"([2, "abc", "Authorize", {"idTag": "AuthorizeTag"}])";
        try{
            router.handle_incoming(str, [&reply_, done](std::string&& reply){
                reply_ = reply;
                try{
                    done->set_value();
                } catch(const std::exception &e){
                    std::cerr << "stdlib threw on promise set_value() threw: "<<e.what()<<std::endl;
                }
            });
        }catch(const std::exception &e){
            FAIL("Exception during handle_incoming: " << e.what());
        } catch(...){
            FAIL("Unknown exception during handle_incoming");
        }
        auto status= fut.wait_for(std::chrono::seconds(1));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(authHandlerCalled);
        REQUIRE_FALSE(bootHandlerCalled);
        REQUIRE(reply_ == R"([3,"abc",{"idTagInfo":"AuthorizeTagOK"}])");
    }

    SECTION("Unknown action dispatch") {
        Call call;
        call.action = "UnknownAction";
        OcppFrame frame = router.route(call);
        REQUIRE_FALSE(bootHandlerCalled);
        REQUIRE_FALSE(authHandlerCalled);
        REQUIRE(std::holds_alternative<CallError>(frame));
    }

    SECTION("Unknown action dispatch v2") {
        std::string reply_;
        auto done = std::make_shared<std::promise<void>>();
        auto fut = done->get_future();

        std::string frame = R"([2, "abc", "UnknownAction", {}])";
        
        router.handle_incoming(frame, [&reply_, done](std::string&& reply){
            reply_ = reply;
            try{
                done->set_value();
            }catch( std::exception &e){
                std::cerr << "stdlib threw on promise set_value() threw : " << e.what() << std::endl;
            }
        });
        REQUIRE_FALSE(bootHandlerCalled);
        REQUIRE_FALSE(authHandlerCalled);
        REQUIRE(reply_ == R"([4,"abc","NotImplemented","Unknown action: UnknownAction",{}])");
    }

    SECTION("Unknown action dispatch") {
        Call call;
        call.action = "UnknownAction";
        OcppFrame frame = router.route(call);
        REQUIRE_FALSE(bootHandlerCalled);
        REQUIRE_FALSE(authHandlerCalled);
        REQUIRE(std::holds_alternative<CallError>(frame));
    }

    SECTION("Unknown action dispatch v2") {
        std::string reply_;
        auto done = std::make_shared<std::promise<void>>();
        auto fut = done->get_future();

        std::string frame = R"([2, "abc", "UnknownAction", {}])";

        router.handle_incoming(frame, [&reply_, done](std::string&& reply){
            reply_ = reply;
            try{
                done->set_value();
            }catch( std::exception &e){
                std::cerr << "stdlib threw on promise set_value() threw : " << e.what() << std::endl;
            }
        });
        REQUIRE_FALSE(bootHandlerCalled);
        REQUIRE_FALSE(authHandlerCalled);
        REQUIRE(reply_ == R"([4,"abc","NotImplemented","Unknown action: UnknownAction",{}])");
    }

    SECTION("Malformed payload"){
        std::string reply_;
        auto done = std::make_shared<std::promise<void>>();
        auto fut = done->get_future();

        std::string str = R"([2, "abc", "BootNotification", {"chargePointVendor":123}])";
        try{
            router.handle_incoming(str, [&reply_, done](std::string&& reply){
                reply_ = reply;
                try{
                    done->set_value();
                }catch( std::exception &e ){
                    std::cerr << "stdlib threw on promise set_value(): " << e.what()<<std::endl;
                }
            });
        }catch( std::exception &e ){
            FAIL("Exception during handle_incoming: " << e.what());
        }catch( ... ){
            FAIL("Unknown exception during handle_incoming");
        }

        REQUIRE_FALSE(bootHandlerCalled);
        REQUIRE_FALSE(authHandlerCalled);
        REQUIRE(reply_ == R"([4,"abc","FormationViolation","[json.exception.out_of_range.403] key 'chargePointModel' not found",{}])");
    }

    SECTION("Handler throws InternalError"){
        std::string reply_;
        auto done = std::make_shared<std::promise<void>>();
        auto fut = done->get_future();

        std::string str = R"([2, "abc", "BootNotification", {"chargePointVendor":123}])";
        try{
            router.handle_incoming(str, [&reply_, done](std::string&& reply){
                reply_ = reply;
                try{
                    done->set_value();
                }catch( std::exception &e ){
                    std::cerr << "stdlib threw on promise set_value(): " << e.what()<<std::endl;
                }
            });
        }catch( std::exception &e ){
            FAIL("Exception during handle_incoming: " << e.what());
        }catch( ... ){
            FAIL("Unknown exception during handle_incoming");
        }

        REQUIRE_FALSE(bootHandlerCalled);
        REQUIRE_FALSE(authHandlerCalled);
        REQUIRE(reply_ == R"([4,"abc","FormationViolation","[json.exception.out_of_range.403] key 'chargePointModel' not found",{}])");
    }

    SECTION("Back-to-Back dispatches"){
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

        int N = 1000;
        for( int i = 0; i < N; i++ ) {
            json frame = json::array({2, std::to_string(i), "Ping", json::object({{"seq", i }})});
            cut.client_->send(frame.dump());
        }

        std::this_thread::sleep_for(3s);

        REQUIRE(cut.transport_max_writes_in_flight() <= 1u);
        REQUIRE(cut.transport_max_write_queue_depth() > 0u);
        REQUIRE(cut.transport_current_write_queue_depth() == 0u);
        // EXPECT_GT(cut.transport_max_write_queue_depth(), 0u) << "Never observed queue growth; test may not have stressed the writer.";

        // EXPECT_EQ(cut.transport_current_write_queue_depth(), 0u) << "Never observed current queue depth; test may not have stressed the writer.";

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
        REQUIRE(seqs.size() == N);

        bool monotonic = true;
        for( int i=1; i < seqs.size(); i++ ){
            if( seqs[i] != seqs[i-1] + 1 ) {
                monotonic = false;
                break;
            }
        }
        REQUIRE(monotonic);


        //Cleanup
        cut.stop();
        server.stop();
        ioc.stop();
        if(io_thread.joinable() ) io_thread.join();

        REQUIRE_FALSE(bootHandlerCalled);
        REQUIRE_FALSE(authHandlerCalled);
        // REQUIRE(reply_ == R"([4,"abc","FormationViolation","[json.exception.out_of_range.403] key 'chargePointModel' not found",{}])");
    }
}