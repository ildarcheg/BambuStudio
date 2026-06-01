#pragma once

// Helpers shared by every project_apply handler.
//
// All schema-shape errors thrown by helpers in this header are
// ManifestFieldError (a std::invalid_argument subclass). The
// exception_dispatch short-circuit routes them to exit 1
// (usage_error) regardless of per-op override maps.

#include "exception_dispatch.hpp"
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

// Parse the translate/rotate/scale sections of an object.add step and
// return a populated ManualTransform with the corresponding has_* flags.
// Object form: {"x":N,"y":N,"z":N}, missing axes default to 0 (translate/
// rotate) or 1 (scale). `scale` also accepts a bare number as uniform
// shorthand. An empty section ({"translate": {}}) is treated as not
// present (the flag stays false).
// Throws ManifestFieldError on type mismatch or unknown axis key.
ManualTransform parse_transform(const nlohmann::json& step);

// Carries failing-key context for a config.set/values or config.unset/keys
// mid-batch failure. The handler classifies the inner exception via
// exception_dispatch::dispatch(inner) BEFORE throwing, so the
// outer dispatcher can extract Dispatched + failing_key in one catch.
class ConfigBatchError : public std::exception {
public:
    ConfigBatchError(std::string failing_key,
                     exception_dispatch::Dispatched dispatched);
    const char* what() const noexcept override;
    const std::string&                       failing_key() const noexcept;
    const exception_dispatch::Dispatched&    dispatched()  const noexcept;
private:
    std::string                       m_what;
    std::string                       m_failing_key;
    exception_dispatch::Dispatched    m_dispatched;
};

} // namespace bambu_cli
