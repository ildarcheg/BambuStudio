#pragma once

// run_mutation: the load-mutate-save envelope used by every mutating CLI
// subcommand. Sibling pattern: matches OrcaSlicer's
// `orca_cli::commands::run_mutation` template
// (src/cli/commands/mutation_runner.hpp).
//
// The envelope owns the boilerplate that was previously inlined into each
// callback (and partially folded into the transitional run_op_or_exit
// from Phase A.3):
//
//   1. load_project(in_path) into a fresh ProjectState. On failure, emit
//      Shape A error and std::exit with the IoResult's exit_code.
//   2. Invoke the mutator. The mutator returns the success message string
//      that emit_ok will print. The mutator may throw a typed/stdlib
//      exception on error.
//   3. save_project(state, out_path). On failure, emit + exit.
//   4. emit_ok(mode, "ok", success_msg).
//
// On any thrown exception from the mutator:
//   - first consults the MutationExceptionMap override (keyed on dynamic
//     std::type_index) and, if found, emits with the overridden
//     (exit_code, error_code) and std::exit-s;
//   - otherwise applies the built-in typed-exception defaults documented
//     against each catch clause below.
//
// No callback that uses run_mutation needs to call std::exit directly.

#include "../exceptions.hpp"
#include "../exit_codes.hpp"
#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_state.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace bambu_cli {

// Override map for run_mutation's exception->ExitCode dispatch. Keyed on
// the dynamic std::type_index of the thrown exception. Value is
// (exit_code, error_code_string) passed to std::exit / emit_error.
//
// When empty (the default), run_mutation uses Bambu's built-in mapping:
//   FileNotFoundError     -> exit 2 / "file_not_found"
//   BadConfigError        -> exit 4 / "bad_config"
//   DuplicateNameError    -> exit 5 / "duplicate_name"
//   InvariantViolation    -> exit 8 / "invariant_violation"
//   PlacementFailure      -> exit 9 / "placement_failure"
//   std::invalid_argument -> exit 1 / "usage_error"
//   std::out_of_range     -> exit 6 / "unknown_reference"
//   (catch-all base)      -> exit 3 / "parse_failure"
using MutationExceptionMap =
    std::unordered_map<std::type_index, std::pair<int, std::string>>;

template <typename Mutator>
void run_mutation(OutputMode mode,
                  const std::string& in_path,
                  const std::string& out_path,
                  Mutator&& mut,
                  const MutationExceptionMap& overrides = {})
{
    ProjectState state;
    IoResult lr = load_project(in_path, state);
    if (!lr.ok) {
        emit_error(mode, lr.error_code, lr.error_message);
        std::exit(lr.exit_code);
    }

    std::string success_msg;
    try {
        success_msg = mut(state);
    } catch (const std::exception& e) {
        // 1. Per-call-site override map (exact dynamic type match).
        auto it = overrides.find(std::type_index(typeid(e)));
        if (it != overrides.end()) {
            emit_error(mode, it->second.second, e.what());
            std::exit(it->second.first);
        }
        // 2. Built-in typed-exception defaults via dynamic_cast (handles
        //    derived classes). Order matters: typed subclasses of
        //    std::runtime_error must precede the runtime_error catch-all
        //    at the bottom.
        if (dynamic_cast<const PlacementFailure*>(&e)) {
            emit_error(mode, "placement_failure", e.what());
            std::exit(to_int(ExitCode::placement_failure));
        }
        if (dynamic_cast<const BadConfigError*>(&e)) {
            emit_error(mode, "bad_config", e.what());
            std::exit(to_int(ExitCode::bad_config));
        }
        if (dynamic_cast<const DuplicateNameError*>(&e)) {
            emit_error(mode, "duplicate_name", e.what());
            std::exit(to_int(ExitCode::duplicate_name));
        }
        if (dynamic_cast<const FileNotFoundError*>(&e)) {
            emit_error(mode, "file_not_found", e.what());
            std::exit(to_int(ExitCode::file_not_found));
        }
        if (dynamic_cast<const InvariantViolation*>(&e)) {
            emit_error(mode, "invariant_violation", e.what());
            std::exit(to_int(ExitCode::invariant_violation));
        }
        // std::invalid_argument and std::out_of_range derive from
        // std::logic_error, NOT std::runtime_error, so they don't
        // collide with the catch-all below.
        if (dynamic_cast<const std::invalid_argument*>(&e)) {
            emit_error(mode, "usage_error", e.what());
            std::exit(to_int(ExitCode::usage_error));
        }
        if (dynamic_cast<const std::out_of_range*>(&e)) {
            emit_error(mode, "unknown_reference", e.what());
            std::exit(to_int(ExitCode::unknown_reference));
        }
        // Catch-all (std::runtime_error and any other std::exception
        // subclass): treated as parse_failure, matching the historical
        // OpResult exit code for STL parse failures in add_object_to_plate.
        emit_error(mode, "parse_failure", e.what());
        std::exit(to_int(ExitCode::parse_failure));
    }

    const std::string& out = out_path.empty() ? in_path : out_path;
    IoResult sr = save_project(state, out);
    if (!sr.ok) {
        emit_error(mode, sr.error_code, sr.error_message);
        std::exit(sr.exit_code);
    }

    emit_ok(mode, "ok", success_msg);
}

} // namespace bambu_cli
