#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
namespace boost::asio { class io_context; }

struct WsClient;
class Session;
struct SessionSignals;//fwd decl
struct Transport;


class ClientLoop {
    public:
    enum class State : std::uint8_t { Offline, Connecting, Online };
    struct Config {
        std::string host = "127.0.0.1";
        unsigned short port = 0;
        std::string url; //optional; if empty, a ws://host:port style URL can be built by caller
    };

    //functors for WsClient and Session
    struct Factories {
        std::function<std::shared_ptr<WsClient>(boost::asio::io_context&, std::string host, std::string port)> make_transport;
        std::function<std::unique_ptr<Session>(boost::asio::io_context&, std::shared_ptr<Transport>, std::shared_ptr<SessionSignals>)> make_session;
    };

    ClientLoop(boost::asio::io_context& ioc, Config cfg, Factories f);
    ~ClientLoop();

    //delete copy ctor/assignment. why? because of the internal shared_ptr to impl_ later below.
    ClientLoop(const ClientLoop&) = delete;
    ClientLoop& operator=(const ClientLoop&) = delete;

    void start();
    void stop();

    // Probes for tests
    State state() const;
    std::uint64_t connect_attempts() const;
    std::uint64_t online_transitions() const;

private:
    //pimpl idiom for reducing build times and encapsulating implementation details
    struct Impl;
    std::shared_ptr<Impl> impl_;
};