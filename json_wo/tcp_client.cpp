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

std::string generate_message_id() {
    static std::mt19937 rng{std::random_device{}()};
    static std::uniform_int_distribution<int> dist(0, 15);

    std::string id = "msg_";
    for (int i = 0; i < 8; ++i)
        id += "0123456789ABCDEF"[dist(rng)];

    return id;
}

template<typename Payload>
Call create_call(const std::string& action, const Payload& p) {
    return Call{
        2,
        generate_message_id(),
        action,
        json(p)
    };
}

//create call would take an OCPPFrame?
//why do you need constexpr here? it seems the compiler only compiles branches for existing types and 
//by the nature of the if-else construct, it returns when it finds one of the branches to be true.
//the way templates work, the compiler generates code for each of the Types defined statically and this
//prevents the execution of the else block.
std::string action_for_payload(const OcppPayload& payload)
{
    return std::visit([](auto &payload)->std::string{
        using T = std::decay_t<decltype(payload)>;
        if constexpr ( std::is_same_v<T, BootNotification> )
        {
            return "BootNotification";
        }
        else if constexpr ( std::is_same_v<T, Authorize> )
        {
            return "Authorize";
        }
        else
        {
            static_assert(!sizeof(T), "Unhandled payload type");
        }
    }, payload);
}

Call create_call(const std::string& id, const OcppPayload& payload){
    //you either fill up the fucker in part by declaring it before hand
    Call c;
    c.messageTypeId = 2;
    c.messageId = id;
    c.action = action_for_payload( payload );
    c.payload = std::visit([](const auto& payload){
        return nlohmann::json(payload);
    }, payload);

    return c;
}


int main() {
    boost::asio::io_context io;
    tcp::socket socket(io);
    socket.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 12345));

    // OcppPayload x = BootNotification {"X100", "OpenAI"};
    // Call c = create_call("BootNotification", x);

    OcppPayload x = Authorize {"msg_0D9F74FD"};
    Call c = create_call("Authorize", x);
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
