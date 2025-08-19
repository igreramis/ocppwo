#include <boost/asio.hpp>
#include "ocpp_model.hpp"
#include "session.hpp"
using boost::asio::ip::tcp;

struct Client {
    boost::asio::io_context& io;
    tcp::socket sock;
    boost::asio::streambuf buf;
    Session session; // pointer to Session
    //have client own the fucking socket. have client own the fucking session.

    Client(boost::asio::io_context& io_) : io(io_),
                                           sock(io_),
                                           session(io_, sock) {}
    void connect_and_boot(const tcp::endpoint& ep) {
        //session.send_call()
        //give it a callback for BootNotificationHandler
        //when the handler is executed on receiving response
        //it starts the heartbeat timer
        //the timer gets a callback that sends the HeartBeat payload through
        //send_call() every x seconds and its callback processes the reply
        sock.async_connect(ep, [this](auto ec){
        if (ec) 
        {
            std::cerr << "Connect failed: " << ec.message() << "\n";
            return;
        }
        session.send_call("BootNotification",
                  BootNotification{"X100", "OpenAI"},
                  [this](const OcppFrame& f){
                    if( std::holds_alternative<CallResult>(f) ) {
                        auto r = std::get<CallResult>(f);
                        std::cout << "BootNotificationResponse: "<< r.payload << "\n";
                        session.state = Session::State::Ready;
                        //start a heartbeat timer here
                    } else if (std::holds_alternative<CallError>(f)){
                        const auto& e = std::get<CallError>(f);
                        std::cerr << "BootNotification Error: " << e.errorDescription << "\n";
                    }
                  });
        async_read();
        // send_call( "BootNotification", 
        //     BootNotification{"X100", "OpenAI"},
        //     [this](const OcppFrame& f) {
        //         if (std::holds_alternative<CallResult>(f)) {
        //             const auto& r = std::get<CallResult>(f);
        //             std::cout << "BootNotification Result: " << r.payload.dump() << "\n";
        //             session.state = Session::State::Ready; // move to Ready state
        //             // start heartbeat timer here if needed
        //         } else if (std::holds_alternative<CallError>(f)) {
        //             const auto& e = std::get<CallError>(f);
        //             std::cerr << "BootNotification Error: " << e.errorDescription << "\n";
        //         }
        //     });

        // Call c = create_call(generate_message_id(), "BootNotification", BootNotification{"X100", "OpenAI"});
        // std::string line = json(c).dump() + "\n";
        // async_write(line);
        // async_read();
        });
    }
    void async_write(std::string line) {
        boost::asio::async_write(sock, boost::asio::buffer(line),
        [line](auto ec, std::size_t len){
            if (ec) {
                std::cerr << "Write failed: " << ec.message() << "\n";
            } else {
                std::cout << "TX>>>" << line << "\n";
            }
        });
    }
    void async_read() {
        boost::asio::async_read_until(sock, buf, '\n',
        [this](auto ec, std::size_t len){
            if (ec)
            {
                if( ec == boost::asio::error::eof ) {
                    std::cerr << "Connection closed by peer.\n";
                } else {
                    std::cerr << "Read failed: " << ec.message() << "\n";
                }
                return;
            }
            std::istream is(&buf); std::string line; std::getline(is, line);
            std::cout << "RX<<<" << line << "\n";
            json j = json::parse(line);
            OcppFrame frame = parse_frame(j);
            session.on_frame(frame);
            async_read();
        });
    }
};