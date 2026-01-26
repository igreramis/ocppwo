#include <gtest/gtest.h>
#include <chrono>
#include "client_loop.hpp"
#include "ws_client.hpp"
#include "session.hpp"
#include "test_harness.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;
using namespace std;
using Ms = std::chrono::milliseconds;

static void run_until(boost::asio::io_context& ioc, std::function<bool ()> pred, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while( std::chrono::steady_clock::now() < deadline ) {
        if( pred() ) return;
        ioc.run_for(std::chrono::milliseconds(10));
        ioc.restart();
    }
}

#if 0
TEST(e2e, StartsOffline) {
    boost::asio::io_context ioc;

    //Pick an ephemeral port
    unsigned short port = 0;
    {
        tcp::acceptor tmp(ioc, {tcp::v4(), 0});
        port = tmp.local_endpoint().port();
    }

    ClientLoop::Config cfg{
        .host = "127.0.0.1",
        .port = port,
        .url = ""
    };

    ClientLoop::Factories f{
        .make_transport = nullptr,
        .make_session = nullptr
    };

    
    ClientLoop cl(ioc, cfg, f);
    ClientLoop::State state = cl.state();
    ASSERT_EQ(state, ClientLoop::State::Offline);
    ASSERT_EQ(cl.connect_attempts(), 0u);
    ASSERT_EQ(cl.online_transitions(), 0u);
}

TEST(e2e, ReconnectTriggersNewBootNotification) {
    boost::asio::io_context ioc;

    //Pick an ephemeral port
    unsigned short port = 0;
    {
        tcp::acceptor tmp(ioc, {tcp::v4(), 0});
        port = tmp.local_endpoint().port();
    }


    ClientLoop::Config cfg{
        .host = "127.0.0.1",
        .port = port,
        .url = ""
    };

    ClientLoop::Factories f{
        .make_transport = [&](boost::asio::io_context& ioc, std::string host, std::string port)->std::shared_ptr<WsClient>{
            return std::make_shared<WsClient>(ioc, host, port);
        },
        .make_session = [&](boost::asio::io_context& ioc, std::shared_ptr<Transport> transport, std::shared_ptr<SessionSignals> sigs) -> std::shared_ptr<Session> {
            return std::make_shared<Session>(ioc, transport, sigs);
        }
    };

    TestHarness tH(ioc, "127.0.0.1", port); tH.server_start();
    ClientLoop cl(ioc, cfg, f);
    cl.start();

    run_until(ioc, [&]{return false; }, std::chrono::seconds(10));

    tH.server_force_close();

    run_until(ioc, [&]{return cl.online_transitions() == 1; }, std::chrono::seconds(10));
    ASSERT_EQ(cl.online_transitions(), 1);

    tH.server_start();

    run_until(ioc, [&]{return cl.online_transitions() == 2; }, std::chrono::seconds(10));

    ASSERT_EQ(cl.online_transitions(), 2);
}

TEST(e2e, HeartbeatsStopOnCloseAndResumeAfterReconnect) {
    boost::asio::io_context ioc;

    //Pick an ephemeral port
    unsigned short port = 0;
    {
        tcp::acceptor tmp(ioc, {tcp::v4(), 0});
        port = tmp.local_endpoint().port();
    }


    ClientLoop::Config cfg{
        .host = "127.0.0.1",
        .port = port,
        .url = ""
    };

    ClientLoop::Factories f{
        .make_transport = [&](boost::asio::io_context& ioc, std::string host, std::string port)->std::shared_ptr<WsClient>{
            return std::make_shared<WsClient>(ioc, host, port);
        },
        .make_session = [&](boost::asio::io_context& ioc, std::shared_ptr<Transport> transport, std::shared_ptr<SessionSignals> sigs) -> std::shared_ptr<Session> {
            return std::make_shared<Session>(ioc, transport, sigs);
        }
    };

    TestHarness tH(ioc, "127.0.0.1", port); tH.server_start();
    ClientLoop cl(ioc, cfg, f);
    cl.start();

    ASSERT_EQ(tH.server_.heartbeats().size(), 0u);
    run_until(ioc, [&]{ return false; }, std::chrono::seconds(10));
    ASSERT_EQ(cl.online_transitions(), 1);
    ASSERT_GE(tH.server_.heartbeats().size(), 1u);
    
    tH.server_force_close();
    run_until(ioc, [&]{ return false;}, std::chrono::seconds(5));
    
    tH.server_start();
    
    run_until(ioc, [&]{ return false; }, std::chrono::seconds(10));
    ASSERT_EQ(cl.online_transitions(), 2);
    ASSERT_GE(tH.server_.heartbeats().size(), 1u);
}


TEST(e2e, ServerCloseFailsPendingCallsAndReconnects) {
    boost::asio::io_context ioc;

    //Pick an ephemeral port
    unsigned short port = 0;
    {
        tcp::acceptor tmp(ioc, {tcp::v4(), 0});
        port = tmp.local_endpoint().port();
    }


    ClientLoop::Config cfg{
        .host = "127.0.0.1",
        .port = port,
        .url = ""
    };

    std::weak_ptr<Session> session_wk;
    ClientLoop::Factories f{
        .make_transport = [&](boost::asio::io_context& ioc, std::string host, std::string port)->std::shared_ptr<WsClient>{
            return std::make_shared<WsClient>(ioc, host, port);
        },
        .make_session = [&](boost::asio::io_context& ioc, std::shared_ptr<Transport> transport, std::shared_ptr<SessionSignals> sigs) -> std::shared_ptr<Session> {
            auto p = std::make_shared<Session>(ioc, transport, sigs);
            session_wk = p;
            return p;
        }
    };

    TestHarness tH(ioc, "127.0.0.1", port); 
    tH.server_.enable_manual_replies(true); tH.server_start();
    ClientLoop cl(ioc, cfg, f);
    cl.start();

    run_until(ioc, [&]{ return false; }, std::chrono::seconds(10));

    std::promise<bool> p;
    auto f_p = p.get_future();
    if( auto session = session_wk.lock())
    {
        session->send_call(HeartBeat{}, [&p](const OcppFrame &frame){
            if( std::holds_alternative<CallError>(frame) ) {
                p.set_value(true);
            }
        });
    }
    
    run_until(ioc, [&]{ return false; }, std::chrono::seconds(10));
    tH.server_force_close();

    run_until(ioc, [&]{ return false;}, std::chrono::seconds(5));
    
    ASSERT_EQ(f_p.get(), true);
    tH.server_start();
    
    run_until(ioc, [&]{ return false; }, std::chrono::seconds(10));
    ASSERT_EQ(cl.online_transitions(), 2);
}


TEST(e2e, ResumeDelayDelaysHeartbeatAfterReconnect) {
    boost::asio::io_context ioc;

    //Pick an ephemeral port
    unsigned short port = 0;
    {
        tcp::acceptor tmp(ioc, {tcp::v4(), 0});
        port = tmp.local_endpoint().port();
    }


    ClientLoop::Config cfg{
        .host = "127.0.0.1",
        .port = port,
        .url = ""
    };

    std::weak_ptr<Session> session_wk;
    ClientLoop::Factories f{
        .make_transport = [&](boost::asio::io_context& ioc, std::string host, std::string port)->std::shared_ptr<WsClient>{
            return std::make_shared<WsClient>(ioc, host, port);
        },
        .make_session = [&](boost::asio::io_context& ioc, std::shared_ptr<Transport> transport, std::shared_ptr<SessionSignals> sigs) -> std::shared_ptr<Session> {
            auto p = std::make_shared<Session>(ioc, transport, sigs);
            session_wk = p;
            return p;
        }
    };

    TestHarness tH(ioc, "127.0.0.1", port);tH.server_start();
    ClientLoop cl(ioc, cfg, f);
    cl.start();

    run_until(ioc, [&]{ return false; }, std::chrono::seconds(5));
    
    auto hb_before = tH.server_.heartbeats().size();
    
    tH.server_force_close();
    run_until(ioc, [&]{ return false;}, std::chrono::seconds(5));
    
    tH.server_start();

    run_until(ioc, [&]{ return (tH.server_.events().size() > 0) && (tH.server_.events().back().type == TestServer::EventType::FirstHeartBeat);}, std::chrono::seconds(15));
    ASSERT_EQ(tH.server_.events().back().type, TestServer::EventType::FirstHeartBeat);
    auto hb_time = tH.server_.events().back().ts;
    auto msg_time = (tH.server_.received().rbegin()) -> t;
    auto reconnect_time = (tH.server_.received().rbegin() + 1) -> t;
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(msg_time - reconnect_time);
    ASSERT_GE(delta.count(), 5250);
    //TODO: 5250 is a magic number that needs to be replaced once todo items
    //in session.hpp , client_loop.hpp have beneworked out.
}
#endif

TEST(e2e, ReconnectBackoffSchedulesIncreasingDelaysUntilCap) {
    boost::asio::io_context ioc;

    //Pick an ephemeral port
    unsigned short port = 0;
    {
        tcp::acceptor tmp(ioc, {tcp::v4(), 0});
        port = tmp.local_endpoint().port();
    }

    std::vector<int> recorded_backoffs;
    ClientLoop::Config cfg{
        .host = "127.0.0.1",
        .port = port,
        .url = "",
        .on_backoff_scheduled = [&recorded_backoffs](std::chrono::milliseconds delay) {
            recorded_backoffs.push_back(static_cast<int>(delay.count()));
            //why are we using static_cast<int> here? what does .count() return?
            // .count() returns the number of ticks as an integral type, which is typically of type std::chrono::milliseconds::rep
            // static_cast<int> is used to convert this count to an int for easier handling and storage in the vector.
            //but if count() returns an integral type, why not use that type directly in the vector?
            // using int simplifies comparisons and assertions in tests, avoiding potential issues with different integral types.
        }
    };

    std::weak_ptr<Session> session_wk;
    ClientLoop::Factories f{
        .make_transport = [&](boost::asio::io_context& ioc, std::string host, std::string port)->std::shared_ptr<WsClient>{
            return std::make_shared<WsClient>(ioc, host, port);
        },
        .make_session = [&](boost::asio::io_context& ioc, std::shared_ptr<Transport> transport, std::shared_ptr<SessionSignals> sigs) -> std::shared_ptr<Session> {
            auto p = std::make_shared<Session>(ioc, transport, sigs);
            session_wk = p;
            return p;
        }
    };

    TestHarness tH(ioc, "127.0.0.1", port);tH.server_.set_close_policy(TestServer::ClosePolicy{.close_after_handshake = true});tH.server_start();
    ClientLoop cl(ioc, cfg, f);
    cl.start();

    run_until(ioc, [&]{ return recorded_backoffs.size() == 4 ; }, std::chrono::seconds(15));
    
    for( size_t i = 1; i < recorded_backoffs.size(); i++ ) {
        ASSERT_LE(recorded_backoffs[i], std::chrono::milliseconds(30000).count());//todo: the policy for reconnect module should be injectable
        ASSERT_LE(recorded_backoffs[i-1], std::chrono::milliseconds(30000).count());
        ASSERT_GT(recorded_backoffs[i], recorded_backoffs[i-1] * 0.6);
    }
}