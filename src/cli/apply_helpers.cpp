#include "apply_helpers.hpp"

#include "exceptions.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace bambu_cli {

void require_only(const nlohmann::json& step,
                  std::initializer_list<const char*> known_keys)
{
    if (!step.is_object()) return;   // nothing to check
    for (auto it = step.begin(); it != step.end(); ++it) {
        const std::string& key = it.key();
        bool found = std::any_of(known_keys.begin(), known_keys.end(),
                                 [&](const char* k) { return key == k; });
        if (!found) {
            throw ManifestFieldError("unknown field: '" + key + "'");
        }
    }
}

int parse_filament(const nlohmann::json& step, const char* key)
{
    if (!step.contains(key))
        throw ManifestFieldError(std::string("missing required field '") + key + "'");
    const auto& v = step[key];
    if (!v.is_number_integer())
        throw ManifestFieldError(std::string("field '") + key +
                                 "' must be an integer (1-based filament slot)");
    int n = v.get<int>();
    if (n < 1)
        throw ManifestFieldError(std::string("field '") + key +
                                 "' must be >= 1 (got " + std::to_string(n) + ")");
    return n;
}

namespace {

// Apply a translate-style section ({"x":..,"y":..,"z":..} or empty) into
// out_{x,y,z} using `default_v` for missing axes. Returns true if the
// section was present and non-empty (caller flips the has_* flag).
bool read_axis_object(const nlohmann::json& section,
                      const char* section_name,
                      double default_v,
                      double& out_x, double& out_y, double& out_z)
{
    if (!section.is_object())
        throw bambu_cli::ManifestFieldError(std::string("section '") + section_name +
                                 "' must be an object");
    if (section.empty()) return false;
    out_x = out_y = out_z = default_v;
    for (auto it = section.begin(); it != section.end(); ++it) {
        const std::string& key = it.key();
        if      (key == "x") out_x = it.value().get<double>();
        else if (key == "y") out_y = it.value().get<double>();
        else if (key == "z") out_z = it.value().get<double>();
        else throw bambu_cli::ManifestFieldError(std::string("unknown axis key '") + key +
                                      "' on '" + section_name + "'");
    }
    return true;
}

} // anonymous namespace

ManualTransform parse_transform(const nlohmann::json& step)
{
    ManualTransform t;

    if (step.contains("translate")) {
        t.has_translate = read_axis_object(step["translate"], "translate",
                                           0.0, t.tx, t.ty, t.tz);
    }

    if (step.contains("rotate")) {
        t.has_rotate    = read_axis_object(step["rotate"], "rotate",
                                           0.0, t.rx, t.ry, t.rz);
    }

    if (step.contains("scale")) {
        const auto& s = step["scale"];
        if (s.is_number()) {
            double v = s.get<double>();
            t.has_scale = true;
            t.sx = t.sy = t.sz = v;
        } else {
            t.has_scale = read_axis_object(s, "scale", 1.0, t.sx, t.sy, t.sz);
        }
    }

    return t;
}

ConfigBatchError::ConfigBatchError(std::string failing_key,
                                   exception_dispatch::Dispatched dispatched)
    : m_failing_key(std::move(failing_key)),
      m_dispatched(std::move(dispatched))
{
    m_what = "failing_key '" + m_failing_key + "': " + m_dispatched.message;
}
const char* ConfigBatchError::what() const noexcept { return m_what.c_str(); }
const std::string& ConfigBatchError::failing_key() const noexcept { return m_failing_key; }
const exception_dispatch::Dispatched& ConfigBatchError::dispatched() const noexcept { return m_dispatched; }

} // namespace bambu_cli
