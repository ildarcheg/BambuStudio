#pragma once

#include <stdexcept>

namespace bambu_cli {

// Typed exception hierarchy thrown by project_ops on error. Each class
// corresponds to one ExitCode (see exit_codes.hpp). The Phase A.4
// run_mutation envelope maps thrown instances to (ExitCode, error_code
// string, error_message) for emit_error.
//
// Sibling pattern: matches OrcaSlicer's typed-exception hierarchy
// (src/cli/project_ops.hpp + src/cli/invariants.hpp).
//
// For exit codes not represented here, project_ops throws stdlib
// exceptions with the following Bambu-specific conventions:
//   std::invalid_argument  -> exit 1 (usage_error)        -- bad argument value
//   std::runtime_error     -> exit 3 (parse_failure)      -- STL parse, etc.
//   std::out_of_range      -> exit 6 (unknown_reference)  -- missing ref
//
// (Note: Bambu's mapping for std::invalid_argument is usage_error, not
//  Orca's default of duplicate_name. Per-callback maps can override in
//  Phase A.4.)

// Source file (e.g., --stl path) not found on disk. -> exit 2.
class FileNotFoundError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Unknown config key, rejected value, or attempt to set a system-managed
// key directly. -> exit 4.
class BadConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Duplicate identifier (e.g., plate name collision on `plate add` or
// `plate rename --to`). -> exit 5.
class DuplicateNameError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Saved-archive invariant guard fault (relationships / thumbnails /
// vector round-trip). Not yet thrown from project_ops; reserved for the
// invariant_guard integration. -> exit 8.
class InvariantViolation : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Object placement off the printable bed AABB with an explicit
// --translate / --rotate / --scale. -> exit 9.
class PlacementFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace bambu_cli
