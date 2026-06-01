#pragma once

// Internal API for project_apply, exposed to unit tests. Not part of the
// public CLI surface.

#include "../exception_dispatch.hpp"   // MutationExceptionMap

#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <unordered_map>

namespace bambu_cli {

// Maximum number of operations allowed in a single manifest.
inline constexpr std::size_t MAX_MANIFEST_OPS = 10000;

// Validate the top-level shape of a parsed manifest JSON value:
//   - must be an object
//   - version key present and integer == 1
//   - operations key present and array
//   - no unknown top-level keys
//   - operations.size() <= MAX_MANIFEST_OPS
//   - every step is an object with a non-empty string "op" key
//
// Throws ManifestFieldError on any failure. Does NOT validate per-op
// field shapes (that's the per-handler responsibility during dispatch).
void parse_and_validate_manifest(const nlohmann::json& m);

// One handler per op. Receives the full step object (including its `op`
// field, which handlers ignore via require_only). Throws on any error.
class ProjectState;
using OpHandler = std::function<void(ProjectState&, const nlohmann::json&)>;

struct HandlerEntry {
    OpHandler            fn;
    MutationExceptionMap overrides;   // empty for ops without exit-7 remapping
};

class HandlerRegistry {
public:
    HandlerRegistry();
    const HandlerEntry& lookup(const std::string& op) const;
private:
    std::unordered_map<std::string, HandlerEntry> m_handlers;
};

} // namespace bambu_cli
