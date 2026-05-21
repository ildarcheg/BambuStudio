#pragma once

#include "project_state.hpp"

#include <string>

namespace bambu_cli_unit {

// Load the canonical committed reference 3mf into <state>. Fails the test
// (REQUIRE) if the load doesn't return ok. Useful for "given a real-world
// project, mutate, assert" unit tests that need realistic config/plates.
void load_reference_into(bambu_cli::ProjectState& state);

// Build a minimal in-memory ProjectState with N plates (default 1) and an
// empty Model + empty DynamicPrintConfig. No on-disk artifacts. For pure
// state-shape tests (plate naming, dedupe, etc.) that don't need a real
// config.
void make_minimal_state(bambu_cli::ProjectState& state, int n_plates = 1);

// Path to an in-tree STL fixture (cube/cylinder/cone). Resolved against
// BAMBU_CLI_FIXTURE_STL_DIR.
std::string fixture_stl(const std::string& name);

} // namespace bambu_cli_unit
