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

#include "../exception_dispatch.hpp"
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
        auto d = bambu_cli::exception_dispatch::dispatch(e, overrides);
        emit_error(mode, d.code, d.message);
        std::exit(d.exit_code);
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
