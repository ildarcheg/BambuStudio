#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

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
//   - text mode: "<code>: <message>" to stderr (data ignored)
//   - json mode: {"status":"error","code":<code>,"message":<message>, ...<data>}
// <data>, if non-null, is *merged* at the top level (each key becomes a
// sibling of code/message/status). nullptr (default) omits the merge.
void emit_error(OutputMode mode, const std::string& code, const std::string& message,
                const nlohmann::json& data = nullptr);

// emit_list_response<Row, ToJson, ToLine>:
// Shared helper for list-style read-only subcommands (plate list, object list,
// config list, ...). Produces either:
//
//   - JSON mode: a Shape A envelope with
//       data = { <count_key>: <N>, <items_key>: [ to_json(row), ... ] }
//
//   - Text mode: one line per row built by to_line(index, row), concatenated.
//     (to_line is responsible for its own trailing newline so callers can
//     control formatting.)
//
// `to_json` signature: `nlohmann::json (const Row&)`
// `to_line` signature: `std::string (size_t index, const Row&)`
//
// Sibling pattern: matches OrcaSlicer's emit_list_response template at
// src/cli/output.hpp:38-59.
template <typename Row, typename ToJson, typename ToLine>
void emit_list_response(OutputMode mode,
                        const std::string& message,
                        const std::string& count_key,
                        const std::string& items_key,
                        const std::vector<Row>& rows,
                        ToJson&& to_json,
                        ToLine&& to_line)
{
    if (mode == OutputMode::Json) {
        nlohmann::json data;
        data[count_key] = rows.size();
        data[items_key] = nlohmann::json::array();
        for (const auto& row : rows)
            data[items_key].push_back(to_json(row));
        emit_ok(mode, "ok", message, data);
    } else {
        std::ostringstream m;
        for (std::size_t i = 0; i < rows.size(); ++i)
            m << to_line(i, rows[i]);
        emit_ok(mode, "ok", m.str());
    }
}

} // namespace bambu_cli
