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
  std::function<uint64_t(std::chrono::milliseconds, std::function<void()>)> post_after;
  std::function<bool(uint64_t)> cancel_after;
  std::function<std::chrono::steady_clock::time_point()> now;
};

/*
 * ReconnectController
 *
 * Purpose:
 *   A small state machine that owns the reconnection policy and orchestrates when to:
 *   - attempt a transport connection,
 *   - retry with backoff after failures/closes,
 *   - stop/close on request, and
 *   - gate “online” readiness after the higher-level OCPP boot flow succeeds.
 *
 * Inputs (what the controller needs from the outside):
 *   1) TransportOps (controller -> transport):
 *      - async_connect(url, done) : initiate a transport connect attempt.
 *      - async_close(done)        : request a transport close.
 *      - post_after(delay, cb)    : schedule a callback after a delay (used for backoff + resume_delay).
 *      - now()                    : time source (optional; telemetry/tests).
 *
 *   2) Inbound events from transport (transport -> controller):
 *      - on_transport_open()              : transport is established (handshake complete).
 *      - on_transport_close(CloseReason)  : transport closed or failed after being established.
 *
 *   3) Inbound events from Session / protocol layer (session -> controller):
 *      - on_boot_accepted() : indicates the OCPP session has completed BootNotification successfully.
 *        This is the missing “session is logically ready” signal the controller needs to decide
 *        when the overall system is truly online.
 *
 * Outputs (what the controller emits to the outside):
 *   ReconnectSignals (controller -> observers):
 *   - on_connecting()         : a connect attempt is starting.
 *   - on_connected()          : transport is connected (TCP/WS established).
 *   - on_closed(reason)       : transport closed (clean/error).
 *   - on_backoff_scheduled(ms): a retry has been scheduled with the computed backoff.
 *   - on_offline()            : system is offline immediately after transport close.
 *   - on_online()             : system is online after BootAccepted + optional resume_delay.
 *
 * Key idea (transport-connected vs system-online):
 *   - “Connected” means the transport layer is up (you can exchange frames).
 *   - “Online” means the higher-level Session/protocol is ready to send normal traffic:
 *     the Session has successfully completed BootNotification and the controller has
 *     applied resume_delay before declaring the system online.
 *
 * Lifetime / async safety:
 *   - Uses enable_shared_from_this / weak_from_this in posted callbacks to avoid use-after-free
 *     when timers fire after destruction; instances should therefore be owned by std::shared_ptr
 *     in any scenario where timers/retries are active.
 */
class ReconnectController : public std::enable_shared_from_this<ReconnectController> {
    public:
        // ReconnectSignals — outbound notifications from ReconnectController
        // - External actors assign these std::function callbacks (observers).
        // - Controller holds a shared_ptr to allow the glue/tests to own/modify callbacks
        //   after construction and to ensure a stable lifetime.
        ReconnectController(ReconnectPolicy r, TransportOps t, std::shared_ptr<ReconnectSignals> rS)
         :rp(r), tOps(std::move(t)), rSigs(std::move(rS)) {};

        /**
         * start(const std::string& url)
         *
         * Brief:
         *   Begin the reconnect controller's connect flow for the given endpoint.
         *
         * Trigger:
         *   Called by the glue/harness to request that the controller establish a transport.
         *
         * Purpose:
         *   - Record the target URL and, if the controller is idle (Disconnected),
         *     kick off the first asynchronous connect attempt via try_reconnect_().
         *   - The method itself is non‑blocking; progress and outcomes are reported
         *     via ReconnectSignals (on_connecting, on_connected, on_backoff_scheduled, on_closed, etc.).
         *
         * Semantics / constraints:
         *   - No‑op if the controller is not in ConnState::Disconnected.
         *   - Does not perform higher‑level protocol actions (boot/resume); those are handled by callers.
         *   - Must be invoked on the same execution context or otherwise synchronized with TransportOps.
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
            if( tOps.post_after)
            {
                std::weak_ptr<ReconnectController> wk = this->weak_from_this();
                tOps.post_after(rp.resume_delay, [wk](){
                    if(auto s = wk.lock() )
                    {
                        s->online_ = true;
                        if( s->rSigs->on_online )
                        {
                            s->rSigs->on_online();
                        }
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

        /*
        * try_reconnect_()
        *
        * Brief:
        *   Drive a single asynchronous connect attempt when the controller is idle.
        *
        * Trigger:
        *   Called by start(), by the post_after timer handler (schedule_reconnect_),
        *   or by other internal logic to initiate the next connect attempt. Progress
        *   is reported by ReconnectSignals on_connecting
        *
        * Purpose:
        *   - Transition controller to Connecting, emit on_connecting, and invoke
        *     TransportOps.async_connect(url, completion).
        *   - On success -> treat transport as open (on_transport_open()).
        *   - On failure -> mark Disconnected and arrange a backoff via schedule_reconnect_().
        * 
        *  Return:
        *   vai ReconnectSignals (on_connecting)
        *
        * Semantics / constraints:
        *   - Non‑blocking: returns immediately; completion is delivered by the provided callback.
        *   - Idempotent guard: only attempts when in the Disconnected state.
        *   - Must be invoked on the same execution context / strand used by TransportOps
        *     (or be externally synchronized) to avoid races with transport callbacks.
        *   - Does not perform higher‑level protocol steps (boot/resume); those are handled elsewhere.
        */
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
            else if( delay_64 > cap_64>>shift )//before increasing exponentially, check if exponential increase would exceed the cap
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

        /*
        * schedule_reconnect_()
        *
        * Brief:
        *   Compute and schedule the next reconnect attempt using the controller's
        *   backoff policy, and notify observers that a backoff was chosen.
        *
        * Trigger:
        *   Called when a connect attempt fails or the transport closes and the
        *   controller decides to retry (e.g. from try_reconnect_(), on_transport_close()).
        *
        * Purpose:
        *   - Guard against duplicate scheduling (idempotent if already scheduled).
        *
        * Output notification:
        *   - Calls rSigs->on_backoff_scheduled(last_backoff_) immediately after computing the delay
        *     (before posting) so observers always see the scheduled value even if post_after
        *     invokes handlers synchronously.
        *
        * Semantics / constraints:
        *   - Idempotent: returns immediately if reconnect_scheduled_ is already true.
        *   - If tOps.post_after is not provided, the function clears reconnect_scheduled_
        *     (no timer can be posted) so retries are not left permanently scheduled.
        *   - Must be invoked on the same execution context / strand used by TransportOps
        *     (or be externally synchronized) to avoid races on controller state.
        *   - Observers are optional; check rSigs and the specific std::function before invoking.
        *
        * Edge cases / notes:
        *   - If post_after executes the handler synchronously, calling the observer
        *     before posting avoids missing the notification (some implementations of post_after
        *     may invoke handlers synchronously(i.e. call cb() before post_after returns).
        *   - The scheduled timer may never run (io stopped, controller destroyed, or
        *     post_after implementation misbehaves); callers/owners should provide a
        *     stop/cancel path that clears reconnect_scheduled_ if needed.
        */
        void schedule_reconnect_(){

            if( reconnect_scheduled_ )
                return;
            
            reconnect_scheduled_ = true;

            last_backoff_ = compute_backoff_(attempt_++);

            if( rSigs->on_backoff_scheduled )
            {
                rSigs->on_backoff_scheduled(last_backoff_);
            }

            if( tOps.post_after )
            {
                std::weak_ptr<ReconnectController> wk = this->weak_from_this();
                tOps.post_after(last_backoff_,[wk](){
                    if( auto s = wk.lock() ){
                        if(s->reconnect_scheduled_ == false)
                            return;

                        s->reconnect_scheduled_ = false;
                        s->try_reconnect_();
                    }
                });
            }
            else
            {
                reconnect_scheduled_ = false;
            }
        };
};

#endif // RECONNECT_HPP