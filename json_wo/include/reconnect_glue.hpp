#ifndef RECONNECT_GLUE_HPP
#define RECONNECT_GLUE_HPP

#include "reconnect.hpp"
#include "ws_client.hpp"
#include "session.hpp"
#include "signals.hpp"
#include <iostream>
struct ReconnectGlue : public std::enable_shared_from_this<ReconnectGlue> {
    std::shared_ptr<WsClient> client_;
    // std::shared_ptr<Session> ss_;
    boost::asio::io_context& io_;

    std::shared_ptr<ReconnectSignals> rS;
    struct ReconnectPolicy pol;
    std::unique_ptr<ReconnectController> rC;

    std::function<void(bool ok)> async_connect_cb;
    std::atomic<uint64_t> next_token{1};//this is the fucking counter
    std::mutex timers_mtx;
    std::unordered_map<uint64_t, std::shared_ptr<boost::asio::steady_timer>> timers;

    /**
     * @brief Factory function to construct a ReconnectGlue instance safely.
     *
     * This creates the object under shared ownership and then performs a second-phase
     * initialization (init()) that depends on enable_shared_from_this being active.
     * In particular, init() captures a weak_ptr to this instance in asynchronous
     * callbacks (timers, transport close handlers) to avoid use-after-free if the
     * callbacks fire after the ReconnectGlue has been destroyed.
     *
     * @param c Shared WebSocket client used as the underlying transport.
     * @param s Shared session object providing io_context access and OCPP session logic.
     * @return std::shared_ptr<ReconnectGlue> Fully initialized instance.
     *
     * @note Do not instantiate ReconnectGlue directly; its constructor is private to
     *       enforce correct lifetime management for asynchronous handlers.
     */
    static std::shared_ptr<ReconnectGlue> create(std::shared_ptr<WsClient> c, boost::asio::io_context& io){
        auto p = std::shared_ptr<ReconnectGlue>(new ReconnectGlue(std::move(c), io, PrivateTag{}));
        p->init();
        return p;
    }

private:
    struct PrivateTag {}; //Tag type to prevent public construction
    struct TransportOps tOps;

    /**
     * @brief Second-phase initialization performed after shared ownership exists.
     *
     * This method must be called only after the instance is managed by a
     * std::shared_ptr (typically via create()) so that weak_from_this() produces a
     * valid weak_ptr. It initializes the TransportOps table with handlers that:
     * - open/close the underlying transport,
     * - schedule/cancel reconnect timers, and
     * - query the current steady-clock time,
     * while guarding access to the timer map with a mutex.
     *
     * All asynchronous callbacks capture a weak_ptr to this instance and lock it
     * before touching members, preventing use-after-free if callbacks run after
     * destruction. Finally, it constructs the ReconnectController and wires the
     * transport close signal to notify the controller.
     */
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
                // auto t = std::make_shared<boost::asio::steady_timer>(s->ss_->io, delay);
                auto t = std::make_shared<boost::asio::steady_timer>(s->io_, delay);
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

    /**
     * @brief Bridges ReconnectController with the concrete transport (WsClient) and session (Session).
     *
     * ReconnectGlue owns/wires:
     * - a transport implementation via TransportOps (open/close + timer scheduling/cancel),
     * - reconnect state machine logic via ReconnectController, and
     * - externally visible reconnect events via ReconnectSignals.
     *
     * Lifetime notes:
     * - Instances should be created via create() to ensure init() runs after shared ownership exists.
     * - Asynchronous callbacks capture a weak_ptr to this instance and lock it before accessing members,
     *   preventing use-after-free if callbacks execute after ReconnectGlue destruction.
     */
    ReconnectGlue(std::shared_ptr<WsClient> c, boost::asio::io_context& io, PrivateTag t) :
        client_(std::move(c)),
        io_(io),
        rS(std::make_shared<ReconnectSignals>()){}
};

#endif // RECONNECT_GLUE_HPP