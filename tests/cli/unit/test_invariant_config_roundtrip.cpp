// Guard check (c) — config roundtrip. The original check compared values
// only when a key existed on BOTH sides, so a vector key the writer
// silently DROPPED sailed through: detecting dropped keys is half of what
// a roundtrip check exists for (audit LOW, 2026-07-15).
#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "../test_helpers.hpp"
#include "invariant_guard.hpp"
#include "io.hpp"

#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace fs = boost::filesystem;
using bambu_cli::ProjectState;

TEST_CASE("config roundtrip guard: a vector key dropped from the saved "
          "archive fails the guard", "[unit][invariant_config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(s.project_config.has("filament_settings_id"));   // vector (coStrings)

    const std::string out = bambu_cli_test::fresh_temp_path("_cfgdrop.3mf");
    REQUIRE(bambu_cli::save_project(s, out).ok);
    // Sanity: an untampered save passes the guard.
    REQUIRE(bambu_cli::run_guard(out, s).ok);

    // Simulate the writer dropping a vector key: strip it from the saved
    // project settings JSON and swap the archive entry.
    auto bytes = bambu_cli_test::read_zip_entry(out, "Metadata/project_settings.config");
    REQUIRE_FALSE(bytes.empty());
    auto j = nlohmann::json::parse(bytes.begin(), bytes.end());
    REQUIRE(j.contains("filament_settings_id"));
    j.erase("filament_settings_id");
    bambu_cli_unit::mutate_archive_remove_entry(out, "Metadata/project_settings.config");
    bambu_cli_unit::mutate_archive_add_extra(out, "Metadata/project_settings.config",
                                             j.dump(2));

    auto gr = bambu_cli::run_guard(out, s);
    REQUIRE_FALSE(gr.ok);
    REQUIRE(gr.failed_check == "config_roundtrip");
    REQUIRE(gr.failure_detail.find("filament_settings_id") != std::string::npos);

    fs::remove(out);
}
