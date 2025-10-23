#ifndef ROUTER_HPP
#define ROUTER_HPP
#include <variant>
#include <iostream>
#include <json.hpp>
#include <string>
#include <tl/expected.hpp>
#include "ocpp_model.hpp"

/*
Router v2 — Typed JSON dispatch (register_handler + handle_incoming)

Overview
- v2 lets you register type-safe handlers that receive deserialized OCPP request payloads
  and produce confirmation payloads, while the router handles JSON parsing and framing.
- Use when you receive raw OCPP JSON text (e.g., over WebSocket).

Frame shapes (OCPP 1.6J)
- Call:        [2, "<messageId>", "<Action>", <PayloadObject>]
- CallResult:  [3, "<messageId>", <PayloadObject>]
- CallError:   [4, "<messageId>", "<ErrorCode>", "<ErrorDescription>", <ErrorDetailsObject>]

Key rules
- Payloads are JSON objects per action schema (e.g., Authorize.req: {"idTag":"..."}).
- Only the outer envelope is an array.

Types
- Handler<Tag, Req, Conf> = std::function<tl::expected<Conf, std::string>(const Req&)>
  - Req  — request type (from_json must exist)
  - Conf — confirmation/response type (to_json must exist)
  - Return tl::expected with Conf on success, or std::string error on failure.

How to use (v2)
1) Define JSON models for your actions:
   - Provide to_json/from_json(Req) and to_json/from_json(Conf).
     Example: BootNotificationResponse must serialize as object:
       {"currentTime":"...","interval":10,"status":"Accepted"}.

2) Implement a handler:
   auto bootHandler_v2 = [&](const BootNotification& req)
       -> tl::expected<BootNotificationResponse, std::string> {
       return BootNotificationResponse{"2025-07-16T12:00:00Z", 10, "Accepted"};
   };

3) Register the handler:
   router.register_handler<OcppActionName<BootNotification>, BootNotification, BootNotificationResponse>(bootHandler_v2);

4) Dispatch incoming frames:
   std::string frame = R"([2,"abc","BootNotification",{"chargePointModel":"X","chargePointVendor":"Y"}])";
   router.handle_incoming(frame, [&](std::string&& reply){
       // reply will be a serialized CallResult or CallError string
       // Forward it to your serialized write queue; avoid throwing.
   });

Behavior of handle_incoming
- Parses JSON and validates minimal Call shape.
- Extracts uniqueId, action name, payload JSON.
- If a handler is registered:
  - payload.get<Req>() → h(req)
  - On success: sends CallResult [3, uniqueId, Conf-as-JSON].
  - On handler error (expected string): sends CallError
      [4, uniqueId, "InternalError", <error>, {}].
- If parsing/shape/JSON conversion fails: sends CallError
    [4, uniqueId, "FormationViolation", <what()>, {}].
- If messageTypeId is wrong (≠ 2): sends CallError
    [4, uniqueId, "ProtocolError", "Unsupported messageTypeId", {}].
- If action is unknown: sends CallError
    [4, uniqueId, "NotImplemented", "Unknown action: <Action>", {}].

Send callback contract
- The router calls send(std::string&&) exactly once per incoming frame path.
- Current implementation: exceptions thrown by send(...) are caught inside the invoker
  and converted into a "FormationViolation" CallError, leading to a second send.
  Therefore, ensure your send callback does not throw (e.g., guard std::promise::set_value()).

Tips
- Prefer semantic comparisons in tests:
    json::parse(actual) == json::parse(expected)  // ignores object key order
- Link with -pthread when using std::promise/std::future in
*/

class Router{
public:
    using Json = nlohmann::json;

    OcppFrame route(const Call& c);
    void registerHandler(std::string action, std::function<OcppFrame(const Call&)> handler);
    template<typename Tag, typename Req, typename Conf>
    using Handler = std::function<tl::expected<Conf, /*CallError spec*/ std::string>(const Req&)>;

    /**
     * register_handler — Register a type-safe OCPP action handler (OCPP 1.6J).
     *
     * Associates the action name Tag::value with an invoker that:
     *   1) Deserializes payload to Req via nlohmann::json::get<Req>().
     *   2) Invokes user-provided handler h(req) returning tl::expected<Conf, std::string>.
     *   3) On success, sends a CallResult frame: [3, "<uniqueId>", <Conf-as-JSON>].
     *   4) On handler error (expected error string), sends CallError:
     *        [4, "<uniqueId>", "InternalError", <error-string>, {}].
     *   5) On exceptions during deserialization/handling, sends CallError:
     *        [4, "<uniqueId>", "FormationViolation", <what()>, {}].
     *
     * Notes
     * - Payloads for OCPP Calls are JSON objects; only the outer envelope is an array.
     * - Conf is serialized with your to_json(Conf) overload.
     * - The send callback is invoked exactly once per invocation path.
     * - In the current implementation, exceptions thrown by send(...) are also caught
     *   and converted to a "FormationViolation" CallError.
     *
     * Template parameters:
     *   Tag  — provides static constexpr const char* value (the action name).
     *   Req  — request type to parse from JSON payload.
     *   Conf — confirmation/response type to serialize in CallResult.
     *
     * Parameters:
     *   h — std::function<tl::expected<Conf, std::string>(const Req&)>
     *       User logic producing either Conf (success) or error string (failure).
     */
    template<typename Tag, typename Req, typename Conf>
    void register_handler(Handler<Tag, Req, Conf> h){
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

// ...existing code...
    /**
     * handle_incoming — Parse and dispatch a raw OCPP frame (JSON text) to a registered handler.
     *
     * Input (expected shape, OCPP 1.6J Call):
     *   [ 2, "<messageId>", "<Action>", <Payload> ]
     *   - messageTypeId must be 2 (Call).
     *   - Action must be a string present in the router’s table_.
     *   - Payload is passed as nlohmann::json (typically an object per action schema).
     *
     * Behavior:
     *   - Parses `frame` as JSON.
     *   - Validates it is an array of size 4 with messageTypeId == 2.
     *   - Extracts messageId, action, and payload, then invokes the registered handler:
     *       table_[action](messageId, payload, send);
     *   - On unknown action, emits a CallError "NotImplemented".
     *
     * Errors → send() a CallError frame and return:
     *   - Malformed/invalid shape:     "FormationViolation"
     *   - Wrong messageTypeId (≠ 2):  "ProtocolError"
     *   - Unknown action:              "NotImplemented"
     *
     * Exception safety:
     *   - Catches std::exception, logs, and emits a "FormationViolation" error via send().
     *   - Intended not to throw to caller; any transport-callback exceptions propagate to the outer try and are converted to an error reply.
     *
     * Parameters:
     *   - frame: JSON text of the incoming OCPP message.
     *   - send:  Callback to deliver a serialized reply frame (std::string); called exactly once on each path.
     */
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