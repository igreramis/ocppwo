#include "router.hpp"

OcppFrame Router::route(const Call& c) 
{
    if( handlerMap.find(c.action) != handlerMap.end() )
    {
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