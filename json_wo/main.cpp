#include <iostream>
#include <fstream>
#include <json.hpp>
#include <variant>
#include <random>
using json = nlohmann::json;
using namespace std::string_literals;
using namespace nlohmann::literals;

// using namespace std;
// int main()
// {
//     std::ifstream f("example.json");
    
//     json data = json::parse (f);

//     cout<<"Hello World"<<endl;
//     cout<<std::setw(4)<<data<<endl;
// }

#if 0
int main()
{
    // create a JSON object
    json j =
    {
        {"pi", 3.141},
        {"happy", true},
        {"name", "Niels"},
        {"nothing", nullptr},
        {
            "answer", {
                {"everything", 42}
            }
        },
        {"list", {1, 0, 2}},
        {
            "object", {
                {"currency", "USD"},
                {"value", 42.99}
            }
        }
    };

    // add new values
    j["new"]["key"]["value"] = {"another", "list"};

    // count elements
    auto s = j.size();
    j["size"] = s;

    // pretty print with indent of 4 spaces
    std::cout << std::setw(4) << j << '\n';
}
#endif

#include <iostream>
#include <vector>
#include <optional>
#include <string>

std::optional<std::string> find_email(const std::vector<std::pair<std::string, std::string>>& users, const std::string& name) {
    // TODO: implement lookup using a loop
    // for each ele in vector:
    //     if ele == name:
    //         return email
    // return nullopt;
    auto it = std::find_if(users.begin(), users.end(), [&](const std::pair<std::string , std::string > &user){
        return user.first == name;
    });
    
    if (it != users.end()){
        return it->second;
    }
    else
        return std::nullopt;
}

#if 0
int main() {
    std::vector<std::pair<std::string, std::string>> users = {
        {"Ali", "ali@example.com"},
        {"Sam", "sam@example.com"},
        {"Tina", "tina@example.com"}
    };
#if 0
    auto result = find_email(users, "Akram");
    if (result) {
        std::cout << "Email found: " << *result << "\n";
    } else {
        std::cout << "User not found.\n";
    }
#endif

    std::string email = find_email(users, "John").value_or("unknown@example.com");
    std::cout << "Email: " << email << "\n";

    return 0;
}
#endif

#if 0
#include <iostream>
#include <optional>

std::optional<int> cached_result;

int compute_heavy_value() {
    std::cout << "Computing heavy value...\n";
    return 42;
}

int get_result() {
    if (!cached_result) {
        cached_result = compute_heavy_value();
    }
    return *cached_result;
}

int main() {
    std::cout << "First call: " << get_result() << "\n";
    std::cout << "Second call: " << get_result() << "\n";  // Should not recompute
}
#endif

////
#if 0
#include <iostream>
#include <variant>
#include <string>

using VarType = std::variant<int, float, std::string>;

void print_variant(const VarType& v) {
    // TODO: Use std::visit with a lambda to print the held value and its type
    std::visit([&](auto&& arg){
        if(auto ptr = std::get_if<int>(&v))
            std::cout<<"It's an int: "<<*ptr<<std::endl;
        if(auto ptr = std::get_if<float>(&v))
            std::cout<<"It's a float: "<<*ptr<<std::endl;
        if(auto ptr = std::get_if<std::string>(&v))
            std::cout<<"It's a string: "<<*ptr<<std::endl;
    }, v);
}

int main() {
    VarType a = 42;
    VarType b = 3.14f;
    VarType c = std::string("Hello");

    print_variant(a);
    print_variant(b);
    print_variant(c);
}
#endif

#if 0
#include <iostream>
#include <variant>
#include <string>

using VarType = std::variant<int, float, std::string>;

struct Overload {
    auto operator()(int i){ return " It's an int: "<<i<<"\n";}
    auto operator()(float f){ return "It's a float: "<<f<<"\n";}
    auto operator()(std::string str){return "It's a string: "<<str<<"\n";}
};

void print_variant(const VarType& v) {
    // TODO: Use std::visit with a lambda to print the held value and its type
    std::visit(Overload, v);
}

int main() {
    VarType a = 42;
    VarType b = 3.14f;
    VarType c = std::string("Hello");

    print_variant(a);
    print_variant(b);
    print_variant(c);
}
#endif

#if 0
using VarType = std::variant<int, float, std::string>;

struct MyVisitor {
    void operator()(int i) const {
        std::cout<<"int: "<< i << "\n";
    }

    void operator()(float f) const {
        std::cout<<"float: "<< f << "\n";
    }

    void operator()(std::string s) const {
        std::cout<<"string: "<< s << "\n";
    }
};

int main() {
    std::variant<int, float, std::string> v = std::string("Ali");
    std::visit(MyVisitor{}, v);
    
    v = 5;
    std::visit(MyVisitor{}, v);

    v = 5.0f;
    std::visit(MyVisitor{}, v);
}
#endif

#if 0
using JsonValue = std::variant<int, double, std::string, bool, std::nullptr_t>;

struct JsonValueVisitor {
    void operator()(int i) const {
        std::cout<<"int: "<< i << "\n";
    }

    void operator()(double d) const {
        std::cout<<"double: "<< d << "\n";
    }

    void operator()(std::string s) const {
        std::cout<<"string: "<< s << "\n";
    }

    void operator()(bool b) const {
        std::cout<<"bool: "<< b << "\n";
    }

    void operator()(std::nullptr_t np) const {
        std::cout<<"nullptr_t: "<< np << "\n";
    }
};

void print_json_value(JsonValue val){
    //map and visitor
    std::visit(JsonValueVisitor{}, val);
}


int main() {
    std::vector<JsonValue> values = {
        42,
        3.14,
        "example"s,
        true,
        nullptr
    };

    for( const auto val : values )
    {
        print_json_value(val);
    }
}
#endif

#if 0
struct BootNotification {
    std::string chargePointModel;
    std::string chargePointVendor;
};

struct Heartbeat {};

struct Authorize {
    std::string idTag;
};

using OcppMessage = std::variant<BootNotification, Heartbeat, Authorize>;

void handle_boot(const BootNotification& msg);
void handle_heartbeat(const Heartbeat& msg);
void handle_authorize(const Authorize& msg);

void handle_boot(const BootNotification& msg) {
    std::cout << "[BootNotification] Model: " << msg.chargePointModel
              << ", Vendor: " << msg.chargePointVendor << "\n";
}

void handle_heartbeat(const Heartbeat& ) {
    std::cout << "[Heartbeat]\n";
}

void handle_authorize(const Authorize& msg) {
    std::cout << "[Authorize] ID Tag: "<< msg.idTag << "\n";
}

void dispatch_message(const OcppMessage& message) {
    std::visit([](auto &message){
        //decode type of visitor and then, based on the type, call the handler
        using T = std::decay_t<decltype(message)>;

        if constexpr (std::is_same_v<T, BootNotification>) {
            handle_boot(message);
        }
        else if constexpr (std::is_same_v<T, Heartbeat>) {
            handle_heartbeat(message);
        }
        else if constexpr (std::is_same_v<T, Authorize>) {
            handle_authorize(message);
        }

    }, message);
}

int main() {
    OcppMessage msg1 = BootNotification{ "X100", "OpenAI" };
    OcppMessage msg2 = Heartbeat{};
    OcppMessage msg3 = Authorize{ "TAG-1234" };

    dispatch_message(msg1);
    dispatch_message(msg2);
    dispatch_message(msg3); 

    return 0;
}
#endif

#if 0
struct BootNotification {
    std::string chargePointModel;
    std::string chargePointVendor;
};

struct Heartbeat {};

struct Authorize {
    std::string idTag;
};

void handle_boot(const BootNotification& msg);
void handle_heartbeat(const Heartbeat& msg);
void handle_authorize(const Authorize& msg);

void handle_boot(const BootNotification& msg) {
    std::cout << "[BootNotification] Model: " << msg.chargePointModel
              << ", Vendor: " << msg.chargePointVendor << "\n";
}

void handle_heartbeat(const Heartbeat& ) {
    std::cout << "[Heartbeat]\n";
}

void handle_authorize(const Authorize& msg) {
    std::cout << "[Authorize] ID Tag: "<< msg.idTag << "\n";
}

using OcppMessage = std::variant<BootNotification, Heartbeat, Authorize>;

void dispatch_message(const OcppMessage& message) {
    std::visit([](auto &message){
        //decode type of visitor and then, based on the type, call the handler
        using T = std::decay_t<decltype(message)>;

        if constexpr (std::is_same_v<T, BootNotification>) {
            handle_boot(message);
        }
        else if constexpr (std::is_same_v<T, Heartbeat>) {
            handle_heartbeat(message);
        }
        else if constexpr (std::is_same_v<T, Authorize>) {
            handle_authorize(message);
        }

    }, message);
}

int main(){
    std::string input = R"({
      "action": "BootNotification",
      "payload": {
        "chargePointModel": "X100",
        "chargePointVendor": "OpenAI"
       }
    })";
    //parse using json
    json jsonInput = json::parse(input);

    //setup relevant structs
    std::cout<<jsonInput.at("action")<<std::endl;

    //make use in OcppMessage
    OcppMessage ocppMessage = BootNotification{
                                    jsonInput.at("payload").at("chargePointModel"),
                                    jsonInput.at("payload").at("chargePointVendor")
                                  };
    //dispatch usin existing dispatch_message()
    dispatch_message(ocppMessage);
    return 0;
}
#endif

#if 0
//[2, "abc123", "BootNotification", { "chargePointModel": "X100", "chargePointVendor": "OpenAI" }]
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

using OcppFrame = std::variant<Call, CallResult, CallError>;

//void from_json(json &x, Call &c) 
void from_json(const json &x, Call &c)
{
    c.messageTypeId = x.at(0);
    c.messageId = x.at(1);
    c.action= x.at(2);
    c.payload = x.at(3);
}

void to_json(json &j, Call &c)
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
//void from_json(json &x, CallResult &c)
//void from_json(json &x, CallError &c)
//to_json

//parse frame returns one of three different possible OcppFrame types: Call, CallResult, CallError
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
// struct Call {
//     int messageTypeId;              // must be 2
//     std::string messageId;          // e.g., "abc123"
//     std::string action;             // e.g., "BootNotification"
//     json payload;                   // generic until we specialize
// };
void handle_call(const Call& x) {
    std::cout << "[Call] TypeId: "<< x.messageTypeId
              << ",messageId: " << x.messageId
              << ",action: " << x.action
              << ", payload: " << x.payload << std::endl;
}

void handle_call_result(const CallResult
& x) {
    std::cout << "[CallResult]"<<std::endl;
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
int main() {
    std::string input = R"([
        2,
        "abc123",
        "BootNotification",
        {
            "chargePointModel": "X100",
            "chargePointVendor": "OpenAI"
        }
    ])";

    json j = json::parse(input);
    /*once parsed, aboves string becomes json array of 4 elements
            0 number 2
            1 string "abc123"
            2 string "BootNotification"
            3 object {"chargePointModel":"X100", ...}
        you can access each of these elements via the .at() method.
        but the .at() method returns a json object. so j.at(0) wouldn't
        return a number, but a json object. To get the number out of the
        json object, you'd have to call .get<int> on it.
    */
    /*
    what is a template? a generic blueprint for a function
        template <typename T>
            T add(T a, T b) {
            return a + b;
        }
        If you call add(1, 2), the compiler deduces T = int.
        But Remember: sometimes, it can't deduce the type. that's when you get errors like:
        error: default type conversion can’t deduce template argument for...
     */

     /*
     SOmething about ADL(arg. dependent lookup) and how your from_json, to_json defined
     functions are being utilized by the library.
     template<typename T>
     T json::get() const {
         T t;
         from_json(*this, t);  // 👈 this is the hook you're providing
         return t;
     }
     CallError e = j.get<CallError>();
      */
    OcppFrame frame = parse_frame(j);
    dispatch_frame(frame);

    return 0;
}
#endif

#if 0
struct BootNotification {
    std::string chargePointModel;
    std::string chargePointVendor;
};

struct Authorize {
    std::string idTag;
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

void from_json(const json& j, BootNotification& b)
{
    b.chargePointModel = j.at("chargePointModel");
    b.chargePointVendor = j.at("chargePointVendor");
}

void from_json(const json& j, Authorize& a)
{
    a.idTag = j.at(0);
}

void from_json(const json &x, Call &c)
{
    c.messageTypeId = x.at(0);
    c.messageId = x.at(1);
    c.action= x.at(2);
    c.payload = x.at(3);
}

void to_json(json &j, Call &c)
{
    j = json::array({c.messageTypeId, c.messageId, c.action, c.payload});
}

using CallPayload = std::variant<BootNotification, Authorize>;

CallPayload decode_call_payload(const Call& call)
{
    //take the call , break it into boot or authorize
    if ( call.action == "BootNotification" )
        return call.payload.get<BootNotification>();
    else if ( call.action == "Authorize" )
        return call.payload.get<Authorize>();
    
    throw std::runtime_error("Unknown action: " + call.action);
}
int main() {
    
    std::string input = R"([
    2,
    "abc123",
    "BootNotification",
    {
        "chargePointModel": "X100",
        "chargePointVendor": "OpenAI"
    }
    ])";

    json j = json::parse(input);
    Call call = j.get<Call>();  // thanks to from_json
    CallPayload payload = decode_call_payload(call);

    // // Now use std::visit to print it
    std::visit([](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, BootNotification>) {
            std::cout << "[Boot] " << p.chargePointModel << ", " << p.chargePointVendor << "\n";
        } else if constexpr (std::is_same_v<T, Authorize>) {
            std::cout << "[Auth] ID: " << p.idTag << "\n";
        }
    }, payload);

    return 0;
}

#endif

#if 1
struct BootNotification {
    std::string chargePointModel;
    std::string chargePointVendor;
};

struct Authorize {
    std::string idTag;
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

void from_json(const json& j, BootNotification& b)
{
    b.chargePointModel = j.at("chargePointModel");
    b.chargePointVendor = j.at("chargePointVendor");
}

void from_json(const json& j, Authorize& a)
{
    a.idTag = j.at(0);
}

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

void to_json(json& j, const BootNotification& b)
{
    j = json::array({b.chargePointModel, b.chargePointVendor});
}

void to_json(json& j, const Authorize& a)
{
    j = json::array({a.idTag});
}

using CallPayload = std::variant<BootNotification, Authorize>;

// Call create_call(const std::string& messageId, const std::string& action, const json& payload)
// {
//     return Call {
//         2,
//         messageId,
//         action,
//         payload
//     };
// }

std::string generate_message_id() {
    static std::mt19937 rng{std::random_device{}()};
    static std::uniform_int_distribution<int> dist(0, 15);

    std::string id = "msg_";
    for (int i = 0; i < 8; ++i)
        id += "0123456789ABCDEF"[dist(rng)];

    return id;
}

template<typename Payload>
Call create_call(const std::string& action, const Payload& p) {
    return Call{
        2,
        generate_message_id(),
        action,
        json(p)
    };
}

#if 0
int main() {
    BootNotification b{"X100", "OpenAI"};
    Call call = create_call("abc123", "BootNotification", b);
    json j = call;

    std::cout << j.dump(2) << "\n";
}
#endif

#if 0
int main() {
    BootNotification b{"X100", "OpenAI"};
    Call c = create_call("BootNotification", b);
    json j = c;

    std::cout << j.dump(2) << "\n";  // should show auto-generated msg ID
}
#endif

#if 1
#include <boost/asio.hpp>
int main() {
    BootNotification b{"X100", "OpenAI"};
    Call c = create_call("BootNotification", b);
    json j = c;

    std::string serialized = j.dump();
    std::stringstream wire;//why is this called as simulating to wire? because from a stringstream, you can read from it and write to it without polluting things with os calls, sockets, etc.
    wire << serialized;
    std::string reply;
    wire >> reply;
    std::cout << "Wire contents: "<<wire.str()<<"\n";
    std::cout << "Wire contents: "<<reply<<"\n";

}
#endif

#endif