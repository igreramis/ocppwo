#include "test_server.hpp"

TestServer::TestServer(asio::io_context& ioc, unsigned short port):
    ioc_(ioc), port_(port), ws_(ioc), acc_(ioc, tcp::endpoint(tcp::v4(), port)){
    // router_.addHandler<BootNotification>(BootNotificationHandler);
    router_.addHandler<BootNotification>([this](const BootNotification& p, const std::string& s){
        return this->TestBootNotificationHandler(p, s);
    });
    router_.register_handler<OcppActionName<BootNotification>, BootNotification, BootNotificationResponse>([this](const BootNotification &x)->tl::expected<BootNotificationResponse, std::string>{
        return this->TestBootNotificationHandler_v2(x);
    });
    router_.addHandler<Authorize>(AuthorizeHandler);
    router_.register_handler<OcppActionName<Authorize>, Authorize, AuthorizeResponse>(AuthorizeHandler_v2);
    router_.addHandler<HeartBeat>(HeartBeatHandler);
    router_.register_handler<OcppActionName<HeartBeat>, HeartBeat, HeartBeatResponse>(HeartBeatHandler_v2);
}

void TestServer::start(){
    std::function<void()> do_accept;
    do_accept = [this, do_accept]() {
        acc_.async_accept([this, do_accept](boost::system::error_code ec, tcp::socket s) {
            if (!ec) {
                std::cout << "New client connected from " << s.remote_endpoint() << "\n";

                record_event_(connect_count_++ == 1 ? EventType::Connect : EventType::Reconnect);

                //over here should be a method call for things to do once client connected.
                received_.clear();
                heartbeats_.clear();

                first_message_seen_ = false;
                close_timer_.reset();

                
                ss = std::make_shared<WsServerSession>(std::move(s));
                std::weak_ptr<WsServerSession> wss = ss;
                if(close_policy_.close_after_handshake){
                    ss->close();
                    return;
                }
                ss->on_message([this, wss](std::string_view msg){
                    if(close_policy_.close_after_first_message && first_message_seen_){
                        first_message_seen_ = true;
                        if(auto s = wss.lock())
                            s->close();
                        return;
                    }
                    const auto now = std::chrono::steady_clock::now();
                    std::cout << "Rx<--- " << msg << "\n";
                    {
                        std::lock_guard<std::mutex> lock(mtx_);
                        received_.push_back(Frame{std::string(msg), now});
                        json j = json::parse(std::string(msg));
                        OcppFrame f = parse_frame(j);
                        if( std::holds_alternative<Call>(f) ) {
                            Call c = std::get<Call>(f);
                            if( c.action == "HeartBeat" ) {
                                heartbeats_.push_back(Frame{std::string(msg), now});
                                if( heartbeats_.size() == 1 ) {
                                    record_event_(EventType::FirstHeartBeat);
                                }
                            }
                            else if( c.action == "BootNotification" ) {
                                last_boot_msg_id_ = c.messageId;
                                record_event_(EventType::BootAccepted);
                            }
                            else if( c.action == "Authorize" ) {
                                return;
                            }
                            else if( c.action == "Ping" ) {
                                return;
                            }
                        }
                    }
                    router_.handle_incoming(msg, [this](std::string&& reply){
                        bool manual = false;
                        {
                            std::lock_guard<std::mutex> lk(replies_mtx_);
                            manual = manual_replies_;
                        }
                        if(!manual)
                        {
                            ss->send(reply);
                            return;
                        }
                        try {
                            //store the reply for later sending
                            std::string s = received_.back().text;
                            json j = json::parse(s);
                            OcppFrame f = parse_frame(j);
                            if(std::holds_alternative<Call>(f)){
                                auto messageId = std::get<Call>(f).messageId;
                                std::lock_guard<std::mutex> lk(replies_mtx_);
                                received_call_ids_.push_back(messageId);
                                stored_replies_.push_back(StoredReply{messageId, std::move(reply)});
                            }
                        } catch(...) {
                            ;
                        }

                    });
                });

                // handle the signal/callback from WsServerSession when the connection has been closed
                ss->on_close([this](){
                    std::cout << "Test Server: Client disconnected\n";
                    std::lock_guard<std::mutex> lock(mtx_);
                    client_disconnected = true;
                    last_boot_msg_id_.clear();
                    record_event_(EventType::Disconnect);
                });

                ss->start();

                arm_close_timer_();
            }
            // do_accept();
        });
    };
    do_accept();
}

void TestServer::do_read(){

}

OcppFrame TestServer::TestBootNotificationHandler(const BootNotification& b, const std::string& msgId)
{
    if( b.chargePointModel.empty() || b.chargePointVendor.empty() )
    {
        json error_details {
            {"hint", "chargePointModel or chargePointVendor empty..."}
        };

        return CallError{
            4,
            msgId,
            "304"
            "Unexpected payload",
            error_details
        };
    }
    
    BootNotificationResponse res = {
        "2025-07-16T12:00:00Z",
        heartbeat_interval_,
        "Accepted"
    };
    std::cout<<"BootNotificationHandler: Returning CallResult"<<std::endl;
    return CallResult{
        3,
        msgId,
        res
    };
}

tl::expected<BootNotificationResponse, std::string> TestServer::TestBootNotificationHandler_v2(const BootNotification& b)
{
    if( b.chargePointModel.empty() || b.chargePointVendor.empty() )
    {
        return tl::unexpected("payload...");
    }
    
    return BootNotificationResponse {
        "2025-07-16T12:00:00Z",
        heartbeat_interval_,
        "Accepted"
    };
}

void TestServer::stop(){
    if(close_timer_) close_timer_->cancel();
    ss->close();
}

void TestServer::set_boot_conf(const std::string& msg_id, int heartbeat_interval_seconds){
    heartbeat_interval_ = heartbeat_interval_seconds;
}

std::vector<TestServer::Frame> TestServer::received() const{
    return received_;
}

std::vector<TestServer::Frame> TestServer::heartbeats() const{
    return heartbeats_;
}

std::string TestServer::last_boot_msg_id() const{
    return last_boot_msg_id_;
}

bool TestServer::is_client_disconnected() const{
    std::lock_guard<std::mutex> lock(mtx_);
    return client_disconnected;
}

void TestServer::set_close_policy(ClosePolicy p){
    close_policy_ = p;
}
//do we need to chain?
//why the fuck are we using std::move here?
void TestServer::on_event(std::function<void(const Event&)> cb){
    on_event_ = std::move(cb);//why are we using std::move() here? 
    //we create a lambda in the call to on_event and then that moves in memory
    //otherwise, if we are passing a method, that is a different thing, then move does not have a purepose.
}

std::vector<TestServer::Event> TestServer::events() const{
    std::lock_guard<std::mutex> lk(events_mtx_);
    return events_;
}

void TestServer::enable_manual_replies(bool enable){
    std::lock_guard<std::mutex> lk(replies_mtx_);
    manual_replies_ = enable;
    //clear the relevant vecotrs.
    received_call_ids_.clear();
    stored_replies_.clear();
}

std::vector<std::string> TestServer::received_call_message_ids() const {
    std::lock_guard<std::mutex> lk(replies_mtx_);
    return received_call_ids_;
}

bool TestServer::send_stored_reply_for(const std::string& message_id) {
    std::shared_ptr<WsServerSession> s = ss;
    if (!s) return false;

    std::string reply_text;
    StoredReply r;
    {
        std::lock_guard<std::mutex> lk(replies_mtx_);
        auto it = std::find_if(stored_replies_.begin(), stored_replies_.end(), [message_id](StoredReply &r) {
            return r.message_id == message_id;
        });
        if(it == stored_replies_.end()) return false;
        r = std::move(*it);
        stored_replies_.erase(it);
    }
    s->send(r.reply_text);
    return true;
}

void TestServer::arm_close_timer_(){
    if(close_policy_.close_after_ms.count()<0) return;
    if(!ss) return;

    close_timer_ = std::make_shared<boost::asio::steady_timer>(ioc_, close_policy_.close_after_ms);
    std::weak_ptr<WsServerSession> wss = ss;

    close_timer_->async_wait([wss](const boost::system::error_code& ec){
        if(ec) return;
        if(auto s = wss.lock())
            s->close();
    });
}

void TestServer::record_event_(EventType t){
    Event e{t, std::chrono::steady_clock::now()};
    {
        std::lock_guard<std::mutex> lk(events_mtx_);
        events_.push_back(e);
    }
    if(on_event_){
        on_event_(e);
    }
}
// int main(){
//     boost::asio::io_context io;
//     // tcp::acceptor acc(io, {tcp::v4(), 12345});
//     auto srv = TestServer(io, 12345);
//     srv.start();
//     io.run();
//     return 0;
// }