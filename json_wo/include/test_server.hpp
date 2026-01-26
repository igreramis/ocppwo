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
        struct ClosePolicy {
            bool close_after_handshake = false;
            bool close_after_first_message = false;
            std::chrono::milliseconds close_after_ms{-1};
        };

        enum class EventType {
            Connect,
            Reconnect,
            BootAccepted,
            FirstHeartBeat,
            Disconnect
        };

        struct Event {
            EventType type;
            std::chrono::steady_clock::time_point ts;
        };


        TestServer(asio::io_context& ioc, unsigned short port);
        void start();
        void stop();
        // void send_boot_conf(const std::string& msg_id, int heartbeat_interval_seconds);
        void set_boot_conf(const std::string& msg_id, int heartbeat_interval_seconds);
        std::vector<Frame> received() const;
        std::vector<Frame> heartbeats() const;
        std::string last_boot_msg_id() const;
        bool is_client_disconnected() const;
        void set_close_policy(ClosePolicy p);
        void on_event(std::function<void(const Event&)> cb);
        std::vector<Event> events() const;
        void enable_manual_replies(bool enable);
        std::vector<std::string> received_call_message_ids() const;
        bool send_stored_reply_for(const std::string& message_id);
        private:
        std::function<void()> do_accept;
        struct StoredReply {
            std::string message_id;
            std::string reply_text;
        };       
        bool manual_replies_{false};
        mutable std::mutex replies_mtx_;
        std::vector<std::string> received_call_ids_;
        std::vector<StoredReply> stored_replies_;
        //
        void do_read();
        void arm_close_timer_();
        void record_event_(EventType t);
        mutable std::mutex events_mtx_;//io_context passed into this class and later on WsServerSession could be run on two
        //different threads. one thing that would happen from this is that on_message could run in parallel at the same time
        //thereby populating vectors in parallel. hence they need to be protected by a mutex.
        std::function<void(const Event&)> on_event_;

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
        std::vector<Event> events_;

        ClosePolicy close_policy_;
        bool first_message_seen_{false};
        std::shared_ptr<boost::asio::steady_timer> close_timer_;
        mutable std::mutex mtx_;
        unsigned short port_;
        unsigned connect_count_{0};
        bool running_ = false;
        std::string last_boot_msg_id_;
        int heartbeat_interval_ = 5;
        bool client_disconnected = false;
    };