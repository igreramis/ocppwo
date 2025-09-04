#include <boost/asio.hpp>
#include "ocpp_model.hpp"
#include "session.hpp"
using boost::asio::ip::tcp;

struct Client {
    boost::asio::io_context& io;
    tcp::socket sock;
    boost::asio::streambuf buf;
    // Session session; // pointer to Session
    std::unique_ptr<boost::asio::steady_timer> timer;
    static constexpr int HEARTBEAT_INTERVAL = 24*60*60; // 1 day in seconds

    Client(boost::asio::io_context& io_) : io(io_),
                                           sock(io_)
                                        //    session(io_, sock) 
                                           {}

    void start_heartbeat(int interval){
        timer = std::make_unique<boost::asio::steady_timer>(io, std::chrono::seconds(interval));
        timer->async_wait([interval, this](auto ec){
            if (ec) 
            {
                std::cerr << "Heartbeat timer canceled: " << ec.message() << "\n";
                return; // timer canceled = got reply
            }
            // send heartbeat
            // session.send_call(HeartBeat{},
            //                     [interval, this](const OcppFrame& f){
            //     if (std::holds_alternative<CallResult>(f)) {
            //         const auto& r = std::get<CallResult>(f);
            //         std::cout << "HeartbeatResponse: " << r.payload << "\n";
            //     } else if (std::holds_alternative<CallError>(f)){
            //         const auto& e = std::get<CallError>(f);
            //         std::cerr << "Heartbeat Error: " << e.errorDescription << "\n";
            //     }
            // });
            // restart timer
            start_heartbeat(interval);
        });
    }

    //TODO: move this functionality to WsClient
    void connect_and_boot(const tcp::endpoint& ep) {
        sock.async_connect(ep, [this](auto ec){
        if (ec) 
        {
            std::cerr << "Connect failed: " << ec.message() << "\n";
            return;
        }
        // session.send_call(BootNotification{"X100", "OpenAI"},
        //           [this](const OcppFrame& f){
        //             if( std::holds_alternative<CallResult>(f) ) {
        //                 auto r = std::get<CallResult>(f);
        //                 std::cout << "BootNotificationResponse: "<< r.payload << "\n";
        //                 BootNotificationResponse resp = r.payload;
        //                 if (resp.status == "Accepted") {
        //                     std::cout << "BootNotification accepted, current time: " << resp.currentTime << "\n";
        //                     session.state = Session::State::Ready;
        //                     start_heartbeat(resp.interval);
        //                 } else if (resp.status == "Pending") {
        //                     std::cout << "BootNotification pending, interval: " << resp.interval << "\n";
        //                 } else {
        //                     std::cout << "BootNotification rejected\n";
        //                 }
        //             } else if (std::holds_alternative<CallError>(f)){
        //                 const auto& e = std::get<CallError>(f);
        //                 std::cerr << "BootNotification Error: " << e.errorDescription << "\n";
        //             }
        //           });
        
        async_read();

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
            // session.on_frame(frame);
            async_read();
        });
    }
};