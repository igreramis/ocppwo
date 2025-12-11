#ifndef RECONNECT_HPP
#define RECONNECT_HPP

#include <boost/asio.hpp>
#include <chrono>
#include <functional>
#include <iostream>

using namespace std;
using Ms = std::chrono::milliseconds;

struct ReconnectPolicy {
  std::chrono::milliseconds initial_backoff{500};
  std::chrono::milliseconds max_backoff{30'000};
  float jitter_ratio{0.20f};
  std::chrono::milliseconds resume_delay{250};
};

enum class ConnState { Disconnected, Connecting, Connected, Closing };
enum class CloseReason { Clean, TransportError, ProtocolError };

// ReconnectSignals — outbound notifications from ReconnectController
// - External actors assign these std::function callbacks (observers).
// - ReconnectController invokes them to report its own state transitions.
struct ReconnectSignals {
  std::function<void()> on_connecting;
  std::function<void()> on_connected;
  std::function<void(CloseReason)> on_closed;
  std::function<void()> on_online;
  std::function<void()> on_offline;
  std::function<void(std::chrono::milliseconds)> on_backoff_scheduled;// Notifies observers when a backoff delay is computed and scheduled. Receives the delay (ms). Called after compute_backoff_ and posting the timer; handler should be quick/non‑blocking.
};

// TransportOps — inbound operations/events into ReconnectController
// - Provided by the transport/glue layer to let the controller initiate work
//   and receive completions (connect/close/timers/clock).
struct TransportOps {
  std::function<void(const std::string& url, std::function<void(bool ok)>)> async_connect;
  std::function<void(std::function<void()>)> async_close;
  std::function<void(std::chrono::milliseconds, std::function<void()>)> post_after;
  std::function<std::chrono::steady_clock::time_point()> now;
};


class ReconnectController {
    public:
        // ReconnectSignals — outbound notifications from ReconnectController
        // - External actors assign these std::function callbacks (observers).
        // - Controller holds a shared_ptr to allow the glue/tests to own/modify callbacks
        //   after construction and to ensure a stable lifetime.
        ReconnectController(ReconnectPolicy r, TransportOps t, std::shared_ptr<ReconnectSignals> rS)
         :rp(r), tOps(std::move(t)), rSigs(std::move(rS)) {};

        /**
         * start — Begin establishing an OCPP transport connection.
         *
         * Input:
         *   url  Endpoint passed to TransportOps::async_connect.
         *
         * Return(Signals):
         *   - Success: rSigs.on_connected()
         *   - On Failure: nothing
         */
        void start(const std::string& url){
            url_ = url;
            if( cS_ != ConnState::Disconnected )
                return;
            try_reconnect_();
        };

        void stop(){
            if( cS_ == ConnState::Disconnected )
                return;
            
            if( tOps.async_close )
            {
                cS_ = ConnState::Closing;
                tOps.async_close([this](){
                    cS_ = ConnState::Disconnected;
                });
            }

            reconnect_scheduled_ = false;
        };

        // on_transport_open()
        // Inbound event from transport layer: underlying socket/channel reports
        // successful open/handshake. Controller may transition to Connecting/Connected
        // and emit rSigs->on_connecting / on_connected as appropriate.
        void on_transport_open(){
            ;
            cS_ = ConnState::Connected;
            if(rSigs->on_connected)
            {
                rSigs->on_connected();
            }
        };

        // on_transport_close(cR)
        // Inbound event from transport layer: connection closed (clean or error).
        // Controller should update cS_, decide on backoff/retry, and emit
        // rSigs->on_closed / on_offline / on_backoff_scheduled.
        void on_transport_close(CloseReason why){
            std::cout<<"on_transport_close() being called\n";
            if( rSigs->on_closed )
            {
                rSigs->on_closed(why);
            }

            if( rSigs->on_offline )
            {
                rSigs->on_offline();
            }

            if( cS_ == ConnState::Closing )
            {
                cS_ = ConnState::Disconnected;
                return;
            }
            
            cS_ = ConnState::Disconnected;

            schedule_reconnect_();
        };

        // on_boot_accepted()
        // Inbound event feeder: higher layer (Session) reports BootNotification
        // accepted. Controller can mark the logical channel online and emit
        // rSigs->on_online.
        void on_boot_accepted(){
            //online should be flagged on after resume_delay time units
            if( tOps.post_after)
            {
                tOps.post_after(rp.resume_delay, [this](){
                    online_ = true;
                    if( rSigs->on_online )
                    {
                        rSigs->on_online();
                    }
                });
            }
            else
            {
                online_ = true;
                if( rSigs->on_online )
                {
                    rSigs->on_online();
                }
            }
            // tOps.post_after(rp.resume_delay, [this](){

            // });
            // if(rSigs->on_online)
            // {
            //     rSigs->on_online();
            // }
        };
        bool can_send() const{
            return online_ && (cS_ == ConnState::Connected);
        };
        ConnState state() const{
            return cS_;
        };
        bool online() const{
            return online_;
        };

        int attempt() const{
            return attempt_;
        };

    private:
        std::string url_;
        ReconnectPolicy rp;
        TransportOps tOps;
        // ReconnectSignals &rSigs;
        std::shared_ptr<ReconnectSignals> rSigs;
        ConnState cS_{ConnState::Disconnected};
        int attempt_{0};
        bool online_{false}, reconnect_scheduled_{false};
        Ms last_backoff_;

        // compute_backoff_(attempt)
        // Purpose: derive the delay before the next reconnect attempt.
        // Logic:
        //   1. Start from initial_backoff.
        //   2. Exponential doubling (base * 2^(attempt-1)) with saturation at max_backoff.
        //   3. Apply symmetric jitter (±jitter_ratio) so average delay remains unbiased.
        // Input: attempt (1 = first retry attempt after initial failure; <=0 treated as 0/1).
        // Output: milliseconds duration in [0, max_backoff].
        void try_reconnect_(){
            if( cS_ != ConnState::Disconnected )
                return;

            std::cout<<"Reconnect starting..."<<"\n";
            
            cS_ = ConnState::Connecting;

            if(rSigs->on_connecting)
            {
                rSigs->on_connecting();
            }

            if(tOps.async_connect)
            {
                tOps.async_connect(url_, [this](bool ok){
                    if(ok)
                    {
                        on_transport_open();
                    }
                    else
                    {
                        cS_ = ConnState::Disconnected;
                        schedule_reconnect_();
                    }
                });
            }

        }

        // compute_backoff_(attempt)
        // Purpose:
        //   Compute the delay to wait before the next reconnect attempt.
        //
        // Behavior summary:
        //   - Base delay is rp.initial_backoff.
        //   - Exponential growth: each successive attempt (as passed in `attempt`) doubles
        //     the delay (subject to clamping). With the current shift logic:
        //       attempt==0 -> initial_backoff
        //       attempt==1 -> initial_backoff(first two attempts are non-exponential to deal with transient network failures before escalating the situation to exponentials)
        //       attempt==2 -> 2 * initial_backoff
        //       attempt==3 -> 4 * initial_backoff
        //       attempt==n -> initial_backoff * 2^(n-2)  (for n>=2)
        //     (In short: delays grow exponentially with doubling on successive retries;
        //      the function treats the first two attempt values as the initial interval.)
        //   - The computed delay is clamped to rp.max_backoff to avoid overflow/growth beyond cap.
        //   - Jitter: a symmetric random jitter of ±rp.jitter_ratio is applied, i.e.
        //       delay *= (1.0 + signed_frac)  where signed_frac ∈ [ -jitter_ratio, +jitter_ratio ].
        //     The function uses a thread_local PRNG for the random component.
        //   - Large attempt values are saturated to avoid undefined shifts (shift > 63 -> use cap).
        //
        // Inputs:
        //   attempt  index describing which retry is being computed (caller-defined semantics).
        //
        // Output:
        //   std::chrono::milliseconds value in [0, rp.max_backoff] (after jitter & clamping).
        //
        // Notes:
        //   - The function is effectively pure except for the RNG (it returns non-deterministic
        //     results because of jitter).
        //   - If you want the first retry to be different, adjust the mapping from `attempt` to
        //     exponent (the current code treats attempt==0/1 as the initial interval).
        Ms compute_backoff_(int attempt) const {
            uint64_t base_64 = static_cast<uint64_t>(rp.initial_backoff.count());
            uint64_t cap_64 = static_cast<uint64_t>(rp.max_backoff.count());
            uint64_t delay_64 = static_cast<uint64_t>(rp.initial_backoff.count());
            uint64_t shift = (attempt > 0) ? static_cast<uint64_t>(attempt - 1): 0;
            if( shift > 63 )
            {
                delay_64 = cap_64;
            }
            else if( delay_64 > cap_64>>shift )
            {
                delay_64 = cap_64;
            }
            else
            {
                delay_64 <<= shift;
            }
            
            static thread_local std::mt19937 rng{std::random_device{}()};
            std::uniform_real_distribution<float> dist(0.f, 1.f);
            float f = dist(rng);
            float signed_frac = ((f*2.f) - 1.f) * rp.jitter_ratio;
            double backoff = static_cast<double>(delay_64) * ( 1.f + signed_frac);
            return Ms{ static_cast<Ms::rep>(backoff) };
        }

        void schedule_reconnect_(){

            if( reconnect_scheduled_ )
                return;
            
            reconnect_scheduled_ = true;
            //does it get reset if the lambda does not get called?
            //if post_after is not assigned
            //can the timer be cancelled?
            //the io context stops after posting the handler and it nevers run
            //the handler throws an exception
            //the reconnectcontroller object is destroyed before the handler runs
            //what the fuck is the timer handler about?
            //how should stopping the reconect layer effect the schedule_reconnect
            //functionality?

            last_backoff_ = compute_backoff_(attempt_++);

            //if we don't have to be a fucking nazi about indicating before or after
            //posting the reconnect that the reconnect has been posted, we can just
            //post it here
            if( rSigs->on_backoff_scheduled )
            {
                rSigs->on_backoff_scheduled(last_backoff_);
            }

            if( tOps.post_after )
            {
                tOps.post_after(last_backoff_,[this](){
                    if(reconnect_scheduled_ == false)
                        return;

                    reconnect_scheduled_ = false;
                    try_reconnect_();
                });
            }
            else
            {
                reconnect_scheduled_ = false;
            }

            // if( rSigs->on_backoff_scheduled )
            // {
            //     rSigs->on_backoff_scheduled(last_backoff_);
            // }
        };
};

#endif // RECONNECT_HPP