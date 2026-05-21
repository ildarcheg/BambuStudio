#pragma once

namespace bambu_cli {

// Documented exit codes (spec §3). Stored as int in IoResult/OpResult and
// passed to std::exit via static_cast<int>(ExitCode::name).
enum class ExitCode : int {
    ok                  = 0,
    usage_error         = 1,
    file_not_found      = 2,
    parse_failure       = 3,
    bad_config          = 4,
    duplicate_name      = 5,
    unknown_reference   = 6,
    invalid_state       = 7,
    invariant_violation = 8,
    placement_failure   = 9,
};

inline int to_int(ExitCode c) { return static_cast<int>(c); }

} // namespace bambu_cli
