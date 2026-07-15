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

// The referenced entity exists but is in a state that forbids the
// operation (e.g. removing a non-empty plate). Distinguishes "found but
// not allowed" from "not found" (std::out_of_range -> exit 6). -> exit 7.
class InvalidStateError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ============================================================
// Derived exceptions (Phase C — project-tab operations)
// ============================================================

// Cover image is not a PNG (signature mismatch) or path is unreadable. -> exit 4.
class BadCoverImage : public BadConfigError {
public:
    using BadConfigError::BadConfigError;
};

// Field name not in the allowed whitelist for info clear or profile clear. -> exit 4.
class InvalidField : public BadConfigError {
public:
    using BadConfigError::BadConfigError;
};

// Aux source file does not exist / unreadable. -> exit 2.
class BadAuxFile : public FileNotFoundError {
public:
    using FileNotFoundError::FileNotFoundError;
};

// Sanitized name is invalid (path separators, dot-only, whitespace, reserved). -> exit 4.
class AuxNameError : public BadConfigError {
public:
    using BadConfigError::BadConfigError;
};

// Target name already exists in the folder and --force was not given. -> exit 5.
class AuxCollisionError : public DuplicateNameError {
public:
    using DuplicateNameError::DuplicateNameError;
};


// Manifest schema-shape error: unknown field, missing required field,
// type mismatch, unknown op. Thrown by `require_only`, by every handler's
// field-validation code, and by `HandlerRegistry::lookup` on unknown op.
// -> exit 1 (usage_error), short-circuited in exception_dispatch::dispatch
//    BEFORE the per-op MutationExceptionMap lookup so verbs that remap
//    std::invalid_argument -> exit 7 (split-to-parts, merge-parts) don't
//    swallow schema typos as invalid_state.
class ManifestFieldError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

} // namespace bambu_cli
