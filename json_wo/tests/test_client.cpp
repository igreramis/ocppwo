#include "test_client.hpp"

TestClient::TestClient(boost::asio::io_context& io, std::string host, std::string port)
    : io_(io), host_(std::move(host)), port_(std::move(port))
{
    client_ = std::make_shared<WsClient>(io_, host_, port_, metrics_);
    sS_ = std::make_shared<SessionSignals>(SessionSignals{
        //on_boot_accepted
        [this](){
            std::cout << "SessionSignals: BootNotification accepted\n";
            rcg_->rC->on_boot_accepted();
        }
    });

    ss_ = std::make_shared<Session>(io_, client_, sS_, metrics_);
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
        state_.store(TestClientState::Disconnected, std::memory_order_relaxed);
    };

    rcg_->rS->on_connecting = [this](){
        std::cout << "ReconnectController: websocket connecting..." << "\n";
        connect_attempts_.fetch_add(1, std::memory_order_relaxed);
        state_.store(TestClientState::Connecting, std::memory_order_relaxed);
    };

    rcg_->rS->on_connected = [this](){
        std::cout << "ReconnectController: websocket connected" << "\n";
        ss_->on_transport_connected();
    };

    rcg_->rS->on_online = [this](){
        online_ = true;
        state_.store(TestClientState::Connected, std::memory_order_relaxed);
        online_transitions_.fetch_add(1, std::memory_order_relaxed);
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
    ss_->on_transport_connected();
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

void TestClient::send_boot(std::function<void(const OcppFrame&)> cb) {
    ss_->send_call(BootNotification{"ModelA", "VendorA"}, std::move(cb));
}

void TestClient::send_authorize(std::function<void(const OcppFrame&)> cb) {
    ss_->send_call(Authorize{"ABC123"}, std::move(cb));
}

TestClientState TestClient::state() const {
    return state_.load(std::memory_order_relaxed);
}
uint64_t TestClient::connect_attempts() const {
    return connect_attempts_.load(std::memory_order_relaxed);
}
uint64_t TestClient::online_transitions() const {
    return online_transitions_.load(std::memory_order_relaxed);
}