#ifndef RECONNECT_GLUE_HPP
#define RECONNECT_GLUE_HPP

#include "reconnect.hpp"
#include "ws_client.hpp"
#include "session.hpp"
#include "signals.hpp"
#include <iostream>
struct ReconnectGlue{
    std::shared_ptr<WsClient> client_;
    std::shared_ptr<Session> ss_;

    std::function<void(bool ok)> async_connect_cb;
    
    ReconnectGlue(std::shared_ptr<WsClient> c, std::shared_ptr<Session> s)
        : client_(c), 
        ss_(s),
        rS(std::make_shared<ReconnectSignals>()),
        rC(pol, tOps, rS) { 
            //
            client_->on_close([this](){
                rC.on_transport_close(CloseReason::TransportError);
            });
        }

    struct ReconnectPolicy pol;
    struct TransportOps tOps{
        [&](const std::string& url, std::function<void(bool ok)> cb){
            //deal with return values from ws client
            client_->on_connected([cb](){
                std::cout<<"tOps: async_open->connected\n";
                cb(true);
            });
            //if transpot signals on_close when client is trying to open it, what
            //do you do?
            //-schedule reconnect
            //-
            auto prev = client_->on_closed_;
            std::shared_ptr<bool> completed = std::make_shared<bool>(false);
            client_->on_close([cb, prev, completed](){
                std::cout<<"tOps: async_open->close entered\n";
                if(prev) prev();
                if(*completed) return;
                cb(false);
                std::cout<<"tOps: async_open->close finished\n";
            });

            client_->start();
        },
        [&](std::function<void()> cb){
            auto prev = client_->on_closed_;
            std::shared_ptr<bool> completed = std::make_shared<bool>(false);
            //what would be the point of passing these by reference?
            client_->on_close([cb, prev, completed](){
                std::cout<<"tOps: async_close->close entered\n";
                if(prev) prev();
                std::cout<<"tOps: post prev() call\n";
                if(*completed) return;
                *completed = true;
                cb();
                std::cout<<"tOps: async_close->close finished\n";
            });
            client_->close();
        },
        [&](std::chrono::milliseconds delay, std::function<void()> cb){
            std::unique_ptr<boost::asio::steady_timer> t =
                std::make_unique<boost::asio::steady_timer>(ss_->io, delay);
            t->async_wait([t=std::move(t), cb](auto ec){
                if(!ec) cb();
            });
        },
        [&](){
            return std::chrono::steady_clock::now();
        }
    };
    // struct ReconnectSignals rS {};
    //why should ReconnectSignals be a shared_ptr?
    //if reconnect_glue instance dies out but reconnectController instance still
    //lives on, the latter would still have access to all the callbacks assigned
    //in ReconnectSignals.
    //An alternative could be passing by value, which is totally fine.
    //One thing to note is when you capture variables by reference, then passing by
    //values is changing the same underlying objects.
    //if you pass by value a parameter, but then use the std::move semantic, how would
    //things change? pass by value makes a copy of the parameter. std::move matters if
    //we pass in the parameter as an r-value(create it at the call site in passing to the
    //function as input) rather than an l-value(create an instance of it locally first
    //and then pass the variable containing the instance as an input to the function).)
    std::shared_ptr<ReconnectSignals> rS;
    ReconnectController rC;
    // ReconnectController rc{pol, tOps, rS};
};

#endif // RECONNECT_GLUE_HPP