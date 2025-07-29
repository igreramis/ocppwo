// tcp_server.cpp
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include "ocpp_model.hpp"

using boost::asio::ip::tcp;

// CallResult _handle_call(const Call &c)
OcppFrame _handle_call(const Call &c)
{
    if( c.action == "BootNotification" )
    {
        BootNotification boot = c.payload;
        BootNotificationResponse res = {
            "2025-07-16T12:00:00Z",
            300,
            "Accepted"
        };
        return CallResult {
            3,
            c.messageId,
            res// c.payload//res
        };
    }
    else if( c.action == "Authorize" )
    {
        Authorize authorize = c.payload;
        AuthorizeResponse res = {
            "ID Tag Authorized"
        };
        return CallResult {
            3,
            c.messageId,
            res
        };
    }
    else
    {
        ;
        std::string error_detail = R"([
            Error Detail...
        ])";
        return CallError {
            3,
            "CallError",
            "404",
            "Command not found",
            json::parse(error_detail)
        };
    }

}
int main() {
    boost::asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 12345));

    std::cout << "Server listening on port 12345...\n";
    tcp::socket socket(io);
    acceptor.accept(socket);

    boost::asio::streambuf buf;
    boost::asio::read_until(socket, buf, '\n');

    std::istream input(&buf);
    std::string message;
    std::getline(input, message);

    std::cout << "[Server] Received: " << message << "\n";

    json msg_json = json::parse(message);
    OcppFrame frame = parse_frame(msg_json);
    // dispatch_frame(frame);

#if 0
    std::map<std::string, std::function<CallResult(const Call&)>> handlerMap;
    handlerMap["BootNotification"] = BootNotificationHandler;
    handlerMap["Authorize"] = AuthorizationHandler;

    if( std::holds_alternative<Call>(frame) )
    {
        Call c = std::get<Call>(frame);
        if( handlerMap.find(c.action) != handlerMap.end() )
        {
            json j = handlerMap[c.action](c);
            message = j.dump();
        }
        else{
            json error_details = {
                {"hint", "Supported actions: BootNotification, Authorization"}
            };
            
            CallError ce {
                4,
                c.messageId,
                "NotImplemented",
                "Unknown action: " + c.action,
                error_details
            };
            //setup a CallError
            //convert it into json
            json j = ce;
            message = j.dump();
        }
        // message = j.dump();
    }
#endif

#if 1
    std::map<std::string, std::function<OcppFrame(const Call&)>> handlerMap;
    handlerMap["BootNotification"] = BootNotificationHandler;
    // handlerMap["Authorize"] = AuthorizationHandler;

    if( std::holds_alternative<Call>(frame) )
    {
        Call c = std::get<Call>(frame);
        if( handlerMap.find(c.action) != handlerMap.end() )
        {
            //the output of handlerMap is now OcppFrame. It should be passed onto holds_alternative<Call>
            //holds_alternative<CallError> and dealt with accordingly
            json j = handlerMap[c.action](c);
            message = j.dump();
        }
        else{
            json error_details = {
                {"hint", "Supported actions: BootNotification, Authorization"}
            };
            
            CallError ce {
                4,
                c.messageId,
                "NotImplemented",
                "Unknown action: " + c.action,
                error_details
            };
            //setup a CallError
            //convert it into json
            json j = ce;
            message = j.dump();
        }
        // message = j.dump();
    }
#endif

    // Echo it back
    boost::asio::write(socket, boost::asio::buffer(message + "\n"));

    return 0;
}
