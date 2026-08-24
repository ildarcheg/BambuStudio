#include "exception_dispatch.hpp"

#include "exceptions.hpp"
#include "exit_codes.hpp"

#include <stdexcept>
#include <typeinfo>

namespace bambu_cli::exception_dispatch {

Dispatched dispatch(const std::exception& e,
                    const MutationExceptionMap& overrides)
{
    // 0. Schema errors short-circuit BEFORE the override lookup so they
    //    never get caught by per-op invalid_argument -> exit 7 remappings.
    if (dynamic_cast<const ManifestFieldError*>(&e))
        return {to_int(ExitCode::usage_error), "usage_error", e.what()};

    // 1. Per-call-site override map (exact dynamic-type match on typeid(e)).
    auto it = overrides.find(std::type_index(typeid(e)));
    if (it != overrides.end())
        return {it->second.first, it->second.second, e.what()};

    // 2. Built-in typed-exception table via dynamic_cast. Order matters:
    //    typed subclasses of std::runtime_error must precede the catch-all.
    if (dynamic_cast<const PlacementFailure*>(&e))      return {to_int(ExitCode::placement_failure),   "placement_failure",   e.what()};
    if (dynamic_cast<const BadConfigError*>(&e))        return {to_int(ExitCode::bad_config),          "bad_config",          e.what()};
    if (dynamic_cast<const DuplicateNameError*>(&e))    return {to_int(ExitCode::duplicate_name),      "duplicate_name",      e.what()};
    if (dynamic_cast<const FileNotFoundError*>(&e))     return {to_int(ExitCode::file_not_found),      "file_not_found",      e.what()};
    if (dynamic_cast<const InvariantViolation*>(&e))    return {to_int(ExitCode::invariant_violation), "invariant_violation", e.what()};
    if (dynamic_cast<const InvalidStateError*>(&e))     return {to_int(ExitCode::invalid_state),       "invalid_state",       e.what()};

    // 2b. Base-class-aware override fallback: an override keyed on a std
    //     base type must also catch *subclasses* (e.g. Slic3r::RuntimeError
    //     vs std::runtime_error), which the exact-typeid lookup at step 1
    //     misses — without outranking the built-in typed table above.
    //     Probed most-derived-first so an invalid_argument override wins
    //     over a logic_error one for an invalid_argument subclass.
    if (!overrides.empty()) {
        auto lookup = [&](const std::type_info& ti) -> const std::pair<int, std::string>* {
            auto bit = overrides.find(std::type_index(ti));
            return bit != overrides.end() ? &bit->second : nullptr;
        };
        const std::pair<int, std::string>* m = nullptr;
        if (!m && dynamic_cast<const std::invalid_argument*>(&e)) m = lookup(typeid(std::invalid_argument));
        if (!m && dynamic_cast<const std::out_of_range*>(&e))     m = lookup(typeid(std::out_of_range));
        if (!m && dynamic_cast<const std::logic_error*>(&e))      m = lookup(typeid(std::logic_error));
        if (!m && dynamic_cast<const std::runtime_error*>(&e))    m = lookup(typeid(std::runtime_error));
        if (m) return {m->first, m->second, e.what()};
    }

    // std::invalid_argument and std::out_of_range derive from std::logic_error,
    // NOT std::runtime_error, so they don't collide with the catch-all below.
    if (dynamic_cast<const std::invalid_argument*>(&e)) return {to_int(ExitCode::usage_error),         "usage_error",         e.what()};
    if (dynamic_cast<const std::out_of_range*>(&e))     return {to_int(ExitCode::unknown_reference),   "unknown_reference",   e.what()};

    // Catch-all (std::runtime_error and any other std::exception subclass).
    return {to_int(ExitCode::parse_failure), "parse_failure", e.what()};
}

} // namespace bambu_cli::exception_dispatch
