// tcp_client.cpp
#include <boost/asio.hpp>
#include <iostream>
#include <string>

#include <fstream>
#include <json.hpp>
#include <variant>
#include <random>

#include "ocpp_model.hpp"
using json = nlohmann::json;
using namespace std::string_literals;
using namespace nlohmann::literals;
using boost::asio::ip::tcp;

int main() {
    boost::asio::io_context io;
    tcp::socket socket(io);
    socket.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 12345));

    OcppPayload x = Authorize {"msg_0D9F74FD"};
    Call c = create_call("Authorize", x);
    json j = c;

    std::string msg = j.dump();
    boost::asio::write(socket, boost::asio::buffer(msg + "\n"));

    boost::asio::streambuf buf;
    boost::asio::read_until(socket, buf, '\n');

    std::istream input(&buf);
    std::string echoed;
    std::getline(input, echoed);
    std::cout << "RX<--- " << echoed << "\n";
    
    json response = json::parse(echoed);
    OcppFrame frame = parse_frame(response);
    dispatch_frame(frame);
    return 0;
}
