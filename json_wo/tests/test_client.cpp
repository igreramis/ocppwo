#include "test_client.hpp"

TestClient::TestClient(boost::asio::io_context& io, std::string host, std::string port)
    : io_(io), host_(std::move(host)), port_(std::move(port))
{
    client_ = std::make_shared<WsClient>(io_, host_, port_);
    ss_ = std::make_shared<Session>(io_, client_);
    // rcg_ = ReconnectGlue::create(client_, ss_);
    rcg_ = ReconnectGlue::create(client_, io_);
    wss_ = ss_;

    std::cout << "WebSocket client starting...\n";

    rcg_->rS->on_closed = [this](CloseReason cR){
        std::cout << "ReconnectController: websocket closed" << "\n";
        ss_->on_close();
    };

    rcg_->rS->on_offline = [this](){
        std::cout << "ReconnectController: websocket offline" << "\n";
        online_ = false;
    };

    rcg_->rS->on_connected = [this](){
        std::cout << "ReconnectController: websocket connected" << "\n";
        ss_->send_call(BootNotification{"X100", "OpenAI"},
        [this](const OcppFrame& f){
            if( std::holds_alternative<CallResult>(f) ) {
                auto r = std::get<CallResult>(f);
                std::cout << "BootNotificationResponse: "<< r.payload << "\n";
                BootNotificationResponse resp = r.payload;
                if (resp.status == "Accepted") {

                    rcg_->rC->on_boot_accepted();
                    
                    std::cout << "BootNotification accepted, current time: " << resp.currentTime << "\n";
                    if (auto ss = wss_.lock()) {
                        ss->state = Session::State::Ready;
                        heartbeat_interval_s = resp.interval;
                    }
                    else {
                        std::cerr << "Session already destroyed, cannot set state to Ready\n";
                    }
                } else if (resp.status == "Pending") {
                    std::cout << "BootNotification pending, interval: " << resp.interval << "\n";
                } else {
                    std::cout << "BootNotification rejected\n";
                }
            } else if (std::holds_alternative<CallError>(f)){
                const auto& e = std::get<CallError>(f);
                std::cerr << "BootNotification Error: " << e.errorDescription << "\n";
            }
        });
    };

    rcg_->rS->on_online = [this](){
        online_ = true;
    };

    rcg_->rS->on_backoff_scheduled = [this](std::chrono::milliseconds backoff){
        backoff_scheduled_ = true; backoff_in_ms = backoff.count();
        std::cout << "ReconnectController: backoff scheduled for " << backoff.count() << " ms\n";
    };
}

void TestClient::start(void) {
    // client_->start();
    rcg_->rC->start(host_+port_);
}

void TestClient::close(void) {
    rcg_->rC->stop();
}

void TestClient::trigger_boot(){
    ss_->send_call(BootNotification{"X100", "OpenAI"},
        [this](const OcppFrame& f){
            if( std::holds_alternative<CallResult>(f) ) {
                auto r = std::get<CallResult>(f);
                std::cout << "BootNotificationResponse: "<< r.payload << "\n";
                BootNotificationResponse resp = r.payload;
                if (resp.status == "Accepted") {
                    std::cout << "BootNotification accepted, current time: " << resp.currentTime << "\n";
                    if (auto ss = wss_.lock()) {
                        ss->state = Session::State::Ready;
                        // start_heartbeat(resp.interval);
                        // ss->start_heartbeat(resp.interval);
                    }
                    else {
                        std::cout << "Session already destroyed, cannot set state to Ready\n";
                    }
                }
                else {
                    std::cout << "BootNotification not accepted, status: " << resp.status << "\n";
                }
            }
            else if( std::holds_alternative<CallError>(f) ) {
                auto e = std::get<CallError>(f);
                std::cout << "BootNotification Error: " << e.errorDescription << "\n";
            }
        });
}

unsigned TestClient::transport_max_writes_in_flight() const {
    return client_->transport_max_writes_in_flight();
}

unsigned TestClient::transport_max_write_queue_depth() const {
    return client_->transport_max_write_queue_depth();
}

unsigned TestClient::transport_current_write_queue_depth() const {
    return client_->transport_current_write_queue_depth();
}

bool TestClient::is_online() const { return online_; }

void TestClient::enable_heartbeats(bool enable) {
    if( enable ) {
        ss_->start_heartbeat(heartbeat_interval_s);
    }
    else {
        ss_->start_heartbeat(0);
    }
}
