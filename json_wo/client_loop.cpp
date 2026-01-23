#include "client_loop.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <chrono>
#include "ws_client.hpp"
#include "session.hpp"
#include "reconnect.hpp"
#include "reconnect_glue.hpp"

//so that we can pass on shared_ptr instances of this to callbacks, we make Impl inherit from enable_shared_from_this.
//without this inheritance, calling shared_from_this() would throw an exception.
struct ClientLoop::Impl : std::enable_shared_from_this<ClientLoop::Impl> {
    //members
    boost::asio::io_context& ioc;
    Config cfg;
    Factories f;

    std::shared_ptr<ReconnectSignals> sigs = std::make_shared<ReconnectSignals>();
    std::shared_ptr<ReconnectController> rc; //why shared_ptr? because the controller uses weak_from_this()
    std::shared_ptr<WsClient> transport; //why shared_ptr? because we need to pass shared_ptr instances to call backs
    std::shared_ptr<SessionSignals> session_sigs = std::make_shared<SessionSignals>();
    std::shared_ptr<Session> session; //why unique_ptr? because only Impl owns the session. but this creates a problem when writing unit tests. hence make it shared_ptr.

    // Probe state
    //why are the following atomics? 
    // they can be used in a multithreading scenario:
    //  -the async_callbacks where these variables are set could be running in different threads or they are
    //  all running in one thread and the test is running in a different thread which would mean that these variables are
    //  being read from a different thread.
    // if used in a multithreading scenario without atomic or lock protection, we could have
    //  -data race situation
    //    -reads might get half updated and half old values(torn reads/writes)
    //    -one thread might not see the updated value written by another thread(visibility issues)
    //commong pattern in ASIO tests
    //run io.poll()/io.run_for() in the same thread as assertions
    //only check probes onces you've run the above.
    //if tests read probes while the event loop might still be running elsewhere: keep atomic
    //if everything is strictly single-threaded and you only read after pumping IO: non-atomic is OK. 
    std::atomic<uint64_t> connect_attempts_{0};
    std::atomic<uint64_t> online_transitions_{0};
    std::atomic<State> state_{State::Offline};

    std::atomic<uint64_t> next_token{1};
    std::mutex timers_mtx;
    std::unordered_map<uint64_t, std::shared_ptr<boost::asio::steady_timer>> timers;

    // Close completion hook for TransportOps::async_close
    // std::mutex close_mtx;
    // std::function<void()> pending_close_done;

    Impl (boost::asio::io_context& io, Config c, Factories factories)
        : ioc(io), cfg(std::move(c)), f(std::move(factories)) {}

    void set_state(State s) {
        state_.store(s, std::memory_order_relaxed);
    }

    void reset_connection_objects() {
        // session.reset();
        // transport.reset();
    }

    TransportOps make_transport_ops() {
        //get a weak ptr to this so that we can use it in the lambdas without forcing shared ownership
        std::weak_ptr<Impl> wk = weak_from_this();
        
        TransportOps tOps = TransportOps{
            [wk](const std::string& url, std::function<void(bool ok)> cb){
                //deal with return values from ws client
                auto s = wk.lock();
                if( !s )
                {
                    cb(false);
                    return;
                }

                s->transport = s->f.make_transport(s->ioc, s->cfg.host, std::to_string(s->cfg.port));
                if( !s->transport )
                {
                    cb(false);
                    return;
                }

                s->transport->on_connected([wk, cb](){
                    std::cout<<"client loop: async_open->connected\n";
                    if( auto s = wk.lock() )
                    {
                        //create session
                        if( s->f.make_session )
                        {
                            s->session = s->f.make_session(s->ioc, s->transport, s->session_sigs);
                        }
                    }
                    cb(true);
                });
                //if transpot signals on_close when client is trying to open it, what
                //do you do?
                //-schedule reconnect
                //-
                s->transport->on_close([wk, cb](){
                    cb(false);
                    std::cout<<"tOps: async_open->close finished\n";
                    //you need to pass reconnect here.

                    
                    if( auto s = wk.lock() )
                    {
                        s->session->on_close();
                        s->rc->on_transport_close(CloseReason::TransportError);
                    }
                });

                s->transport->on_message([wk](std::string_view sv){
                    if(auto s = wk.lock() )
                    {
                        s->session->on_message(sv);
                    }
                });

                s->transport->start();

            },
            [wk](std::function<void()> cb){
                auto s = wk.lock();
                if(!s){ return; }
                cb();
                s->transport->close();
            },
            [wk](std::chrono::milliseconds delay, std::function<void()> cb) -> uint64_t {
                auto s = wk.lock();
                if (!s) return 0;
                uint64_t id = s->next_token.fetch_add(1, std::memory_order_relaxed);
                auto t = std::make_shared<boost::asio::steady_timer>(s->ioc, delay);
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

        return tOps;
    }

    void wire_signals() {
        std::weak_ptr<Impl> wk = weak_from_this();

        session_sigs->on_boot_accepted = [wk] {
            ;;//something here...
            if( auto self = wk.lock() )
            {
                self->online_transitions_.fetch_add(1, std::memory_order_relaxed);
                self->set_state(State::Online);
                self->rc->on_boot_accepted();
            }
        };

        sigs->on_connecting = [wk] {
            if( auto self = wk.lock() ) {
                self->set_state(State::Connecting);
            }
        };

        sigs->on_connected = [wk] {
            if (auto self = wk.lock()) {
                self->set_state(State::Connecting);
                self->session->on_transport_connected();
            }
        };

        sigs->on_offline = [wk] {
            if( auto self = wk.lock()) {
                self->reset_connection_objects();
                self->set_state(State::Offline);
            }
        };

        //on_closed/on_backoff_scheduled omitted for now
    }

    void start_controller() {
        wire_signals();

        ReconnectPolicy pol;
        auto ops = make_transport_ops();
        rc = std::make_shared<ReconnectController>(pol, std::move(ops), sigs);

        set_state(State::Offline);

        const std::string url = !cfg.url.empty() ? cfg.url : "ws://" + cfg.host + ":" + std::to_string(cfg.port);
        rc->start(url);
    }

    void stop_controller() {
        {
            std::lock_guard<std::mutex> lg(timers_mtx);
            for(auto & [_, t] : timers ) t->cancel();
            timers.clear();
        }
        
        if( rc ) rc->stop();
        reset_connection_objects();
        set_state(State::Offline);
    }
};



ClientLoop::ClientLoop(boost::asio::io_context& ioc, Config cfg, Factories f)
    :impl_(std::make_shared<Impl>(ioc, std::move(cfg), std::move(f))) {}

ClientLoop::~ClientLoop() {
    if( impl_ ) impl_->stop_controller();
}

void ClientLoop::start() {
    impl_->start_controller();
}

void ClientLoop::stop() {
    impl_->stop_controller();
}

ClientLoop::State ClientLoop::state() const { return impl_->state_.load(std::memory_order_relaxed); }
std::uint64_t ClientLoop::connect_attempts() const { return impl_->connect_attempts_.load(std::memory_order_relaxed); }
std::uint64_t ClientLoop::online_transitions() const { return impl_->online_transitions_.load(std::memory_order_relaxed); }