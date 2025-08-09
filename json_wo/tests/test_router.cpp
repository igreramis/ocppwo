#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "router.hpp"
#include "ocpp_model.hpp"
// #include <catch2/catch.hpp>

TEST_CASE("Router dispatches to correct handler") {
    Router router;
    bool bootHandlerCalled = false;
    bool authHandlerCalled = false;

    // Mock handlers
    // auto bootHandler = [&](const Call& c) -> OcppFrame {
    //     bootHandlerCalled = true;
    //     CallResult result;
    //     // ...populate result as needed...
    //     return result;
    // };
    // auto authHandler = [&](const Call& c) -> OcppFrame {
    //     authHandlerCalled = true;
    //     CallResult result;
    //     // ...populate result as needed...
    //     return result;
    // };
    auto bootHandler = [&](const BootNotification& b) -> OcppFrame {
        bootHandlerCalled = true;
        BootNotificationResponse res = {
            "2025-07-16T12:00:00Z",
            300,
            "Accepted"
        };
        return CallResult {
            3,
            "c.messageId",
            res
        };
    };

    auto authHandler = [&](const Authorize& a) -> OcppFrame {
        authHandlerCalled = true;
        AuthorizeResponse res = {
            a.idTag + "OK",
        };
        return CallResult{
            3,
            "c.messageId",
            res
        };
    };
    // router.registerHandler("BootNotification", bootHandler);
    // router.registerHandler("Authorize", authHandler);
    router.addHandler<BootNotification>(bootHandler);
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

    SECTION("Authorize dispatch") {
        std::cout<<"1"<<std::endl;
        Call call;
        call.action = "Authorize";
        call.payload = json(Authorize{"test_id"});
        OcppFrame frame = router.route(call);
        REQUIRE(authHandlerCalled);
        REQUIRE_FALSE(bootHandlerCalled);
        REQUIRE(std::holds_alternative<CallResult>(frame));
    }

    SECTION("Unknown action dispatch") {
        Call call;
        call.action = "UnknownAction";
        OcppFrame frame = router.route(call);
        REQUIRE_FALSE(bootHandlerCalled);
        REQUIRE_FALSE(authHandlerCalled);
        REQUIRE(std::holds_alternative<CallError>(frame));
    }
}