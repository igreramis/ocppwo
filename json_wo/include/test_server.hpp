#pragma once
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <chrono>
#include "ws_server_session.hpp"
#include "router.hpp"

namespace beast  = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class TestServer {
    struct Frame {
        std::string text;
        std::chrono::steady_clock::time_point t;
    };

    public:
        TestServer(asio::io_context& ioc, unsigned short port);
        void start();
        void stop();
        // void send_boot_conf(const std::string& msg_id, int heartbeat_interval_seconds);
        void set_boot_conf(const std::string& msg_id, int heartbeat_interval_seconds);
        std::vector<Frame> received() const;
        std::vector<Frame> heartbeats() const;
        std::string last_boot_msg_id() const;
        bool is_client_disconnected() const;
    private:
        void do_read();
        OcppFrame TestBootNotificationHandler(const BootNotification&, const std::string& );
        tl::expected<BootNotificationResponse, std::string> TestBootNotificationHandler_v2(const BootNotification&);
        boost::asio::io_context& ioc_;
        tcp::acceptor acc_;
        websocket::stream<tcp::socket> ws_;
        beast::flat_buffer buffer_;
        Router router_;
        std::shared_ptr<WsServerSession> ss;
        std::vector<Frame> received_;
        std::vector<Frame> heartbeats_;
        mutable std::mutex mtx_;
        unsigned short port_;
        bool running_ = false;
        std::string last_boot_msg_id_;
        int heartbeat_interval_ = 5;
        bool client_disconnected = false;
    };