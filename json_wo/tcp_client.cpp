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

//create call would take an OCPPFrame?
//why do you need constexpr here? it seems the compiler only compiles branches for existing types and 
//by the nature of the if-else construct, it returns when it finds one of the branches to be true.
//the way templates work, the compiler generates code for each of the Types defined statically and this
//prevents the execution of the else block.

int main() {
    boost::asio::io_context io;
    tcp::socket socket(io);
    socket.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 12345));

    // OcppPayload x = BootNotification {"X100", "OpenAI"};
    // Call c = create_call("BootNotification", x);

    OcppPayload x = Authorize {"msg_0D9F74FD"};
    // Call c = create_call("Authorize", x);
    Call c = create_call("Biken", x);
    json j = c;
    // std::string msg = R"([2,"msg_0","BootNotification",{"chargePointModel":"X100","chargePointVendor":"OpenAI"}])";
    std::string msg = j.dump();
    boost::asio::write(socket, boost::asio::buffer(msg + "\n"));

    boost::asio::streambuf buf;
    boost::asio::read_until(socket, buf, '\n');

    std::istream input(&buf);
    std::string echoed;
    std::getline(input, echoed);
    std::cout << "RX<--- " << echoed << "\n";
    
    json response = json::parse(echoed);
    //convert the json to CallResult if possible
    OcppFrame frame = parse_frame(response);
    dispatch_frame(frame);
    return 0;
}
