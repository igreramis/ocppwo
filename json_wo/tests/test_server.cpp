#include "test_server.hpp"

TestServer::TestServer(asio::io_context& ioc, unsigned short port):
    ioc_(ioc), port_(port), ws_(ioc), acc_(ioc, tcp::endpoint(tcp::v4(), port)){
    // router_.addHandler<BootNotification>(BootNotificationHandler);
    router_.addHandler<BootNotification>([this](const BootNotification& p, const std::string& s){
        return this->TestBootNotificationHandler(p, s);
    });
    router_.addHandler<Authorize>(AuthorizeHandler);
    router_.addHandler<HeartBeat>(HeartBeatHandler);
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
                        }
                    }
                });

                ss->on_call([this, wss](const Call& c, std::function<void(const OcppFrame& f)> respond){
                    std::cout << __func__ << "Received Call: " << c.action << "\n";
                    OcppFrame reply = router_.route(c);
                    if( c.action == "BootNotification" ) {
                        last_boot_msg_id_ = c.messageId;
                    }
                    respond(reply);
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

// int main(){
//     boost::asio::io_context io;
//     // tcp::acceptor acc(io, {tcp::v4(), 12345});
//     auto srv = TestServer(io, 12345);
//     srv.start();
//     io.run();
//     return 0;
// }