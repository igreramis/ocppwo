#ifndef RECONNECT_HPP
#define RECONNECT_HPP

#include <boost/asio.hpp>
#include <chrono>
#include <functional>
#include <iostream>

using namespace std;

struct ReconnectPolicy {
  std::chrono::milliseconds initial_backoff{500};
  std::chrono::milliseconds max_backoff{30'000};
  float jitter_ratio{0.20f};
  std::chrono::milliseconds resume_delay{250};
};

enum class ConnState { Disconnected, Connecting, Connected, Closing };
enum class CloseReason { Clean, TransportError, ProtocolError };

struct ReconnectSignals {
  std::function<void()> on_connecting;
  std::function<void()> on_connected;
  std::function<void(CloseReason)> on_closed;
  std::function<void()> on_online;
  std::function<void()> on_offline;
  std::function<void(std::chrono::milliseconds)> on_backoff_scheduled;
};

struct TransportOps {
  std::function<void(const std::string& url, std::function<void(bool ok)>)> async_connect;
  std::function<void(std::function<void()>)> async_close;
  std::function<void(std::chrono::milliseconds, std::function<void()>)> post_after;
  std::function<std::chrono::steady_clock::time_point()> now;
};

class ReconnectController {
    public:
        ReconnectController(ReconnectPolicy r, TransportOps t, ReconnectSignals rs= {})
         :rp(r), tOps(t), rSigs(rs) {};
        void start(const std::string& url){
            cout<<"Reconnect starting..."<<"\n";
            tOps.async_connect(url, [this](bool ok){
                if(ok)
                    cS_ = ConnState::Connected;
            });

        };
            //rc->start(url);
            //tops_.async_connect(url, [](bool ok){ if ok state = Connected});
        void stop(){
            ;
        };
        void on_transport_open(){
            ;
        };
        void on_transport_close(CloseReason cR){
            (void)cR;
        };
        void on_boot_accepted(){
            ;
        };
        bool can_send() const{
            return true;
        };
        ConnState state() const{
            return cS_;
        };
        bool online() const{
            return cS_ == ConnState::Connected;
        };
        int attempt() const{
            return 0;
        };
        std::chrono::milliseconds current_backoff() const{
            return std::chrono::milliseconds(0);
        };
    private:
        ReconnectPolicy rp;
        TransportOps tOps;
        ReconnectSignals rSigs;
        ConnState cS_{ConnState::Disconnected};
};

#endif // RECONNECT_HPP