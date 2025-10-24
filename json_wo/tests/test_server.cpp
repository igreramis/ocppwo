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

                ss = std::make_shared<WsServerSession>(std::move(s));
                std::weak_ptr<WsServerSession> wss = ss;
                ss->on_message([this, wss](std::string_view msg){
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
                            }
                            else if( c.action == "BootNotification" ) {
                                last_boot_msg_id_ = c.messageId;
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
                        ss->send(reply);
                    });
                });

                ss->on_close([this](){
                    std::cout << "Client disconnected\n";
                    std::lock_guard<std::mutex> lock(mtx_);
                    client_disconnected = true;
                });

                ss->start();
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
// int main(){
//     boost::asio::io_context io;
//     // tcp::acceptor acc(io, {tcp::v4(), 12345});
//     auto srv = TestServer(io, 12345);
//     srv.start();
//     io.run();
//     return 0;
// }