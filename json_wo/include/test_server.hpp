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

    // ---------------------------------------------------------------------
    // Lifecycle: start()
    //
    // Purpose:
    //   Starts the async accept loop and begins serving a single active
    //   WebSocket session at a time.
    //
    // Touches / populates:
    //   - `do_accept` (std::function<void()>) is assigned to a recursive accept
    //     lambda and invoked.
    //   - `ss` (std::shared_ptr<WsServerSession>) is created per accepted socket.
    //   - Per-connection state is reset on each accept (see implementation):
    //       - `received_` cleared
    //       - `heartbeats_` cleared
    //       - `first_message_seen_` reset
    //       - `close_timer_` reset
    //   - `events_` is appended to via `record_event_`.
    //
    // Persistence / reset behavior:
    //   - `events_` is NOT cleared; it accumulates across the lifetime of the
    //     TestServer instance.
    //   - Manual-reply buffers (`received_call_ids_`, `stored_replies_`) are NOT
    //     cleared here (they are cleared only in `enable_manual_replies(...)`).
    //
    // Conditions / notes:
    //   - Must be called while `ioc_` is being pumped (io_context run/run_for).
    //   - Close policies (set via `set_close_policy`) may close the session
    //     immediately and prevent message telemetry from populating.
        void start();

    // ---------------------------------------------------------------------
    // Lifecycle: stop()
    //
    // Purpose:
    //   Stops the active session and cancels any close timer.
    //
    // Touches:
    //   - Cancels `close_timer_` if present.
    //   - Calls `ss->close()` if `ss` exists.
    //
    // Persistence / reset behavior:
    //   - Does NOT clear telemetry vectors (`received_`, `heartbeats_`, `events_`).
    //     (They remain available for assertions after stop.)
        void stop();

    // ---------------------------------------------------------------------
    // Configuration: set_boot_conf(msg_id, heartbeat_interval_seconds)
    //
    // Purpose:
    //   Sets the heartbeat interval that will be returned in BootAccepted.
    //
    // Writes:
    //   - `heartbeat_interval_` (int)
    //
    // Persistence / reset behavior:
    //   - Persists across connections until changed.
    //
    // Conditions / notes:
    //   - In current implementation, `msg_id` is ignored.
    //   - Used by the BootNotification handler when constructing
    //     BootNotificationResponse.
        void set_boot_conf(const std::string& msg_id, int heartbeat_interval_seconds);

        // ---------------------------------------------------------------------
        // Telemetry: received()
        //
        // Populates / returns:
        //   - `received_` (std::vector<Frame>)
        //       Each `Frame` contains:
        //         - `text`: raw inbound WebSocket text message
        //         - `t`: receive timestamp (std::chrono::steady_clock::time_point)
        //
        // Persistence / reset behavior:
        //   - Reset per connection: `received_` is cleared when a new TCP client
        //     is accepted (inside `start()`'s accept handler).
        //   - It does NOT persist across connections.
        //
        // Conditions to populate:
        //   - Client must connect and complete WebSocket handshake (so
        //     `WsServerSession::on_message` can fire).
        //   - Client must send at least one text frame.
        //   - Close policies can prevent population:
        //       - If `close_after_handshake` is enabled, the session is closed on
        //         `on_open` before any messages arrive.
        //       - If `close_after_first_message` is enabled, the server may close
        //         quickly after initial traffic, limiting what gets recorded.
        std::vector<Frame> received() const;

        // ---------------------------------------------------------------------
        // Telemetry: heartbeats()
        //
        // Populates / returns:
        //   - `heartbeats_` (std::vector<Frame>)
        //       Subset of `received_` containing inbound OCPP Call frames where
        //       `action == "HeartBeat"`.
        //
        // Persistence / reset behavior:
        //   - Reset per connection: `heartbeats_` is cleared when a new TCP client
        //     is accepted (inside `start()`'s accept handler).
        //   - It does NOT persist across connections.
        //
        // Conditions to populate:
        //   - Same base conditions as `received()` (handshake + inbound message).
        //   - Inbound message must parse as an OCPP Call and have
        //     `action == "HeartBeat"`.
        //   - If the client never becomes "online" (e.g., boot not accepted, or
        //     heartbeat gated), then no HeartBeat calls are sent and this stays
        //     empty.
        std::vector<Frame> heartbeats() const;
        std::string last_boot_msg_id() const;

        // ---------------------------------------------------------------------
        // Connection state probe: is_client_disconnected()
        //
        // Reads:
        //   - `client_disconnected` (bool)
        //
        // Persistence / reset behavior:
        //   - Sticky once set: `client_disconnected` is set to true when the active
        //     `WsServerSession` reports `on_close`.
        //   - It is NOT currently reset to false on subsequent accepts.
        //     (So if you need per-connection semantics, rely on `events()` or
        //     explicitly add a reset when a new client is accepted.)
        //
        // Conditions to become true:
        //   - A connection must exist and then close (server stop, peer close,
        //     close policy firing, etc.), and the session's close handler runs.
        bool is_client_disconnected() const;

        // ---------------------------------------------------------------------
        // Fault injection: set_close_policy(...)
        //
        // Writes:
        //   - `close_policy_` (ClosePolicy)
        //
        // Persistence / reset behavior:
        //   - Persists across connections until changed.
        //
        // Conditions / effects:
        //   - Policy is applied to the next (and subsequent) accepted sessions.
        //   - `close_after_handshake`:
        //       - Closes the WebSocket session on `WsServerSession::on_open`.
        //       - Prevents any `on_message` traffic, so `received_`, `heartbeats_`,
        //         and manual-reply buffers will not populate.
        //   - `close_after_first_message`:
        //       - Intended: close immediately after first inbound message.
        //       - Limits telemetry and may surface client-side reconnect behavior.
        //   - `close_after_ms`:
        //       - Arms a timer after accept; when it fires, closes the session.
        //
        // Note: these policies intentionally perturb timing/ordering; tests should
        // assert properties/bounds rather than exact sequences.
        void set_close_policy(ClosePolicy p);

        // ---------------------------------------------------------------------
        // Observability: on_event(callback)
        //
        // Writes:
        //   - `on_event_` (std::function<void(const Event&)>)
        //
        // Persistence / reset behavior:
        //   - Persists until replaced (assigning a new callback overwrites the old one).
        //   - No internal buffering beyond `events_`; callback is invoked synchronously
        //     at record time.
        //
        // Conditions to invoke:
        //   - Called whenever `record_event_(...)` is executed.
        //   - Currently recorded from:
        //       - Accept handler: Connect/Reconnect
        //       - Message handler: BootAccepted (on receiving BootNotification Call),
        //         FirstHeartBeat (on receiving first HeartBeat Call)
        //       - Close handler: Disconnect
        void on_event(std::function<void(const Event&)> cb);

        // ---------------------------------------------------------------------
        // Observability: events()
        //
        // Populates / returns:
        //   - `events_` (std::vector<Event>)
        //
        // Persistence / reset behavior:
        //   - Accumulates across the entire lifetime of the TestServer instance.
        //   - It is NOT cleared on new connections.
        //
        // Conditions to populate:
        //   - Same as `on_event`: every call to `record_event_(...)` appends.
        //   - This is usually the most reliable way to assert multi-connection
        //     sequences because it is not cleared per accept.
        std::vector<Event> events() const;

        // ---------------------------------------------------------------------
        // Manual replies: enable_manual_replies(enable)
        //
        // Writes:
        //   - `manual_replies_` (bool)
        //
        // Clears:
        //   - `received_call_ids_` (std::vector<std::string>)
        //   - `stored_replies_` (std::vector<StoredReply>)
        //
        // Persistence / reset behavior:
        //   - `manual_replies_` persists across connections until toggled.
        //   - The *buffers* are reset every time this method is called (both enable
        //     and disable clear them).
        //
        // Conditions / effects:
        //   - When enabled, Router-generated replies are NOT auto-sent; instead
        //     they are stored (keyed by messageId) and can be sent later using
        //     `send_stored_reply_for(...)`.
        //   - When disabled, replies are immediately sent on the wire.
        void enable_manual_replies(bool enable);

        // ---------------------------------------------------------------------
        // Manual replies: received_call_message_ids()
        //
        // Populates / returns:
        //   - `received_call_ids_` (std::vector<std::string>)
        //       Call messageIds for inbound OCPP Call frames whose replies were
        //       captured for manual sending.
        //   - (coupled with) `stored_replies_` (std::vector<StoredReply>)
        //       Serialized reply frames produced by Router, keyed by messageId.
        //
        // Persistence / reset behavior:
        //   - NOT automatically reset on new connections.
        //   - Reset only by calling `enable_manual_replies(...)`, which clears
        //     both `received_call_ids_` and `stored_replies_`.
        //   - Treat this as a "manual-reply window" controlled by
        //     `enable_manual_replies(true/false)`.
        //
        // Conditions to populate:
        //   - `manual_replies_` must be enabled (via `enable_manual_replies(true)`).
        //   - Client must connect + handshake and send an inbound message.
        //   - Inbound message must parse as an OCPP Call.
        //   - Router must generate a reply for that Call (reply is captured and
        //     stored instead of being auto-sent).
        std::vector<std::string> received_call_message_ids() const;

        // ---------------------------------------------------------------------
        // Manual replies: send_stored_reply_for(message_id)
        //
        // Uses:
        //   - `stored_replies_` (std::vector<StoredReply>)
        //   - `ss` (std::shared_ptr<WsServerSession>) as the active connection
        //
        // Persistence / reset behavior:
        //   - Consumes exactly one StoredReply entry: on success, the matching
        //     reply is erased from `stored_replies_`.
        //   - If you toggle `enable_manual_replies(...)`, all stored replies are
        //     dropped (cleared).
        //
        // Conditions to send / return true:
        //   - There must be an active server session (`ss` non-null) to write on.
        //   - A stored reply must exist whose message_id matches the argument.
        //   - Typically `manual_replies_` must have been true when the Call arrived;
        //     otherwise replies are auto-sent and nothing is stored.
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

        // ---------------------------------------------------------------------
        // Internal: arm_close_timer_()
        //
        // Purpose:
        //   If `close_policy_.close_after_ms >= 0`, arms `close_timer_` so that when
        //   it fires the current session is closed.
        //
        // Touches:
        //   - `close_timer_` (std::shared_ptr<boost::asio::steady_timer>)
        //
        // Persistence / reset behavior:
        //   - Timer is recreated per accepted connection (reset on accept).
        //
        // Conditions to arm:
        //   - `close_policy_.close_after_ms.count() >= 0`
        //   - `ss` must be non-null (an active session exists)
        void arm_close_timer_();

        // ---------------------------------------------------------------------
        // Internal: record_event_(EventType)
        //
        // Purpose:
        //   Append an Event (type + steady_clock timestamp) to `events_`, and if an
        //   observer is registered, invoke it.
        //
        // Populates:
        //   - `events_` (std::vector<Event>) under `events_mtx_`.
        //
        // Persistence / reset behavior:
        //   - `events_` accumulates across the lifetime of the TestServer instance
        //     (not cleared per connection).
        //
        // Conditions to invoke observer:
        //   - `on_event_` must be set (via `on_event(...)`).
        void record_event_(EventType t);
        mutable std::mutex events_mtx_;//io_context passed into this class and later on WsServerSession could be run on two
        //different threads. one thing that would happen from this is that on_message could run in parallel at the same time
        //thereby populating vectors in parallel. hence they need to be protected by a mutex.
        std::function<void(const Event&)> on_event_;

        // ---------------------------------------------------------------------
        // Router handler: TestBootNotificationHandler(payload, message_id)
        //
        // Purpose:
        //   Implements BootNotification handling for the legacy Router API that
        //   returns an `OcppFrame` (CallResult/CallError).
        //
        // Touches / reads:
        //   - Reads `heartbeat_interval_` to construct BootNotificationResponse.
        //
        // Conditions / behavior:
        //   - If required fields are missing/empty, returns CallError.
        //   - Otherwise returns CallResult containing BootNotificationResponse.
        OcppFrame TestBootNotificationHandler(const BootNotification&, const std::string& );

        // ---------------------------------------------------------------------
        // Router handler: TestBootNotificationHandler_v2(payload)
        //
        // Purpose:
        //   Implements BootNotification handling for the newer Router API that
        //   returns `tl::expected<Response, std::string>`.
        //
        // Touches / reads:
        //   - Reads `heartbeat_interval_` to construct BootNotificationResponse.
        //
        // Conditions / behavior:
        //   - On invalid payload, returns `tl::unexpected(error_string)`.
        //   - On success, returns BootNotificationResponse.
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