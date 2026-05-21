#include "json_output.hpp"

#include <iostream>

namespace bambu_cli {

void emit_ok(OutputMode mode, const std::string& code, const std::string& message,
             const nlohmann::json& data) {
    if (mode == OutputMode::Json) {
        nlohmann::json envelope;
        envelope["status"]  = "ok";
        envelope["code"]    = code;
        envelope["message"] = message;
        if (!data.is_null()) envelope["data"] = data;
        std::cout << envelope.dump() << std::endl;
    } else {
        std::cout << message << std::endl;
    }
}

void emit_error(OutputMode mode, const std::string& code, const std::string& message) {
    if (mode == OutputMode::Json) {
        nlohmann::json envelope;
        envelope["status"]  = "error";
        envelope["code"]    = code;
        envelope["message"] = message;
        std::cerr << envelope.dump() << std::endl;
    } else {
        std::cerr << code << ": " << message << std::endl;
    }
}

} // namespace bambu_cli
