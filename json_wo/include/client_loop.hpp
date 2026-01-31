#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <chrono>
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
        //handle for unit tests to know about the backoff duration scheduled
        std::function<void(std::chrono::milliseconds)> on_backoff_scheduled;
    };

    //functors for WsClient and Session
    struct Factories {
        std::function<std::shared_ptr<WsClient>(boost::asio::io_context&, std::string host, std::string port)> make_transport;
        std::function<std::shared_ptr<Session>(boost::asio::io_context&, std::shared_ptr<Transport>, std::shared_ptr<SessionSignals>)> make_session;
    };

    // ---------------------------------------------------------------------
    // Lifecycle: ClientLoop(ioc, cfg, factories)
    //
    // Purpose:
    //   Constructs the reconnect coordinator. ClientLoop owns (directly or via
    //   its Impl) the lifecycle of:
    //     connect -> create Session -> wire callbacks -> boot -> online ->
    //     handle close -> schedule reconnect.
    //
    // Touches:
    //   - Stores `cfg` and `factories`.
    //   - Installs glue so reconnect/backoff signals (if enabled) can be observed
    //     via `cfg.on_backoff_scheduled`.
    //
    // Persistence / reset behavior:
    //   - Counters such as connect_attempts / online_transitions are intended to
    //     accumulate over the lifetime of the ClientLoop instance.
    //   - The current transport/session are replaced on reconnect attempts.
    //
    // Conditions / notes:
    //   - Does not start IO by itself; call start() and pump the io_context.
    ClientLoop(boost::asio::io_context& ioc, Config cfg, Factories f);

    // ---------------------------------------------------------------------
    // Lifecycle: ~ClientLoop()
    //
    // Purpose:
    //   Releases the implementation and any owned transports/sessions.
    //
    // Conditions / notes:
    //   - As with most async components, the caller should prefer calling stop()
    //     explicitly (and pumping the io_context) to drive orderly shutdown.
    ~ClientLoop();

    //delete copy ctor/assignment. why? because of the internal shared_ptr to impl_ later below.
    ClientLoop(const ClientLoop&) = delete;
    ClientLoop& operator=(const ClientLoop&) = delete;

    // ---------------------------------------------------------------------
    // Lifecycle: start()
    //
    // Purpose:
    //   Begins the reconnect loop. Typically:
    //     Offline -> Connecting -> (on success) Online
    //
    // Touches / effects:
    //   - Initiates the first connect attempt.
    //   - Wires transport callbacks (on_open/on_message/on_close) for the current
    //     transport, and creates/wires a Session for protocol-level behavior.
    //   - May schedule reconnects via backoff on failures/close.
    //
    // Persistence / reset behavior:
    //   - Increments `connect_attempts()` on each attempted connect.
    //   - Increments `online_transitions()` each time the loop reaches Online.
    //
    // Conditions / notes:
    //   - Requires `Factories::make_transport` and `Factories::make_session` to be
    //     provided (non-null) in normal operation.
    //   - Callers must pump the `io_context` for progress.
    void start();

    // ---------------------------------------------------------------------
    // Lifecycle: stop()
    //
    // Purpose:
    //   Stops the reconnect loop and shuts down the current transport/session.
    //
    // Touches / effects:
    //   - Cancels any pending reconnect timers/backoff (implementation detail).
    //   - Closes the current transport if connected.
    //
    // Persistence / reset behavior:
    //   - Does NOT reset counters/probes; those remain queryable after stop.
    //
    // Conditions / notes:
    //   - The io_context must be pumped for async closes/cancellations to run.
    void stop();

    // ---------------------------------------------------------------------
    // Probe: state()
    //
    // Returns:
    //   - Current high-level connectivity state:
    //       Offline    : not connected, and not actively connecting
    //       Connecting : a connect attempt is in progress / scheduled
    //       Online     : connected + session is considered "online"
    //
    // Persistence / reset behavior:
    //   - State transitions over time as the loop runs. Reconnects typically
    //     drive Online -> Offline/Connecting -> Online.
    State state() const;

    // ---------------------------------------------------------------------
    // Probe: connect_attempts()
    //
    // Returns:
    //   - Count of connect attempts started by this ClientLoop instance.
    //
    // Persistence / reset behavior:
    //   - Monotonically increases over the lifetime of the ClientLoop.
    //   - Not reset on reconnect.
    std::uint64_t connect_attempts() const;

    // ---------------------------------------------------------------------
    // Probe: online_transitions()
    //
    // Returns:
    //   - Count of transitions into Online state.
    //
    // Persistence / reset behavior:
    //   - Monotonically increases over the lifetime of the ClientLoop.
    //   - Useful for E2E assertions like "boot happens once per connection".
    std::uint64_t online_transitions() const;

private:
    //pimpl idiom for reducing build times and encapsulating implementation details
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

/*todo
-take as input the policy structure that you pass onto Reconnect. this way,
you can use different configurations from unit tests.
*/