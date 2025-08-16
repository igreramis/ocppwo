// tcp_client.cpp
#include <boost/asio.hpp>
#include <iostream>
#include <string>

#include <fstream>
#include <json.hpp>
#include <variant>
#include <random>

#include "ocpp_model.hpp"
#include "session.hpp"
#include "client.hpp"
using json = nlohmann::json;
using namespace std::string_literals;
using namespace nlohmann::literals;
using boost::asio::ip::tcp;

//connect to a server via tcp
//create a ocpp payloadf
//covnert payload to json and write into socket
//read response back from socket
//reverse parse json into ocpp payload and dispatch handler

//connect to a server via tcp, send out a boot notification and read the response back
//how would you use it
//boost::asio::io_context io;
//Client client(io);
//client.connect_and_boot(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 12345));
// int main() {
//     boost::asio::io_context io;//this is the event loop
//     Client client(io);
//     client.connect_and_boot(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 12345));
//     // socket.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 12345));

//     // OcppPayload x = Authorize {"msg_0D9F74FD"};
//     // Call c = create_call("Authorize", x);
//     // json j = c;

//     // std::string msg = j.dump();
//     // boost::asio::write(socket, boost::asio::buffer(msg + "\n"));

//     // boost::asio::streambuf buf;
//     // boost::asio::read_until(socket, buf, '\n');

//     // std::istream input(&buf);
//     // std::string echoed;
//     // std::getline(input, echoed);
//     // std::cout << "RX<--- " << echoed << "\n";
    
//     // json response = json::parse(echoed);
//     // OcppFrame frame = parse_frame(response);
//     // dispatch_frame(frame);
//     return 0;
// }
int main() {
    boost::asio::io_context io;//this is the event loop
    Client client(io);
    client.connect_and_boot(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 12345));
    io.run();
    
    return 0;
}
