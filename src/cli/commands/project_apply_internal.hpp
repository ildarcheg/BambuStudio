#pragma once

// Internal API for project_apply, exposed to unit tests. Not part of the
// public CLI surface.

#include <nlohmann/json.hpp>
#include <string>

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

} // namespace bambu_cli
