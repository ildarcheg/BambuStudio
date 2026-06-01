#pragma once

// Helpers shared by every project_apply handler.
//
// All schema-shape errors thrown by helpers in this header are
// ManifestFieldError (a std::invalid_argument subclass). The
// exception_dispatch short-circuit routes them to exit 1
// (usage_error) regardless of per-op override maps.

#include "project_ops.hpp"   // ManualTransform

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>

namespace bambu_cli {

// Iterate `step.items()` and throw ManifestFieldError for any key not
// in `known_keys`. Caller passes a brace-enclosed list including "op".
// `step` may be any JSON value; non-objects are accepted as no-ops.
void require_only(const nlohmann::json& step,
                  std::initializer_list<const char*> known_keys);

// Return step[key] as an integer, validating that it is present, an
// integer (not float, not string), and >= 1 (1-based filament slot).
// Throws ManifestFieldError on any failure.
int parse_filament(const nlohmann::json& step, const char* key);

} // namespace bambu_cli
