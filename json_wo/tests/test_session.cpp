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

struct TimerPump{
    struct Item{
        std::chrono::milliseconds delay;
        std::function<void()> cb;
    };
    std::vector<Item> timers;

    void post_after(std::chrono::milliseconds ms, std::function<void()> cb){
        timers.push_back({ms, std::move(cb)});
    }
    void run_all(){
        for(auto& timer: timers){timer.cb();}
        timers.clear();
    }
    template<class Pred>
    void run_while(Pred p){
        while(p() && !timers.empty()){
            auto it = timers.front();
            timers.erase(timers.begin());
            it.cb();
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

TEST(Session, CallResultResolvesPendingExactlyOnce){
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
    
    //inject same message again and verify callback not invoked again
    replied_ = false; // reset
    tOps_->inject_inbound_text(json(cr).dump());
    ASSERT_FALSE(replied_) << "Expected reply callback not to be invoked again upon duplicate inbound CallResult";
}

TEST(Session, CallErrorResolvesPendingExactlyOnce){
    ;
    boost::asio::io_context io_;
    auto tOps_ = std::make_shared<FakeTransport>();
    auto sS_ = std::make_shared<SessionSignals>(SessionSignals{});
    Session s(io_, tOps_, sS_);
    bool replied_{false};

    s.send_call(BootNotification{"X100", "OpenAI"}, [&](const OcppFrame& f){
        // replied_ = std::holds_alternative<CallResult>(f) || std::holds_alternative<CallError>(f);
        replied_ = std::holds_alternative<CallError>(f);
    });

    ASSERT_EQ(tOps_->outbound.size(), 1) << "Expected one outbound message after send_call";

    // verify its a Call
    auto j = json::parse(tOps_->outbound.back());
    auto f = parse_frame(j);
    ASSERT_TRUE(std::holds_alternative<Call>(f)) << "Expected outbound frame to be a Call";

    auto message_id = std::get<Call>(f).messageId;
    BootNotificationResponse resp{"2024-01-01T00:00:00Z", 10, "Accepted"};
    CallError ce{4, message_id, "InternalError", "simulated error", json::object()};
    tOps_->inject_inbound_text(json(ce).dump());
    ASSERT_TRUE(replied_) << "Expected reply callback to have been invoked upon inbound CallError";
    
    //inject same message again and verify callback not invoked again
    replied_ = false; // reset
    tOps_->inject_inbound_text(json(ce).dump());
    ASSERT_FALSE(replied_) << "Expected reply callback not to be invoked again upon duplicate inbound CallError";
}

TEST(Session, TimeoutResolvesPendingAndClearsIt){
    boost::asio::io_context io_;
    auto tOps_ = std::make_shared<FakeTransport>();
    auto sS_ = std::make_shared<SessionSignals>(SessionSignals{});
    Session s(io_, tOps_, sS_);
    bool replied_{false};

    s.send_call(BootNotification{"X100", "OpenAI"}, [&](const OcppFrame& f){

        replied_ = std::holds_alternative<CallError>(f) && 
            (std::get<CallError>(f).errorCode == "Timeout");

    }, std::chrono::seconds(0));

    ASSERT_EQ(tOps_->outbound.size(), 1) << "Expected one outbound message after send_call";

    ASSERT_EQ(s.pending.size(), 1) << "Expected one pending call after send_call";

    //why are we using io_.poll() here?
    //lets answer this by comparing with io_.run() which we normally have been using.
    //io_.run() would block until all work is done, which may not be what we want in a test.
    //this includes waiting on timers until their timeout has expired.
    //io_.poll() does not block. it executes events that are ready. ignores that are not and
    //returns.
    //using io_.poll() in tests is more deterministic than io_.run()
    io_.poll();
    ASSERT_TRUE(replied_) << "Expected reply CallError with errorCode Timeout";

    ASSERT_EQ(s.pending.size(), 0) << "Expected no pending calls after timeout";
}

TEST(Session, TransportCloseResolvesAllPendingAndClearsIt) {
    boost::asio::io_context io_;
    auto tOps_ = std::make_shared<FakeTransport>();
    auto sS_ = std::make_shared<SessionSignals>(SessionSignals{});
    Session s(io_, tOps_, sS_);

    int resolved = 0;

    s.send_call(BootNotification{"X100", "OpenAI"}, [&](const OcppFrame& f){
        ASSERT_TRUE(std::holds_alternative<CallError>(f));
        ASSERT_EQ(std::get<CallError>(f).errorCode, "ConnectionClosed");
        resolved++;
    }, std::chrono::seconds(30));

    s.send_call(BootNotification{"X101", "OpenAI"}, [&](const OcppFrame& f){
        ASSERT_TRUE(std::holds_alternative<CallError>(f));
        ASSERT_EQ(std::get<CallError>(f).errorCode, "ConnectionClosed");
        resolved++;
    }, std::chrono::seconds(30));

    tOps_->close();

    ASSERT_EQ(resolved, 2) << "Expected both pending calls to be resolved upon transport close";
    ASSERT_EQ(s.pending.size(), 0) << "Expected no pending calls after transport close";
    //you just check for resolved or you check for CallError and then errorCode and
    //then resolve
}

TEST(Session, UnmatchedRepliesAreCountedAndIgnored){
    ;
    boost::asio::io_context io_;
    auto tOps_ = std::make_shared<FakeTransport>();
    auto sS_ = std::make_shared<SessionSignals>(SessionSignals{});
    Session s(io_, tOps_, sS_);
    bool replied_{false};

    s.send_call(BootNotification{"X100", "OpenAI"}, [&](const OcppFrame& f){
        replied_ = std::holds_alternative<CallResult>(f) || std::holds_alternative<CallError>(f);
    });

    uint64_t unmatched = s.unmatched_replies();

    //send a reply with an unmatched id
    BootNotificationResponse resp{"2024-01-01T00:00:00Z", 10, "Accepted"};
    CallResult cr{3, "unmatched_id", resp};

    tOps_->inject_inbound_text(json(cr).dump());

    ASSERT_EQ(s.unmatched_replies(), unmatched + 1) << "Expected unmatched replies count uponreceiving unmatched CallResult";


    auto f = json::parse(tOps_->outbound.back());
    auto right_id = std::get<Call>(parse_frame(f)).messageId;
    CallResult crOk{3, right_id, resp};
    tOps_->inject_inbound_text(json(crOk).dump());
    ASSERT_TRUE(replied_) << "Expected reply callback to have been invoked upon inbound CallResult";

    //duplicates
    unmatched = s.unmatched_replies();
    tOps_->inject_inbound_text(json(crOk).dump());
    ASSERT_EQ(s.unmatched_replies(), unmatched+1) << "Expected unmatched replies count not to increase upon receiving duplicate CallResult";
}

TEST(Session, MismatchedReplyPayloadResolvesProtocolError){
    ;
    boost::asio::io_context io_;
    auto tOps_ = std::make_shared<FakeTransport>();
    auto sS_ = std::make_shared<SessionSignals>();
    Session s(io_, tOps_, sS_);
    bool replied_{false};
    OcppFrame got;

    s.send_call(BootNotification{"X100", "OpenAI"}, [&](const OcppFrame& f){
        replied_ = std::holds_alternative<CallResult>(f) || std::holds_alternative<CallError>(f);
        got = f;
    });

    ASSERT_EQ(tOps_->outbound.size(), 1) << "Expected one outbound message after send_call";

    auto j = json::parse(tOps_->outbound.back());
    auto f = parse_frame(j);
    ASSERT_TRUE(std::holds_alternative<Call>(f)) << "Expected outbound frame to be a Call";
    auto message_id = std::get<Call>(f).messageId;

    //i want to make a CallResult with wrong payload type.
    CallResult cr{3, message_id, json::object({{"unexpected_field", 123}})};
    tOps_->inject_inbound_text(json(cr).dump());
    ASSERT_TRUE(replied_) << "Expected reply callback to have been invoked upon inbound CallResult";
    ASSERT_TRUE(std::holds_alternative<CallError>(got)) << "Expected CallError";
    ASSERT_EQ(std::get<CallError>(got).errorCode, "ProtocolError") << "Expected ProtocolError due to payload mismatch";
}

TEST(Session, TimeoutIsCancelledOnCallResult) {
    boost::asio::io_context io_;
    auto tOps_ = std::make_shared<FakeTransport>();
    auto sS_ = std::make_shared<SessionSignals>();
    Session s(io_, tOps_, sS_);
    uint32_t resolved = 0;

    s.send_call(BootNotification{"X100", "OpenAI"}, [&](const OcppFrame& f){

        resolved++;

    }, std::chrono::seconds(1));

    ASSERT_EQ(tOps_->outbound.size(), 1) << "Expected one outbound message after send_call";

    auto j = json::parse(tOps_->outbound.back());
    auto f = parse_frame(j);
    ASSERT_TRUE(std::holds_alternative<Call>(f)) << "Expected outbound frame to be a Call";
    auto message_id = std::get<Call>(f).messageId;

    BootNotificationResponse resp{"2024-01-01T00:00:00Z", 10, "Accepted"};
    CallResult cr{3, message_id, resp};
    tOps_->inject_inbound_text(json(cr).dump());
    ASSERT_EQ(resolved, 1) << "Expected only one resolution of the pending call (no timeout after CallResult)";
    //wait for 2 seconds to see if timeout happens
    io_.run_for(std::chrono::seconds(2));
    ASSERT_EQ(resolved, 1) << "Expected only one resolution of the pending call (no timeout after CallResult)";
}

TEST(Session, TwoOutstandingCallsResolveToCorrectRequest_OutOfOrderReplies) {
    boost::asio::io_context io_;
    TestHarness tH_(io_, "127.0.0.1", 23456);
    tH_.server_.set_boot_conf("boot_msg_id", 2);tH_.server_start();tH_.server_manual_replies(true);
    tH_.client_start();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    std::string boot_id;
    while( std::chrono::steady_clock::now() < deadline ) {
        // std::this_thread::sleep_for(100ms);
        io_.poll();
        boot_id = tH_.server_.last_boot_msg_id();
        if( !boot_id.empty() ) break;
    }
    ASSERT_FALSE(boot_id.empty()) << "Client never sent BootNotification";

    std::promise<std::string> pA; auto fA = pA.get_future();
    std::promise<std::string>pB; auto fB = pB.get_future();
    tH_.client_.send_boot([&pA](const OcppFrame& f){
        ASSERT_TRUE(std::holds_alternative<CallResult>(f)) << "Expected BootNotification CallResult";
        pA.set_value("A");
    });

    tH_.client_.send_authorize([&pB](const OcppFrame& f){
        ASSERT_TRUE(std::holds_alternative<CallResult>(f)) << "Expected Authorize CallResult";
        pB.set_value("B");
    });

    while(tH_.server_received_call_ids().size() < 2 && std::chrono::steady_clock::now() < deadline) {
        io_.poll();
    }
    auto ids = tH_.server_received_call_ids();
    ASSERT_EQ(ids.size(), 2) << "Expected server to have received two Calls";

    std::string idA = ids[0], idB = ids[1];

    ASSERT_TRUE(tH_.server_send_reply_for(idB));
    ASSERT_TRUE(tH_.server_send_reply_for(idA));

    io_.run_for(std::chrono::milliseconds(200));

    ASSERT_EQ(fB.get(), "B");
    ASSERT_EQ(fA.get(), "A");
}