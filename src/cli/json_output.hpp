#pragma once

#include <string>

namespace bambu_cli {

// Output mode: human-readable text (default) or JSON Shape A.
enum class OutputMode { Text, Json };

// Emit an "ok" message to stdout.
//   - text mode: <message> + newline
//   - json mode: {"status":"ok","code":<code>,"message":<message>}
void emit_ok(OutputMode mode, const std::string& code, const std::string& message,
             const std::string& data_json = {});

// Emit an "error" message to stderr.
//   - text mode: "<code>: <message>" to stderr
//   - json mode: {"status":"error","code":<code>,"message":<message>}  (still stderr)
void emit_error(OutputMode mode, const std::string& code, const std::string& message);

// JSON-escape a string for inclusion as a JSON string value.
std::string json_escape(const std::string& s);

} // namespace bambu_cli
