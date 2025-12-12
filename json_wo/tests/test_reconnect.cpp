#include <gtest/gtest.h>
#include <chrono>
#include "test_server.hpp"
#include "reconnect.hpp"
#include "ws_client.hpp"
#include "session.hpp"
#include "reconnect_glue.hpp"
#include "test_harness.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;
using namespace std;
using Ms = std::chrono::milliseconds;

struct ClientUnderTest{
    //provide a way to start the client pointing to ws:127.0.0.1:port
    //and a way to stop it. This structd is a placeholder.
    std::shared_ptr<WsClient> client_;
    std::shared_ptr<Session> ss_;
    std::shared_ptr<ReconnectGlue> rcg_;
    std::weak_ptr<Session> wss_;
    unsigned transport_max_writes_in_flight() const {
        return client_->transport_max_writes_in_flight();
    }

    unsigned transport_max_write_queue_depth() const {
        return client_->transport_max_write_queue_depth();
    }

    unsigned transport_current_write_queue_depth() const {
        return client_->transport_current_write_queue_depth();
    }

    void start(boost::asio::io_context& io, std::string host, std::string port)
    {
        client_ = std::make_shared<WsClient>(io, host, port);
        ss_ = std::make_shared<Session>(io, client_); wss_= ss_;
        rcg_ = std::make_shared<ReconnectGlue>(client_, ss_);

        rcg_->rS->on_connected = [this](){
            std::cout << "ReconnectController: websocket connected" << "\n";
            ss_->send_call(BootNotification{"X100", "OpenAI"},
            [this](const OcppFrame& f){
                if( std::holds_alternative<CallResult>(f) ) {
                    auto r = std::get<CallResult>(f);
                    std::cout << "BootNotificationResponse: "<< r.payload << "\n";
                    BootNotificationResponse resp = r.payload;
                    if (resp.status == "Accepted") {

                        rcg_->rC.on_boot_accepted();
                        
                        std::cout << "BootNotification accepted, current time: " << resp.currentTime << "\n";
                        if (auto ss = wss_.lock()) {
                            ss->state = Session::State::Ready;
                            // start_heartbeat(resp.interval);
                            ss->start_heartbeat(resp.interval);
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

        rcg_->rS->on_closed = [this](CloseReason cR){
            std::cout << "ReconnectController: websocket closed" << "\n";
            ss_->on_close();
        };

        rcg_->rC.start(host+port);

        //if start returns status closed, it indicates event where while connecting
        //websocket, connection closed. this is different from an ongoing connection
        //getting closed.

    };

    void stop()
    {
        client_->close();
    };
};

struct TimerPump{
    struct Item{
        std::chrono::milliseconds delay;
        std::function<void()> cb;
    };
    std::vector<Item> timers;

    void post_after(std::chrono::milliseconds ms, std::function<void()> cb){
        timers.push_back({ms, std::move(cb)});
    }
    void run_all(){
        for(auto& timer: timers){timer.cb();}
        timers.clear();
    }
    template<class Pred>
    void run_while(Pred p){
        while(p() && !timers.empty()){
            auto it = timers.front();
            timers.erase(timers.begin());
            it.cb();
        }
    }

};

struct FakeClock{
    std::chrono::steady_clock::time_point now{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point operator()() const {
        return now;
    }
};

#if 0
TEST(Reconnect, TypesCompile){
    ReconnectPolicy pol;
    ConnState s = ConnState::Disconnected;
    CloseReason r = CloseReason::Clean;
    ReconnectSignals sigs;

}

TEST(Reconnect, OpsAreAssignable){
    TransportOps ops;
    ops.async_connect = [&](const std::string& url, std::function<void(bool ok)> done){
        ;
    };

    ops.async_close = [&](std::function<void()> closed){
        ;
    };

    ops.post_after = [&](std::chrono::milliseconds d, std::function<void()> cb){
        ;
    };

    ops.now = [](){
        return std::chrono::steady_clock::now();
    };
}


TEST(Reconnect, StartLeadsToConnecting){
    bool saw_connecting = false, saw_connected = false, went_online = false;
    TimerPump timers;
    ReconnectPolicy rP;
    std::shared_ptr<ReconnectSignals> rS = std::make_shared<ReconnectSignals>();
    rS->on_connecting = [&](){
        saw_connecting = true;
    };
    rS->on_connected = [&](){
        saw_connected = true;
    };
    rS->on_online = [&](){
        went_online = true;
    };

    TransportOps tO;
    tO.async_connect = [](const std::string& url, std::function<void(bool ok)> done){
        (void)url;
        done(true);
    };
    tO.post_after = [&](Ms delay, std::function<void()> cb){
        timers.post_after(delay, cb);
    };

    ReconnectController rc{rP, tO, rS};
    rc.start("ws://dummy");
    EXPECT_TRUE(saw_connecting);
    EXPECT_TRUE(saw_connected);
    EXPECT_EQ(rc.state(), ConnState::Connected);

    rc.on_boot_accepted();
    timers.run_all();
    EXPECT_TRUE(went_online);
    EXPECT_TRUE(rc.can_send());

    rc.on_transport_close(CloseReason::TransportError);
    EXPECT_EQ(rc.state(), ConnState::Disconnected);
    EXPECT_FALSE(rc.can_send());

    timers.run_all();
    rc.on_boot_accepted();
    EXPECT_TRUE(rc.can_send());
}
#endif

#if 0
TEST(Reconnect, NoDoubleSendAcrossReconnect) {
    boost::asio::io_context ioc;

    //Pick an ephemeral port
    unsigned short port = 0;
    {
        tcp::acceptor tmp(ioc, {tcp::v4(), 0});
        port = tmp.local_endpoint().port();
    }

    TestHarness tH(ioc, "127.0.0.1", port);

    tH.server_.set_boot_conf("boot_msg_id", 2);tH.server_start();
    tH.client_start();

    std::thread io_thread([&]{ ioc.run(); });

    std::string boot_id;
    for( int i = 0; i < 50; i++ ) { // upto ~5s(50*100ms)
        std::this_thread::sleep_for(100ms);
        boot_id = tH.server_.last_boot_msg_id();
        if( !boot_id.empty() ) break;
    }
    ASSERT_FALSE(boot_id.empty()) << "Client never sent BootNotification";
    //you check on the server if it got the BootNotification.
    //but who keeps track of where the client is at? maybe you should wait for the
    //client to get somewhere before sending this dump of packets?
    //or since this is a test, maybe you shouldn't send the heartbeats-make them programmable

    int N = 1000;
    for( int i = 0; i < N/2; i++ ) {
        json frame = json::array({2, std::to_string(i), "Ping", json::object({{"seq", i }})});
        tH.client_.client_->send(frame.dump());
    }

    std::this_thread::sleep_for(3s);
    
    tH.server_force_close();

    for( int i=0; i < 50; i++ )
    {
        std::this_thread::sleep_for(50ms);
        if( !tH.is_client_online() )
            break;
        if( tH.server_.is_client_disconnected() )
            break;
    }
    EXPECT_FALSE( tH.is_client_online() ) << "Client is still online";
    EXPECT_TRUE( tH.server_.is_client_disconnected()) << "Server did not disconnect client";


    auto frames = tH.server_.received();
    std::vector<int> seqs;
    for (const auto& frame : frames) {
        auto a = json::parse(frame.text);
        if( a.is_array() && a.size() > 3 && a[2] == "Ping" ) {
            seqs.push_back(a[3].value("seq", -1));
        }
    }
    ASSERT_EQ(seqs.size(), N/2) << "Server did not receive N/2 messages";
    for(int i=1; i<seqs.size(); i++) {
        ASSERT_EQ(seqs[i], seqs[i-1] + 1)<<"Messages out of order";
    }

    tH.server_.set_boot_conf("boot_msg_id", 2);tH.server_start();
    
    //wait for the client to reconnect again
    // tH.client_start();

    for( int i = 0; i < 50; i++ ) { // upto ~5s(50*100ms)
        std::this_thread::sleep_for(100ms);
        boot_id = tH.server_.last_boot_msg_id();
        if( !boot_id.empty() ) break;
    }
    ASSERT_FALSE(boot_id.empty()) << "Client never sent BootNotification on reconnect";

    //send remaining frames on reconnect
    for( int i = N/2; i < N; i++ ) {
        json frame = json::array({2, std::to_string(i), "Ping", json::object({{"seq", i }})});
        tH.client_.client_->send(frame.dump());
    }

    std::this_thread::sleep_for(3s);

    frames = tH.server_.received();
    seqs.clear();
    for (const auto& frame : frames) {
        auto a = json::parse(frame.text);
        if( a.is_array() && a.size() > 3 && a[2] == "Ping" ) {
            seqs.push_back(a[3].value("seq", -1));
        }
    }
    ASSERT_EQ(seqs.size(), N/2) << "Server did not receive N/2 messages after reconnect";
    for(int i=1;i<seqs.size();i++){
        ASSERT_EQ(seqs[i], seqs[i-1]+1)<<"Messages out of order after reconnect";
    }

    //force close the client connection
    tH.server_force_close();

    ioc.stop();
    if(io_thread.joinable()) io_thread.join();

}
#endif

//make first 4 connects fail → capture scheduled delays → 
//assert each is within jitter bounds and non-collapsing (e.g., `>= 0.6 * previous`) and never exceeds `max_backoff`.
//Note:the jitter bounds condition (>=0.6*previous) is based on the assumption of a jitter range of ±20%. Adjust as necessary.
TEST(Reconnect, BackoffGrowthAndCap) {
    //tH or something tells jme failed, with backup value for reconnect as :...
    unsigned count_reconnects = 0;
    TransportOps tOps{
        //async_connect
        [&](const std::string& url, std::function<void(bool ok)> done){
            (void)url;
            if( count_reconnects < 4 )
            {
                done(false); //always fail
            }
            else
            {
                done(true);
            }
        },
        //async_close
        [](std::function<void()> closed){
            closed();
        },
        //post_after
        [](Ms delay, std::function<void()> cb){
            cb(); //immediate execution for test
        },
        //now
        [](){
            return std::chrono::steady_clock::now();
        }
    };
    std::vector<int> recorded_backoffs;
    std::shared_ptr<ReconnectSignals> rS = std::make_shared<ReconnectSignals>(ReconnectSignals{
        // on_connecting
        [](){},
        // on_connected
        [](){},
        // on_closed
        [](CloseReason){},
        // on_online
        [](){},
        // on_offline
        [](){},
        // on_backoff_scheduled
        [&](std::chrono::milliseconds chrono_ms){
            count_reconnects++;
            std::cout << "Backoff scheduled for (ms): " << chrono_ms.count() << "\n";
            recorded_backoffs.push_back(chrono_ms.count());
        },
    });
    ReconnectPolicy rP;
    ReconnectController rC(rP, tOps, rS);
    rC.start("ws://dummy");//this causes reconnect attempts. hwo do you go from here?
    //i don't think you ever come down to the sleep thread part. never happens
    //the test as is does not seem to achieve what we are looking for.
    //it seems we need a timer, that would trigger reconnects after a time and thereby make the systme sleep one
    //thread and execute the other.
    // for (int i = 0; i < 10; i++) {
    //     std::this_thread::sleep_for(1ms);
    //     std::cout<<"recorded_backoffs.size(): "<<recorded_backoffs.size()<<"\n";
    // }
    ASSERT_EQ(recorded_backoffs.size(), 4) << "Expected 4 reconnects";

    for(int i = 1; i < recorded_backoffs.size(); i++)
    {
        ASSERT_LE(recorded_backoffs[i-1], rP.max_backoff.count()) << "Backoff exceeds max_backoff";
        ASSERT_LE(recorded_backoffs[i], rP.max_backoff.count()) << "Backoff exceeds max_backoff";
        ASSERT_GE(recorded_backoffs[i], recorded_backoffs[i-1]*0.6) << "jitter not within bounds";
    }
}

TEST(Reconnect, ResumeDelayPolicy) {
    //tH or something tells jme failed, with backup value for reconnect as :...
    unsigned count_reconnects = 0;
    std::function<void()> cb_;
    TransportOps tOps{
        //async_connect
        [&](const std::string& url, std::function<void(bool ok)> done){
            (void)url;
            done(true);
        },
        //async_close
        [](std::function<void()> closed){
            closed();
        },
        //post_after
        [&](Ms delay, std::function<void()> cb){
            cb_ = std::move(cb); //immediate execution for test
        },
        //now
        [](){
            return std::chrono::steady_clock::now();
        }
    };
    std::vector<int> recorded_backoffs;
    bool connected_ = false, online_ = false;
    std::shared_ptr<ReconnectSignals> rS = std::make_shared<ReconnectSignals>(ReconnectSignals{
        // on_connecting
        [](){},
        // on_connected
        [&](){connected_ = true;},
        // on_closed
        [](CloseReason){},
        // on_online
        [&](){
            online_ = true;
        },
        // on_offline
        [](){},
        // on_backoff_scheduled
        [&](std::chrono::milliseconds chrono_ms){
            count_reconnects++;
            std::cout << "Backoff scheduled for (ms): " << chrono_ms.count() << "\n";
            recorded_backoffs.push_back(chrono_ms.count());
        },
    });
    ReconnectPolicy rP;
    ReconnectController rC(rP, tOps, rS);
    rC.start("ws://dummy");
    ASSERT_TRUE(connected_) << "Should be connected initially";
    ASSERT_FALSE(online_) << "Should not be online unless boot notification accepted";

    rC.on_boot_accepted();
    ASSERT_FALSE(online_) << "Should still not be online after boot accepted but resume_delay pending";

    cb_();
    ASSERT_TRUE(online_) << "Should be online after resume_delay elapsed";
}
//we need to indicate to test server to ignore 4 connection attempts
//
/*
wsclient has a send() and start() api. it has completion signals
session has a start, send_call api. the latter has a completion callback
reconnect has a series of public apis. they report back via completion signals
    -internally, reconnect calls methods from wsclient and ties its completion signals
    with its own completion callbacks
    -
rescheduling feature:
    if connect !successful, fireup a timer with a fixed timeout and once it expires
    try connecting again
        timer = std::make_unique<boost::asio::steady_timer>(io, std::chrono::seconds(interval));
        timer->async_wait([interval, this](auto ec);

struct TimerPump {
  // A tiny manual scheduler for deterministic tests.
  struct Item { ocppwo::Ms delay; std::function<void()> cb; };
  std::vector<Item> q;

  void post_after(ocppwo::Ms d, std::function<void()> cb) { q.push_back({d, std::move(cb)}); }
  void run_all() { for (auto& it : q) it.cb(); q.clear(); }
  template<class Pred>
  void run_while(Pred p) {
    // In a real test, advance fake time and trigger due timers in order
    while (p() && !q.empty()) { auto it = q.front(); q.erase(q.begin()); it.cb(); }
  }
};
//what does TimerPump do:
/*  an entry is defined as 
        containing Ms
        containing std::function<void()>
    it stores entreis in a vector from the rear end.
    it has a post_after api that adds entry into the vector
    it has a run_all() api that executes each entry and then clears the vector.
 */
/*
    when the test closes you have pending outgoing frames
        how do you simulate this?
        you start sending a ton of frames and then suddently close the connection
        or you create a queue of pending outgoing frames, send them out with delay
        and then close the connection and see how the system deals with them?

    create a test harness
        -that starts a server for test
        -that starts a client under test
        -go from there...
    

 */