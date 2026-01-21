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
        .make_session = [&](boost::asio::io_context& ioc, std::shared_ptr<Transport> transport, std::shared_ptr<SessionSignals> sigs) -> std::unique_ptr<Session> {
            return std::make_unique<Session>(ioc, transport, sigs);
        }
    };

    TestHarness tH(ioc, "127.0.0.1", port); tH.server_start();
    ClientLoop cl(ioc, cfg, f);
    cl.start();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ( std::chrono::steady_clock::now() < deadline ) {
        ioc.poll();
    }

    tH.server_force_close();

    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ( std::chrono::steady_clock::now() < deadline ) {
        ioc.poll();
    }

    tH.server_start();

    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while ( std::chrono::steady_clock::now() < deadline ) {
        ioc.poll();
    }

    ASSERT_EQ(cl.online_transitions(), 2);
    // ClientLoop::State state = cl.state();
    // ASSERT_EQ(state, ClientLoop::State::Offline);
    // ASSERT_EQ(cl.connect_attempts(), 0u);
    // ASSERT_EQ(cl.online_transitions(), 0u);
}