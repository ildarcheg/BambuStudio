// Direct unit tests for exception_dispatch (audit gap: the dispatch table
// was previously covered only behaviorally through e2e exit codes).
//
// Also pins the base-class-aware override fallback: an override keyed on a
// std base type (the only remaining user is auto-orient's
// {std::runtime_error -> exit 7}) must catch *subclasses* too — libslic3r
// throws Slic3r::RuntimeError, a std::runtime_error subclass, which the
// exact-typeid lookup misses and the catch-all then misreports as exit 3
// parse_failure.
#include <catch2/catch.hpp>

#include "exception_dispatch.hpp"
#include "exceptions.hpp"

#include <stdexcept>
#include <typeindex>

using bambu_cli::MutationExceptionMap;
using bambu_cli::exception_dispatch::dispatch;

namespace {

// Stand-in for Slic3r::RuntimeError (also a plain std::runtime_error
// subclass) — avoids dragging libslic3r headers into this test.
struct EngineError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

const MutationExceptionMap kOrientOverrides = {
    {std::type_index(typeid(std::runtime_error)), {7, "invalid_state"}},
};

} // namespace

TEST_CASE("dispatch: built-in ladder basics", "[unit][exception_dispatch]") {
    CHECK(dispatch(std::invalid_argument("x")).exit_code == 1);
    CHECK(dispatch(std::out_of_range("x")).exit_code == 6);
    CHECK(dispatch(std::runtime_error("x")).exit_code == 3);
    CHECK(dispatch(bambu_cli::InvalidStateError("x")).exit_code == 7);
    CHECK(dispatch(bambu_cli::PlacementFailure("x")).exit_code == 9);
    CHECK(dispatch(bambu_cli::DuplicateNameError("x")).exit_code == 5);
}

TEST_CASE("dispatch: ManifestFieldError short-circuits before overrides",
          "[unit][exception_dispatch]") {
    const MutationExceptionMap remap = {
        {std::type_index(typeid(std::invalid_argument)), {7, "invalid_state"}},
    };
    auto d = dispatch(bambu_cli::ManifestFieldError("typo"), remap);
    REQUIRE(d.exit_code == 1);
    REQUIRE(d.code == "usage_error");
}

TEST_CASE("dispatch: exact-type override match", "[unit][exception_dispatch]") {
    auto d = dispatch(std::runtime_error("engine failed"), kOrientOverrides);
    REQUIRE(d.exit_code == 7);
    REQUIRE(d.code == "invalid_state");
}

TEST_CASE("dispatch: override keyed on a std base type catches subclasses",
          "[unit][exception_dispatch]") {
    // Slic3r::RuntimeError-style subclass: the exact-typeid lookup misses
    // it; without the base-aware fallback it lands in the catch-all as
    // exit 3 parse_failure, breaking auto-orient's documented exit 7.
    auto d = dispatch(EngineError("orient engine failed"), kOrientOverrides);
    REQUIRE(d.exit_code == 7);
    REQUIRE(d.code == "invalid_state");
}

TEST_CASE("dispatch: typed table outranks the base-aware override fallback",
          "[unit][exception_dispatch]") {
    // PlacementFailure IS a runtime_error subclass, but its own typed
    // classification (exit 9) must win over a base-keyed override.
    auto d = dispatch(bambu_cli::PlacementFailure("off bed"), kOrientOverrides);
    REQUIRE(d.exit_code == 9);
    REQUIRE(d.code == "placement_failure");
}
