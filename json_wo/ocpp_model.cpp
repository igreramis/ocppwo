#include "ocpp_model.hpp"

void from_json(const json &x, Call &c)
{
    c.messageTypeId = x.at(0);
    c.messageId = x.at(1);
    c.action= x.at(2);
    c.payload = x.at(3);
}

void to_json(json &j, const Call &c)
{
    j = json::array({c.messageTypeId, c.messageId, c.action, c.payload});
}

void from_json(const json &x, CallResult &c)
{
    c.messageTypeId = x.at(0);
    c.messageId = x.at(1);
    c.payload = x.at(2);
}

void to_json(json &j, const CallResult& c)
{
    j = json::array({c.messageTypeId, c.messageId, c.payload});
}

void from_json(const json &x, CallError &c)
{
    c.messageTypeId = x.at(0);
    c.messageId = x.at(1);
    c.errorCode = x.at(2);
    c.errorDescription = x.at(3);
    c.errorDetails = x.at(4);
}

void to_json(json &j, const CallError& c)
{
    j = json::array({c.messageTypeId, c.messageId, c.errorCode, c.errorDescription, c.errorDetails});
}

void from_json(const json& j, BootNotification& b)
{
    // b.chargePointModel = j.at("chargePointModel");
    // b.chargePointVendor = j.at("chargePointVendor");
    b.chargePointModel = j.at(0);
    b.chargePointVendor = j.at(1);
}

void to_json(json& j, const BootNotification& b)
{
    j = json::array({b.chargePointModel, b.chargePointVendor});
}

void to_json(json& j, const BootNotificationResponse& r)
{
    j = json::array({r.currentTime, r.interval, r.status});
}

void from_json(const json& j, BootNotificationResponse& r)
{
    std::cout<<"in from_json()..."<<std::endl;
    r.currentTime = j.at(0);
    r.interval = j.at(1);
    r.status = j.at(2);
}

void to_json(json& j, const AuthorizeResponse& r)
{
    j = json::array({r.idTagInfo});
}

void from_json(const json& j, AuthorizeResponse& r)
{
    r.idTagInfo = j.at(0);
}

void from_json(const json& j, Authorize& a)
{
    a.idTag = j.at(0);
}

void to_json(json& j, const Authorize& a)
{
    j = json::array({a.idTag});
}

OcppFrame parse_frame(json &x)
{
    if( !x.is_array() || x.size() < 3 )
        throw std::runtime_error("Invalid OCPP frame format");

    int messageTypeId = x.at(0);
    std::string messageId = x.at(1);

    switch(x.at(0).get<int>())
    {
        case 2://Call
            return x.get<Call>();
        break;
        case 3://CallResult
        {
            return x.get<CallResult>();
        }
        break;
        case 4:
            return x.get<CallError>();
        default:
        {
            throw std::runtime_error("Unknown messageTypeId: " + std::to_string(messageTypeId));
        }
    }

    throw std::logic_error("Unreachable: parse_frame fell through without return");
}

void handle_call(const Call& x) {
    std::cout << "[Call] TypeId: "<< x.messageTypeId
              << ",messageId: " << x.messageId
              << ",action: " << x.action
              << ", payload: " << x.payload << std::endl;
}

void handle_call_result(const CallResult& x) {
    std::cout << "[CallResult] messageTypeId: "<< x.messageTypeId
              << ", messageId: "<< x.messageId
              << ", payload: " << x.payload<<std::endl;
}

void dispatch_frame(const OcppFrame& message)
{
    std::visit([](auto &message){
        using T = std::decay_t<decltype(message)>;
        if constexpr(std::is_same_v<T, Call>) {
            handle_call(message);
        }
        else if constexpr(std::is_same_v<T, CallResult>) {
            handle_call_result(message);
        }
    }, message);
}

#if 0
CallResult BootNotificationHandler(const Call& c)
{
    BootNotificationResponse res = {
        "2025-07-16T12:00:00Z",
        300,
        "Accepted"
    };

    return CallResult{
        3,
        c.messageId,
        res
    };
}

CallResult AuthorizationHandler(const Call& c)
{
    Authorize a = c.payload;
    AuthorizeResponse res = {
        a.idTag + "OK",
    };

    return CallResult{
        3,
        c.messageId,
        res
    };
}
#endif

OcppFrame BootNotificationHandler(const Call& c)
{
    BootNotification b = c.payload;
    if( b.chargePointModel.empty() || b.chargePointVendor.empty() )
    {
        json error_details {
            {"hint", "chargePointModel or chargePointVendor empty..."}
        };
        return CallError{
            4,
            c.messageId,
            "304"
            "Unexpected payload",
            error_details
        }
    }
    
    BootNotificationResponse res = {
        "2025-07-16T12:00:00Z",
        300,
        "Accepted"
    };

    return CallResult{
        3,
        c.messageId,
        res
    };
}

// OcppFrame AuthorizationHandler(const Call& c)
// {
//     Authorize a = c.payload;
//     AuthorizeResponse res = {
//         a.idTag + "OK",
//     };

//     return CallResult{
//         3,
//         c.messageId,
//         res
//     };
// }