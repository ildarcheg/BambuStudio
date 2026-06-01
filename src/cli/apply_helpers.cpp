#include "apply_helpers.hpp"

#include "exceptions.hpp"

#include <algorithm>
#include <string>

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

} // namespace bambu_cli
