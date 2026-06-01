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

} // namespace bambu_cli
