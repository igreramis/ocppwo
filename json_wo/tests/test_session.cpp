#include <gtest/gtest.h>
#include <chrono>
#include "test_server.hpp"
#include "reconnect.hpp"
#include "ws_client.hpp"
#include "session.hpp"
#include "reconnect_glue.hpp"
#include "test_harness.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;
using namespace std;
using Ms = std::chrono::milliseconds;

struct FakeTransport : Transport {
    void start(){
        ;
    }
    std::vector<std::string> outbound;

    void send(std::string text){
        outbound.push_back(std::move(text));
    }

    std::function<void(std::string_view)> on_message_;
    void on_message(std::function<void(std::string_view)> cb){
        on_message_ = std::move(cb);
    }

    std::function<void()> on_close_;
    void on_close(std::function<void()> cb){
        on_close_ = std::move(cb);
    }

    void close(){
        ;
        if( on_close_ ) {
            on_close_();
        }
    }

    // helper methods for simulating incoming traffic
    void inject_inbound_text(std::string_view sv){
        if( on_message_ ) {
            on_message_(sv);
        }
    }


};

TEST(Session, CanCaptureOutboundAndInjectInboundWithoutSockets){
    ;
    boost::asio::io_context io_;
    auto tOps_ = std::make_shared<FakeTransport>();
    auto sS_ = std::make_shared<SessionSignals>(SessionSignals{});
    Session s(io_, tOps_, sS_);
    bool replied_{false};

    s.send_call(BootNotification{"X100", "OpenAI"}, [&](const OcppFrame& f){
        replied_ = std::holds_alternative<CallResult>(f) || std::holds_alternative<CallError>(f);
    });

    ASSERT_EQ(tOps_->outbound.size(), 1) << "Expected one outbound message after send_call";

    // verify its a Call
    auto j = json::parse(tOps_->outbound.back());
    auto f = parse_frame(j);
    ASSERT_TRUE(std::holds_alternative<Call>(f)) << "Expected outbound frame to be a Call";

    auto message_id = std::get<Call>(f).messageId;
    BootNotificationResponse resp{"2024-01-01T00:00:00Z", 10, "Accepted"};
    CallResult cr{3, message_id, resp};
    tOps_->inject_inbound_text(json(cr).dump());
    ASSERT_TRUE(replied_) << "Expected reply callback to have been invoked upon inbound CallResult";
}

TEST(Session, ReplyResolvesOnceAndClearsPending){
    ;
    boost::asio::io_context io_;
    auto tOps_ = std::make_shared<FakeTransport>();
    auto sS_ = std::make_shared<SessionSignals>(SessionSignals{});
    Session s(io_, tOps_, sS_);
    bool replied_{false};

    ASSERT_EQ(s.pending.size(), 0) << "Expected no pending calls at start of test";
    s.send_call(BootNotification{"X100", "OpenAI"}, [&](const OcppFrame& f){
        replied_ = std::holds_alternative<CallResult>(f) || std::holds_alternative<CallError>(f);
    });

    ASSERT_EQ(tOps_->outbound.size(), 1) << "Expected one outbound message after send_call";

    ASSERT_EQ(s.pending.size(), 1) << "Expected one pending call after send_call";

    // verify its a Call
    auto j = json::parse(tOps_->outbound.back());
    auto f = parse_frame(j);
    ASSERT_TRUE(std::holds_alternative<Call>(f)) << "Expected outbound frame to be a Call";

    auto message_id = std::get<Call>(f).messageId;
    BootNotificationResponse resp{"2024-01-01T00:00:00Z", 10, "Accepted"};
    CallResult cr{3, message_id, resp};
    tOps_->inject_inbound_text(json(cr).dump());
    ASSERT_TRUE(replied_) << "Expected reply callback to have been invoked upon inbound CallResult";
    ASSERT_EQ(s.pending.size(), 0) << "Expected no pending calls after reply has been processed";
}