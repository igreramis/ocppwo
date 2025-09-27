#include "ocpp_model.hpp"

void to_json(json &j, const OcppFrame &f)
{
    std::visit([&](auto msg){
        using T = std::decay_t<decltype(msg)>;
        if( std::is_same_v<T, Call> || std::is_same_v<T, CallResult> || std::is_same_v<T, CallError>) {
            j = json(msg);
        }
        else {
            throw std::runtime_error("Uknown OcppFrame type");
        }
    }, f);
}

void to_json(json &j, const Call &c)
{
    j = json::array({c.messageTypeId, c.messageId, c.action, c.payload});
}

void from_json(const json &x, Call &c)
{
    c.messageTypeId = x.at(0);
    c.messageId = x.at(1);
    c.action= x.at(2);
    c.payload = x.at(3);
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

void to_json(json& j, const HeartBeat& h)
{
    j = json::object();
}

void from_json(const json& j, HeartBeat& h)
{
    // no fields, just a signal
    // nothing to do here
}

void to_json(json& j, const HeartBeatResponse &r)
{
    j = json::array({r.currentTime});
}

void from_json(const json& j, HeartBeatResponse &r)
{
    r.currentTime = j.at(0);
}

void to_json( json& j, const UnknownAction& a ) {
    j = json::object();
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
        {
            return x.get<Call>();
        }
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

Call create_call(const std::string& id, const std::string& action, const OcppPayload& payload){
    Call c;
    c.messageTypeId = 2;
    c.messageId = id;
    c.action = action;
    c.payload = std::visit([](const auto& payload){
        return nlohmann::json(payload);
    }, payload);

    return c;
}

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
        else if constexpr ( std::is_same_v<T, HeartBeat> )
        {
            return "Heartbeat";
        }
        else
        {
            static_assert(!sizeof(T), "Unhandled payload type");
        }
    }, payload);
}

std::string generate_message_id() {
    static std::mt19937 rng{std::random_device{}()};
    static std::uniform_int_distribution<int> dist(0, 15);

    std::string id = "msg_";
    for (int i = 0; i < 8; ++i)
        id += "0123456789ABCDEF"[dist(rng)];

    return id;
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

// OcppFrame BootNotificationHandler(const Call& c)
OcppFrame BootNotificationHandler(const BootNotification& b, const std::string& msgId)
{
    if( b.chargePointModel.empty() || b.chargePointVendor.empty() )
    {
        json error_details {
            {"hint", "chargePointModel or chargePointVendor empty..."}
        };

        return CallError{
            4,
            msgId,
            "304"
            "Unexpected payload",
            error_details
        };
    }
    
    BootNotificationResponse res = {
        "2025-07-16T12:00:00Z",
        5,//300,
        "Accepted"
    };
    std::cout<<"BootNotificationHandler: Returning CallResult"<<std::endl;
    return CallResult{
        3,
        msgId,
        res
    };
}

OcppFrame AuthorizeHandler(const Authorize& a, const std::string& msgId)
{
    AuthorizeResponse res = {
        a.idTag + "OK",
    };

    return CallResult{
        3,
        msgId,
        res
    };
}

OcppFrame HeartBeatHandler(const HeartBeat& h, const std::string& msgId)
{
    // no fields, just a signal
    HeartBeatResponse res = {
        "2025-07-16T12:00:00Z" // example current time
    };

    std::cout<<"HeartBeatHandler: Returning CallResult"<<std::endl;

    return CallResult{
        3,
        msgId,
        res
    };
}