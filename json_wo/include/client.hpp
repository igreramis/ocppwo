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
        sock.async_connect(ep, [this](auto ec){
        if (ec) 
        {
            std::cerr << "Connect failed: " << ec.message() << "\n";
            return;
        }
        // TODO: Call c = create_call(id, "BootNotification", BootNotification{...});
        Call c = create_call(generate_message_id(), "BootNotification", BootNotification{"X100", "OpenAI"});
        std::string line = json(c).dump() + "\n";
        async_write(line);
        async_read();
        });
    }
    void async_write(std::string line) {
        boost::asio::async_write(sock, boost::asio::buffer(line),
        [line](auto ec, std::size_t len){
            if (ec) {
                std::cerr << "Write failed: " << ec.message() << "\n";
            } else {
                std::cout << "Sent  " << len << "bytes :"<< line << "\n";
            }
        });
    }
    void async_read() {
        boost::asio::async_read_until(sock, buf, '\n',
        [this](auto ec, std::size_t){
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
            json j = json::parse(line);
            OcppFrame frame = parse_frame(j);
            session.on_frame(frame);
            async_read();
        });
    }
};