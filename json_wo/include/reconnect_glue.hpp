#ifndef RECONNECT_GLUE_HPP
#define RECONNECT_GLUE_HPP

#include "reconnect.hpp"
#include "ws_client.hpp"
#include "session.hpp"
#include "signals.hpp"
#include <iostream>
struct ReconnectGlue : public std::enable_shared_from_this<ReconnectGlue> {
    std::shared_ptr<WsClient> client_;
    std::shared_ptr<Session> ss_;

    std::shared_ptr<ReconnectSignals> rS;
    struct ReconnectPolicy pol;
    std::unique_ptr<ReconnectController> rC;

    std::function<void(bool ok)> async_connect_cb;
    std::atomic<uint64_t> next_token{1};//this is the fucking counter
    std::mutex timers_mtx;
    std::unordered_map<uint64_t, std::shared_ptr<boost::asio::steady_timer>> timers;

    static std::shared_ptr<ReconnectGlue> create(std::shared_ptr<WsClient> c, std::shared_ptr<Session> s){
        auto p = std::shared_ptr<ReconnectGlue>(new ReconnectGlue(std::move(c), std::move(s), PrivateTag{}));
        p->init();
        return p;
    }
private:
    struct PrivateTag {};
    struct TransportOps tOps;
    void init(){
        std::weak_ptr<ReconnectGlue> wk = weak_from_this();
        tOps = TransportOps {
            [wk](const std::string& url, std::function<void(bool ok)> cb){
                //deal with return values from ws client
                if( auto s = wk.lock() )
                {
                    s->client_->on_connected([cb](){
                        std::cout<<"tOps: async_open->connected\n";
                        cb(true);
                    });
                    //if transpot signals on_close when client is trying to open it, what
                    //do you do?
                    //-schedule reconnect
                    //-
                    auto prev = s->client_->on_closed_;
                    std::shared_ptr<bool> completed = std::make_shared<bool>(false);
                    s->client_->on_close([cb, prev, completed](){
                        std::cout<<"tOps: async_open->close entered\n";
                        if(prev) prev();
                        if(*completed) return;
                        cb(false);
                        std::cout<<"tOps: async_open->close finished\n";
                    });

                    s->client_->start();
                }
                else
                {
                    cb(false);
                }
            },
            [wk](std::function<void()> cb){
                if( auto s = wk.lock() )
                {
                    auto prev = s->client_->on_closed_;
                    std::shared_ptr<bool> completed = std::make_shared<bool>(false);
                    //what would be the point of passing these by reference?
                    s->client_->on_close([cb, prev, completed](){
                        std::cout<<"tOps: async_close->close entered\n";
                        if(prev) prev();
                        std::cout<<"tOps: post prev() call\n";
                        if(*completed) return;
                        *completed = true;
                        cb();
                        std::cout<<"tOps: async_close->close finished\n";
                    });
                    s->client_->close();

                }
            },
            [wk](std::chrono::milliseconds delay, std::function<void()> cb) -> uint64_t {
                auto s = wk.lock();
                if(!s) return 0; 
                uint64_t id = s->next_token.fetch_add(1, std::memory_order_relaxed);
                auto t = std::make_shared<boost::asio::steady_timer>(s->ss_->io, delay);
                //why make_shared?
                {
                    std::lock_guard<std::mutex> lg(s->timers_mtx);
                    s->timers.emplace(id, t);
                }
                t->async_wait([wk, id, cb](auto ec){
                    bool invoke = false;
                    if( auto s = wk.lock() )
                    {
                        std::lock_guard<std::mutex> lg(s->timers_mtx);
                        auto it = s->timers.find(id);
                        if(it != s->timers.end())
                        {
                            s->timers.erase(it);
                        }
                        invoke = (ec == boost::system::errc::success);                    
                    }
                    if(invoke) cb();
                });

                return id;
            },
            [wk](uint64_t id)->bool{
                //how do we cancel the fuck? take the id, look it up in the ds
                auto s = wk.lock();
                if( !s ) return false;

                std::shared_ptr<boost::asio::steady_timer> t;
                {
                    std::lock_guard<std::mutex> lg(s->timers_mtx);
                    auto it = s->timers.find(id);
                    if( it == s->timers.end() ) return false;
                    t = it->second;
                    s->timers.erase(it);
                }
                t->cancel();
                return true;
            },
            [](){
                return std::chrono::steady_clock::now();
            }

        };
        
        rC = std::make_unique<ReconnectController>(pol, tOps, rS);

        client_->on_close([wk](){
            auto s = wk.lock();
            if(!s) return;

            s->rC->on_transport_close(CloseReason::TransportError);
        });
    }
    
    ReconnectGlue(std::shared_ptr<WsClient> c, std::shared_ptr<Session> s, PrivateTag t) :
        client_(std::move(c)),
        ss_(std::move(s)),
        rS(std::make_shared<ReconnectSignals>()){}
        
    // ReconnectGlue(std::shared_ptr<WsClient> c, std::shared_ptr<Session> s, PrivateTag t)
    //     : client_(c), 
    //     ss_(s),
    //     rS(std::make_shared<ReconnectSignals>()),
    //     rC(pol, tOps, rS) { 
    //         //
    //         client_->on_close([this](){
    //             rC.on_transport_close(CloseReason::TransportError);
    //         });
    //     }

    // struct TransportOps tOps{
    //     [&](const std::string& url, std::function<void(bool ok)> cb){
    //         //deal with return values from ws client
    //         client_->on_connected([cb](){
    //             std::cout<<"tOps: async_open->connected\n";
    //             cb(true);
    //         });
    //         //if transpot signals on_close when client is trying to open it, what
    //         //do you do?
    //         //-schedule reconnect
    //         //-
    //         auto prev = client_->on_closed_;
    //         std::shared_ptr<bool> completed = std::make_shared<bool>(false);
    //         client_->on_close([cb, prev, completed](){
    //             std::cout<<"tOps: async_open->close entered\n";
    //             if(prev) prev();
    //             if(*completed) return;
    //             cb(false);
    //             std::cout<<"tOps: async_open->close finished\n";
    //         });

    //         client_->start();
    //     },
    //     [&](std::function<void()> cb){
    //         auto prev = client_->on_closed_;
    //         std::shared_ptr<bool> completed = std::make_shared<bool>(false);
    //         //what would be the point of passing these by reference?
    //         client_->on_close([cb, prev, completed](){
    //             std::cout<<"tOps: async_close->close entered\n";
    //             if(prev) prev();
    //             std::cout<<"tOps: post prev() call\n";
    //             if(*completed) return;
    //             *completed = true;
    //             cb();
    //             std::cout<<"tOps: async_close->close finished\n";
    //         });
    //         client_->close();
    //     },
    //     [&](std::chrono::milliseconds delay, std::function<void()> cb) -> uint64_t {
    //         uint64_t id = next_token.fetch_add(1, std::memory_order_relaxed);
    //         auto t = std::make_shared<boost::asio::steady_timer>(ss_->io, delay);
    //         //why make_shared?
    //         {
    //             std::lock_guard<std::mutex> lg(timers_mtx);
    //             timers.emplace(id, t);
    //         }
    //         //what if by the time the lambda is fucking executed, rcg no longer exists?
    //         //if we use weak ptr to capture rcg, then we need to ensure that we create 
    //         //a dynamic shared_ptgr instance of rcg rather than a stack instance
    //         std::weak_ptr<ReconnectGlue> wk = weak_from_this();
    //         t->async_wait([wk, id, cb](auto ec){
    //             //what to do once the time has expired?
    //             //execute the cb. you pass on to async_wait the cb. now its upto the fucking async_wait to keep track of it.
    //             //you have xX instances of steady_timer, each created dynamically and each instance knows which fucking cb
    //             //to call.
    //             bool invoke = false;
    //             if( auto s = wk.lock() )
    //             {
    //                 std::lock_guard<std::mutex> lg(s->timers_mtx);
    //                 auto it = s->timers.find(id);
    //                 if(it != s->timers.end())
    //                 {
    //                     s->timers.erase(it);
    //                 }
    //                 invoke = (ec == boost::system::errc::success);                    
    //             }
    //             if(invoke) cb();
    //         });

    //         return id;
    //     },
    //     [this](uint64_t id)->bool{
    //         //how do we cancel the fuck? take the id, look it up in the ds
    //         std::shared_ptr<boost::asio::steady_timer> t;
    //         {
    //             std::lock_guard<std::mutex> lg(timers_mtx);
    //             auto it = timers.find(id);
    //             if( it == timers.end() ) return false;
    //             t = it->second;
    //             timers.erase(it);
    //         }
    //         t->cancel();
    //         return true;
    //     },
    //     [&](){
    //         return std::chrono::steady_clock::now();
    //     }
    // };
};

#endif // RECONNECT_GLUE_HPP