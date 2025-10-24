#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "router.hpp"
#include "ocpp_model.hpp"
#include <future>
// #include <catch2/catch.hpp>

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
}