#pragma once

// exception_dispatch — shared exception → (exit_code, error_code) table.
//
// Used by both the single-verb run_mutation envelope
// (commands/mutation_runner.hpp) and the batch `project apply` dispatcher
// (commands/project_apply.cpp). Lifting this out of run_mutation gives a
// single point of truth and lets the batch dispatcher wrap the produced
// message with step/op (and failing_key) context.
//
// Dispatch order (matters!):
//   0. ManifestFieldError              -> exit 1 / "usage_error"
//      (BEFORE overrides — protects schema typos from any per-verb
//      override remapping; historically split/merge remapped
//      std::invalid_argument to exit 7, since replaced by the typed
//      InvalidStateError. auto-orient still overrides runtime_error.)
//   1. per-call-site overrides         -> as configured
//      (exact std::type_index match on dynamic typeid(e))
//   2. built-in typed-exception table  -> see Dispatched return values
//   2b. base-class-aware override fallback: overrides keyed on a std base
//      type also catch subclasses (Slic3r::RuntimeError et al.) that the
//      exact match at step 1 misses; ranked below the typed table so e.g.
//      PlacementFailure keeps exit 9 under a runtime_error override
//
// The table at step 2 is byte-identical to mutation_runner.hpp's prior
// inline dynamic_cast ladder (lines 87-123). Behaviour-preserving for
// every existing single-verb call site.

#include <exception>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace bambu_cli {

// Override map. Re-exported here so callers don't need to drag in the
// commands/mutation_runner.hpp header just for the type.
using MutationExceptionMap =
    std::unordered_map<std::type_index, std::pair<int, std::string>>;

namespace exception_dispatch {

struct Dispatched {
    int         exit_code;
    std::string code;       // e.g. "duplicate_name", "placement_failure"
    std::string message;    // e.what(), unchanged
};

Dispatched dispatch(const std::exception& e,
                    const MutationExceptionMap& overrides = {});

} // namespace exception_dispatch
} // namespace bambu_cli
