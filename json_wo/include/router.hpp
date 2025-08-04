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
private:
    std::map<std::string, std::function<OcppFrame(const Call&)>> handlerMap;
};


#endif /* ROUTER_HPP*/