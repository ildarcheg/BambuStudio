#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace bambu_cli {

// Output mode: human-readable text (default) or JSON Shape A.
enum class OutputMode { Text, Json };

// Emit an "ok" message to stdout.
//   - text mode: <message> + newline
//   - json mode: {"status":"ok","code":<code>,"message":<message>,"data":<data>}
// <data> is omitted from the JSON envelope when it is null (default).
void emit_ok(OutputMode mode, const std::string& code, const std::string& message,
             const nlohmann::json& data = nullptr);

// Emit an "error" message to stderr.
//   - text mode: "<code>: <message>" to stderr
//   - json mode: {"status":"error","code":<code>,"message":<message>}  (still stderr)
void emit_error(OutputMode mode, const std::string& code, const std::string& message);

} // namespace bambu_cli
