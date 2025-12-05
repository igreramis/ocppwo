#include "test_harness.hpp"

TestHarness::TestHarness(boost::asio::io_context& io, std::string host, unsigned short port)
    : client_(io, host, std::to_string(port)), server_(io, port)
{
    server_start = [&](){
        server_.start();
    };

    server_force_close = [&](){
        // if( auto ss = server_.ss ) {
        //     ss->close(websocket::close_code::normal);
        // }
        ;
        server_.stop();
    };

    server_close_on_accept = [&](){
        // server_.acc_.close();
        ;
    };

    server_close_after_first_msg = [&](){
        // server_.ss->on_message([this](std::string_view){
        //     server_.ss->close(websocket::close_code::normal);
        // });
        ;
    };

    clear_telemetry = [&](){
        // std::lock_guard<std::mutex> lock(server_.mtx_);
        // server_.heartbeats_.clear();
        ;
    };

    client_start = [&](){
        client_.start();
        ;
    };

    client_force_close = [&](){
        client_.close();
    };

    client_send_ping = [&](){
        // client_.send_ping();
        ;
    };

    client_enable_heartbeats = [&](){
        client_.enable_heartbeats(true);
    };

    trigger_boot = [&](){
        client_.trigger_boot();
    };

    accept_boot = [&](){
        // server_.accept_boot();
        ;
    };
}

bool TestHarness::is_client_online() const {
    return client_.is_online();
}