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

int main() {
    boost::asio::io_context io;//this is the event loop
    Client client(io);
    client.connect_and_boot(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 12345));
    io.run();

    return 0;
}
