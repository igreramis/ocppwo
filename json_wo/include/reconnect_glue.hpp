#ifndef RECONNECT_GLUE_HPP
#define RECONNECT_GLUE_HPP

#include "reconnect.hpp"
#include "ws_client.hpp"
#include "session.hpp"

struct ReconnectGlue{
    std::shared_ptr<WsClient> client_;
    std::shared_ptr<Session> ss_;

    std::function<void(bool ok)> async_connect_cb;
    
    ReconnectGlue(std::shared_ptr<WsClient> c, std::shared_ptr<Session> s)
        : client_(c), ss_(s) { }

    struct ReconnectPolicy pol;
    struct TransportOps tOps{
        [&](const std::string& url, std::function<void(bool ok)> cb){
            //deal with return values.
            client_->on_connected([cb](){
                cb(true);
            });

            client_->on_close([cb](){
                cb(false);
            });

            client_->start();
        },
        [&](std::function<void()> cb){
            //once reply for BootNotificaiton recieved, set here state
            //to Connected
            client_->on_close([cb](){
                cb();
            });
            client_->close();
        },
        [&](std::chrono::milliseconds delay, std::function<void()> cb){
            std::unique_ptr<boost::asio::steady_timer> t =
                std::make_unique<boost::asio::steady_timer>(ss_->io, delay);
            t->async_wait([cb](auto ec){
                if(!ec) cb();
            });
        },
        [&](){
            return std::chrono::steady_clock::now();
        }
    };
    ReconnectController rc{pol, tOps};
};

#endif // RECONNECT_GLUE_HPP