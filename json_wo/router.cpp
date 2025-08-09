#include "router.hpp"

OcppFrame Router::route(const Call& c) 
{
    if( handlerMap.find(c.action) != handlerMap.end() )
    {
        std::cout<<"Routing action: " << c.action << std::endl;
        return handlerMap[c.action](c);
    }
    else
    {
        json error_details = {
            {"hint", "supported actions: BootNotification, Authorization"}
        };

        return CallError{
            4,
            c.messageId,
            "Not Implemented",
            "Unknown action: " + c.action,
            error_details
        };
    }
}

void Router::registerHandler(std::string action, std::function<OcppFrame(const Call&)> handler)
{
    handlerMap[action] = std::move(handler);
}

// template<typename PayloadType>
// void Router::fuckHandler(std::function<OcppFrame(const PayloadType& p)> handler)
// {
//     // handlerMap[action] = 
//     std::string action = OcppActionName<PayloadType>::value;
//     handlerMap[action] = [handler](const Call& c) -> OcppFrame {
//         return handler(c.payload);
//     };
// }