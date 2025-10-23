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
            {"hint", "supported actions: HeartBeat,BootNotification, Authorization"}
        };

        return CallError{
            4,
            c.messageId,
            "NotImplemented",
            "Unknown action: " + c.action,
            error_details
        };
    }
}

void Router::registerHandler(std::string action, std::function<OcppFrame(const Call&)> handler)
{
    handlerMap[action] = std::move(handler);
}

void Router::handle_incoming(std::string_view frame, std::function<void(std::string&&)> send)
{
    //if its a frame, then i'd need to convert it to Call and get the action and index the map
    try
    {
        auto arr = Json::parse(frame);
        if( !arr.is_array() || arr.size() < 3 )
        {
            send_error("","FormationViolation", "Invalid frame format", json::object(), send);
            return;
        }
        
        int messageTypeId = arr.at(0).get<int>();

        //only support Call(arr[0] = 2)
        if( messageTypeId != 2 ) {
            std::string bestEffortId;
            try{
                bestEffortId = arr.at(1).get<std::string>();
            }catch(...){}

            send_error(bestEffortId, "ProtocolError", "Expected Call (messageTypeId 2) on this path", json::object(), send);
            return;
        }

        //Call needs to have [2, messageId, action, payload]
        if( arr.size() != 4 ) {
            std::string bestEffortId;
            try{
                bestEffortId = arr.at(1).get<std::string>();
            }catch(...){}
            send_error(bestEffortId, "FormationViolation", "Call must have 4 elements[2, messageId, action, payload]", json::object(), send);
            return;
        }

        std::string action = arr.at(2).get<std::string>();
        if( table_.find(action) != table_.end() )
        {
            std::string msgId;
            try{ msgId = arr.at(1).get<std::string>();} catch(...){ msgId.clear();}
            Json payload = arr.at(3);

            // table_[action](arr.at(1).get<std::string>(), arr.at(3), send);
            table_[action](msgId, payload, send);
        }
        else
        {
            //create CallError indicating unknown action
            send_error(arr.at(1), "NotImplemented", "Unknown action: " + action, json::object(), send);
            return;
        }

    }
    catch( const std::exception &e )
    {
        std::cout<<"Router::handle_incoming caught exception: " << e.what() << "\n";
        send_error("", "FormationViolation", e.what(), json::object(), send);
    }

}

// struct CallError {
//     int messageTypeId;              // must be 4
//     std::string messageId;
//     std::string errorCode;
//     std::string errorDescription;
//     json errorDetails;
// };
void Router::send_error( std::string messageId, std::string errorCode, std::string errorDescription, json errorDetails, std::function<void(std::string&&)> send)
{
    json reply = json::array({4, messageId, errorCode, errorDescription, errorDetails});
    send(reply.dump());
}