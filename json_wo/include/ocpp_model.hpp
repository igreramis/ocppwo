#ifndef OCPP_MODEL_HPP
#define OCPP_MODEL_HPP
#include <json.hpp>
#include <variant>
#include <iostream>
#include <random>
#include <tl/expected.hpp>
using json = nlohmann::json;

struct BootNotification {
    std::string chargePointModel;
    std::string chargePointVendor;
};

struct BootNotificationResponse {
    std::string currentTime;
    int interval;
    std::string status; // e.g. "Accepted"
};

struct Authorize {
    std::string idTag;
};

struct AuthorizeResponse {
    std::string idTagInfo;  // keep simple for now
};

struct HeartBeat {
    // no fields, just a signal
};

struct HeartBeatResponse {
    std::string currentTime; // format: date-time
};

struct UnknownAction {
    // Placeholder for unknown actions
};

struct Call {
    int messageTypeId;              // must be 2
    std::string messageId;          // e.g., "abc123"
    std::string action;             // e.g., "BootNotification"
    json payload;                   // generic until we specialize
};

struct CallResult {
    int messageTypeId;              // must be 3
    std::string messageId;
    json payload;
};

struct CallError {
    int messageTypeId;              // must be 4
    std::string messageId;
    std::string errorCode;
    std::string errorDescription;
    json errorDetails;
};

template<typename T>
struct OcppActionName;

template<>
struct OcppActionName<BootNotification> {
    static constexpr const char* value = "BootNotification";
};

template<>
struct OcppActionName<Authorize> {
    static constexpr const char* value = "Authorize";
};

template<>
struct OcppActionName<HeartBeat> {
    static constexpr const char* value = "HeartBeat";
};

template<>
struct OcppActionName<UnknownAction> {
    static constexpr const char* value = "UnknownAction";
};

using OcppFrame = std::variant<Call, CallResult, CallError>;
using OcppPayload = std::variant<BootNotification, Authorize, HeartBeat>;

void to_json(json &j, const OcppFrame &f);
void from_json(const json &x, Call &c);
void to_json(json &j, const Call &c);
void from_json(const json &x, CallResult &c);
void to_json(json &j, const CallResult& c);
void from_json(const json &x, CallError &c);
void to_json(json &j, const CallError& c);
void to_json(json& j, const BootNotification& b);
void from_json(const json& j, BootNotification& b);
void to_json(json& j, const BootNotificationResponse& r);
void from_json(const json& j, BootNotificationResponse& r);
void to_json(json& j, const Authorize& a);
void from_json(const json& j, Authorize& a);
void to_json(json& j, const AuthorizeResponse& r);
void from_json(const json& j, AuthorizeResponse& r);
void to_json(json& j, const HeartBeat& h);
void from_json(const json& j, HeartBeat& h);
void to_json(json& j, const HeartBeatResponse& r);
void from_json(const json& j, HeartBeatResponse& r);
void to_json(json& j, const UnknownAction& a);
OcppFrame parse_frame(json &x);
void dispatch_frame(const OcppFrame& message);
std::string generate_message_id();
Call create_call(const std::string& id, const OcppPayload& payload);
Call create_call(const std::string& id, const std::string& action, const OcppPayload& payload);

/**
 * create_call
 *
 * Build an OCPP Call frame (messageTypeId == 2) for sending.
 *
 *      Convenience template: deduces the action name via OcppActionName<Payload>::value and
 *      serializes the concrete payload to json(p).
 *
 * Parameters:
 *  - id: unique message id to assign to the Call.
 *  - payload / p: payload content (either variant or concrete payload type).
 *
 * Return value:
 *  - Call: populated Call structure with messageTypeId, messageId, action, and JSON payload.
 *
 * Notes:
 *  - The template overload performs serialization with nlohmann::json.
 *  - Caller is responsible for providing a unique id when required by the protocol.
 */
template<typename Payload>
Call create_call(const std::string &id, const Payload& p) {
    return Call{
        2,
        id,
        OcppActionName<Payload>::value,
        json(p)
    };
};

std::string action_for_payload(const OcppPayload& payload);
void handle_call(const Call& x);
void handle_call_result(const CallResult& x);
#if 0
CallResult BootNotificationHandler(const Call& );
CallResult AuthorizationHandler(const Call& );
#endif
OcppFrame BootNotificationHandler(const BootNotification&, const std::string& );
OcppFrame AuthorizeHandler(const Authorize&, const std::string& );
tl::expected<AuthorizeResponse, std::string> AuthorizeHandler_v2(const Authorize& );
OcppFrame HeartBeatHandler(const HeartBeat&, const std::string& );
tl::expected<HeartBeatResponse, std::string> HeartBeatHandler_v2(const HeartBeat& );
#endif //OCPP_MODEL_HPP