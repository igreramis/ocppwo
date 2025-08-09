#ifndef ROUTER_HPP
#define ROUTER_HPP
#include <variant>
#include <iostream>
#include <json.hpp>
#include <string>
#include "ocpp_model.hpp"

class Router{
public:
    OcppFrame route(const Call& c);
    //a method for registering the callback so that route can use it.
    void registerHandler(std::string action, std::function<OcppFrame(const Call&)> handler);
    template<typename PayloadType>
    void addHandler(std::function<OcppFrame(const PayloadType&, const std::string& )> handler){
        std::string action = OcppActionName<PayloadType>::value;
        handlerMap[action] = [handler](const Call& c) -> OcppFrame {
            PayloadType req = c.payload.get<PayloadType>();
            // OcppFrame f = handler(req);
            // std::visit([&](auto &msg){
            //     msg.messageId = c.messageId;
            // }, f);
            // return f;
            return handler(req, c.messageId);
        };
    }
private:
    std::map<std::string, std::function<OcppFrame(const Call&)>> handlerMap;
};


#endif /* ROUTER_HPP*/