#ifndef ROUTER_HPP
#define ROUTER_HPP
#include <variant>
#include <iostream>
#include <json.hpp>
#include <string>
#include <tl/expected.hpp>
#include "ocpp_model.hpp"

//TODO: FUKCING write the purpose of this class along with examples of usage
class Router{
public:
    using Json = nlohmann::json;

    OcppFrame route(const Call& c);
    //a method for registering the callback so that route can use it.
    void registerHandler(std::string action, std::function<OcppFrame(const Call&)> handler);

    //Req = payload in call
    //Conf = payload in callresult
    template<typename Tag, typename Req, typename Conf>
    using Handler = std::function<tl::expected<Conf, /*CallError spec*/ std::string>(const Req&)>;
    template<typename Tag, typename Req, typename Conf>
    void register_handler(Handler<Tag, Req, Conf> h){
        //use this handler to get a response for the incoming payload
        //attach that response to the map of handlers
        // table_[OcppActionName<Tag>::value] = [h](const std::string& uniqueId,
        table_[Tag::value] = [h](const std::string& uniqueId,
                             const Json& payload,
                             std::function<void(std::string&&)> send) {
            try{
                auto req = payload.get<Req>();
                auto res = h(req);
                if( res ) {
                    Json reply = json::array({3, uniqueId, *res});
                    send(reply.dump());//dumpstr
                }
                else{
                    Json reply = json::array({4, uniqueId, "InternalError", res.error(), json::object()});
                    send(reply.dump());
                }
            }catch(const std::exception &e){
                //send error response
                Json reply = json::array({4, uniqueId, "FormationViolation", e.what(), json::object()});
                send(reply.dump());
            }
        
        };
    };
    void handle_incoming(std::string_view frame, std::function<void(std::string&&)> send);
    void send_error( std::string messageId, std::string errorCode, std::string errorDescription, json errorDetails, std::function<void(std::string&&)> send);

    template<typename PayloadType>
    void addHandler(std::function<OcppFrame(const PayloadType&, const std::string& )> handler){
        std::string action = OcppActionName<PayloadType>::value;
        handlerMap[action] = [handler](const Call& c) -> OcppFrame {
            PayloadType req = c.payload.get<PayloadType>();
            return handler(req, c.messageId);
        };
    }
private:
    std::map<std::string, std::function<OcppFrame(const Call&)>> handlerMap;
    using Invoker = std::function<void(const std::string& uniqueId, const Json& payload, std::function<void(std::string&&)> send)>;
    std::unordered_map<std::string, Invoker> table_;
};


#endif /* ROUTER_HPP*/