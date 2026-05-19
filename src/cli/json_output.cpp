#include "json_output.hpp"

#include <iostream>
#include <sstream>

namespace bambu_cli {

std::string json_escape(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b";  break;
            case '\f': o << "\\f";  break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o << buf;
                } else o << c;
        }
    }
    return o.str();
}

void emit_ok(OutputMode mode, const std::string& code, const std::string& message,
             const std::string& data_json) {
    if (mode == OutputMode::Json) {
        std::cout << R"({"status":"ok","code":")" << json_escape(code)
                  << R"(","message":")" << json_escape(message) << "\"";
        if (!data_json.empty()) std::cout << R"(,"data":)" << data_json;
        std::cout << "}" << std::endl;
    } else {
        std::cout << message << std::endl;
    }
}

void emit_error(OutputMode mode, const std::string& code, const std::string& message) {
    if (mode == OutputMode::Json) {
        std::cerr << R"({"status":"error","code":")" << json_escape(code)
                  << R"(","message":")" << json_escape(message) << "\"}" << std::endl;
    } else {
        std::cerr << code << ": " << message << std::endl;
    }
}

} // namespace bambu_cli
