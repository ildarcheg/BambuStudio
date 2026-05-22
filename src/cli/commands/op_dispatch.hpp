#pragma once

// Transitional bridge for Phase A.3:
// Wraps a project_ops mutation call in a try/catch, mapping each typed/stdlib
// exception to its ExitCode + error_code string, and emits the Shape A error
// envelope to stderr via emit_error before std::exit-ing.
//
// On success, returns the OpResult{ok=true} produced by the mutation.
//
// Replaced by mutation_runner::run_mutation in Phase A.4, which folds this
// catch-and-emit step together with the surrounding load_project /
// save_project / emit_ok scaffolding.

#include "../exceptions.hpp"
#include "../exit_codes.hpp"
#include "../json_output.hpp"
#include "../project_ops.hpp"

#include <cstdlib>
#include <stdexcept>

namespace bambu_cli {

template <typename F>
OpResult run_op_or_exit(OutputMode mode, F&& fn) {
    try {
        return fn();
    } catch (const PlacementFailure& e) {
        emit_error(mode, "placement_failure", e.what());
        std::exit(to_int(ExitCode::placement_failure));
    } catch (const BadConfigError& e) {
        emit_error(mode, "bad_config", e.what());
        std::exit(to_int(ExitCode::bad_config));
    } catch (const DuplicateNameError& e) {
        emit_error(mode, "duplicate_name", e.what());
        std::exit(to_int(ExitCode::duplicate_name));
    } catch (const FileNotFoundError& e) {
        emit_error(mode, "file_not_found", e.what());
        std::exit(to_int(ExitCode::file_not_found));
    } catch (const InvariantViolation& e) {
        emit_error(mode, "invariant_violation", e.what());
        std::exit(to_int(ExitCode::invariant_violation));
    } catch (const std::invalid_argument& e) {
        // Bambu convention: invalid_argument is usage_error (exit 1), not
        // duplicate_name (5). Per-callback overrides land in Phase A.4.
        emit_error(mode, "usage_error", e.what());
        std::exit(to_int(ExitCode::usage_error));
    } catch (const std::out_of_range& e) {
        emit_error(mode, "unknown_reference", e.what());
        std::exit(to_int(ExitCode::unknown_reference));
    } catch (const std::runtime_error& e) {
        // Catch-all base class (after the typed std::runtime_error subclasses
        // above). Used for parse failures thrown by add_object_to_plate's
        // load_stl path.
        emit_error(mode, "parse_failure", e.what());
        std::exit(to_int(ExitCode::parse_failure));
    }
}

} // namespace bambu_cli
