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

// Open <archive_path> as a zip, copy all entries except <entry_name> into a
// new zip, atomically replace the original. Used by invariant-guard tests
// to produce "metadata points at file that no longer exists" scenarios.
// Test-fails (REQUIRE) on any miniz error.
void mutate_archive_remove_entry(const std::string& archive_path,
                                 const std::string& entry_name);

// Open <archive_path> as a zip, append a new entry <entry_name> with bytes
// <content>, atomically replace the original. Used to produce "saved
// archive has an aux file not present in temp dir" scenarios.
// Test-fails (REQUIRE) on any miniz error.
void mutate_archive_add_extra(const std::string& archive_path,
                              const std::string& entry_name,
                              const std::string& content);

} // namespace bambu_cli_unit
