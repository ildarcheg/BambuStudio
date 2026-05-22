#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_ops.hpp"
#include "exceptions.hpp"

#include <libslic3r/PrintConfig.hpp>

#include <stdexcept>

using bambu_cli::ProjectState;

static std::string first_plate(const ProjectState& s) {
    REQUIRE_FALSE(s.plate_data.empty());
    return s.plate_data.front()->plate_name;
}

TEST_CASE("config_set project-level: stores value on project_config",
          "[unit][config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::config_set(s, "", "line_width", "0.5").ok);
    REQUIRE(s.project_config.has("line_width"));
    REQUIRE(s.project_config.opt_serialize("line_width") == "0.5");
}

TEST_CASE("config_set project-level: registers in different_settings_to_system",
          "[unit][config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::config_set(s, "", "line_width", "0.5").ok);
    const auto* opt = s.project_config.option("different_settings_to_system");
    REQUIRE(opt != nullptr);
    const auto* vs =
        dynamic_cast<const Slic3r::ConfigOptionStrings*>(opt);
    REQUIRE(vs != nullptr);
    REQUIRE_FALSE(vs->values.empty());
    REQUIRE(vs->values[0].find("line_width") != std::string::npos);
}

TEST_CASE("config_set: unknown key -> bad_config", "[unit][config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::config_set(s, "", "no_such_key_xyz", "1"),
                      bambu_cli::BadConfigError);
}

TEST_CASE("config_set: different_settings_to_system is rejected (system-managed)",
          "[unit][config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(
        bambu_cli::config_set(s, "", "different_settings_to_system", "x"),
        bambu_cli::BadConfigError);
}

TEST_CASE("config_set --object: stores on per-object config",
          "[unit][config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    REQUIRE(bambu_cli::add_object_to_plate(
        s, first_plate(s), stl, "", -1, nullptr, 1, nullptr).ok);

    REQUIRE(bambu_cli::config_set(s, "cube", "line_width", "0.4").ok);
    REQUIRE(s.model.objects[0]->config.has("line_width"));
    REQUIRE(s.model.objects[0]->config.opt_serialize("line_width") == "0.4");
}

TEST_CASE("config_set --object: object not found -> unknown_reference",
          "[unit][config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(
        bambu_cli::config_set(s, "missing", "line_width", "0.4"),
        std::out_of_range);
}

TEST_CASE("config_unset project-level: clears key and untracks it",
          "[unit][config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::config_set(s, "", "line_width", "0.5").ok);
    REQUIRE(bambu_cli::config_unset(s, "", "line_width").ok);
    REQUIRE_FALSE(s.project_config.has("line_width"));
    // Untrack: the key should NOT be in different_settings_to_system slot 0.
    const auto* opt = s.project_config.option("different_settings_to_system");
    if (opt) {
        const auto* vs =
            dynamic_cast<const Slic3r::ConfigOptionStrings*>(opt);
        if (vs && !vs->values.empty())
            REQUIRE(vs->values[0].find("line_width") == std::string::npos);
    }
}

TEST_CASE("config_unset: key not set -> unknown_reference", "[unit][config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    // Test the "key not currently set" path of config_unset. The reference
    // fixture pre-sets line_width="0.42", so the simpler "load + unset"
    // shape works in practice; the set+unset+unset shape below makes the
    // expectation independent of fixture state. Either approach exercises
    // the unknown_reference branch.
    REQUIRE(bambu_cli::config_set(s, "", "line_width", "0.5").ok);
    REQUIRE(bambu_cli::config_unset(s, "", "line_width").ok);
    REQUIRE_THROWS_AS(bambu_cli::config_unset(s, "", "line_width"),
                      std::out_of_range);
}

TEST_CASE("config_unset: unknown key -> bad_config", "[unit][config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::config_unset(s, "", "no_such_key_xyz"),
                      bambu_cli::BadConfigError);
}

TEST_CASE("config_list: returns all keys when not changed-only",
          "[unit][config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto entries = bambu_cli::config_list(s, "", /*changed_only=*/false);
    REQUIRE(entries.size() > 100);   // reference 3mf has many keys
}

TEST_CASE("config_list --changed-only: includes a freshly-set key",
          "[unit][config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::config_set(s, "", "line_width", "0.5").ok);
    auto entries = bambu_cli::config_list(s, "", /*changed_only=*/true);
    bool found = false;
    for (const auto& e : entries)
        if (e.key == "line_width") { found = true; break; }
    REQUIRE(found);
}

TEST_CASE("config_list --object: returns empty for unknown object",
          "[unit][config]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto entries = bambu_cli::config_list(s, "ghost", /*changed_only=*/false);
    REQUIRE(entries.empty());
}

TEST_CASE("config_set: filament-tab key lands in slot 1..fc (not slot 0)",
          "[unit][config][m1_routing]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    // filament_max_volumetric_speed is a filament-tab key in
    // Preset::filament_options() (verify via grep
    // src/libslic3r/Preset.cpp filament_options).
    REQUIRE(bambu_cli::config_set(
        s, "", "filament_max_volumetric_speed", "12").ok);

    const auto* opt = s.project_config.option("different_settings_to_system");
    REQUIRE(opt != nullptr);
    const auto* vs =
        dynamic_cast<const Slic3r::ConfigOptionStrings*>(opt);
    REQUIRE(vs != nullptr);

    // Determine filament_count from the loaded fixture.
    const auto* fs_opt = s.project_config.option("filament_settings_id");
    const auto* fs_vs =
        dynamic_cast<const Slic3r::ConfigOptionStrings*>(fs_opt);
    REQUIRE(fs_vs != nullptr);
    const size_t fc = fs_vs->values.size();
    REQUIRE(fc >= 1);

    // Slot layout: 0=process, 1=printer, 2..fc+1=per-filament.
    REQUIRE(vs->values.size() >= fc + 2);
    REQUIRE(vs->values[0].find("filament_max_volumetric_speed") == std::string::npos);
    REQUIRE(vs->values[1].find("filament_max_volumetric_speed") == std::string::npos);
    for (size_t i = 0; i < fc; ++i) {
        REQUIRE(vs->values[2 + i].find("filament_max_volumetric_speed") != std::string::npos);
    }
}

TEST_CASE("config_set: printer-tab key lands in slot 1",
          "[unit][config][m1_routing]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    // printable_height is in Preset::printer_options() at
    // src/libslic3r/Preset.cpp:1150. Use a numeric printer-tab key
    // rather than printer_settings_id (the latter is in skipped_in_dirty
    // at Preset.cpp:3108 and is intentionally not dirty-tracked).
    REQUIRE(bambu_cli::config_set(
        s, "", "printable_height", "300").ok);

    const auto* opt = s.project_config.option("different_settings_to_system");
    const auto* vs =
        dynamic_cast<const Slic3r::ConfigOptionStrings*>(opt);
    REQUIRE(vs != nullptr);

    const auto* fs_opt = s.project_config.option("filament_settings_id");
    const auto* fs_vs =
        dynamic_cast<const Slic3r::ConfigOptionStrings*>(fs_opt);
    const size_t fc = fs_vs->values.size();

    // Slot layout (matches OrcaSlicer/src/cli/project_ops.cpp:420-427 comment):
    //   slot 0       = process-tab dirty keys
    //   slot 1       = printer-tab dirty keys
    //   slots 2..fc+1 = per-filament dirty keys (fc total)
    REQUIRE(vs->values.size() >= fc + 2);
    REQUIRE(vs->values[0].find("printable_height") == std::string::npos);
    REQUIRE(vs->values[1].find("printable_height") != std::string::npos);
    for (size_t i = 0; i < fc; ++i) {
        REQUIRE(vs->values[2 + i].find("printable_height") == std::string::npos);
    }
}

TEST_CASE("config_unset: filament-tab key is removed from all filament slots",
          "[unit][config][m1_routing]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::config_set(
        s, "", "filament_max_volumetric_speed", "12").ok);
    REQUIRE(bambu_cli::config_unset(
        s, "", "filament_max_volumetric_speed").ok);

    const auto* opt = s.project_config.option("different_settings_to_system");
    if (!opt) return;   // option may have been erased entirely; that's fine
    const auto* vs =
        dynamic_cast<const Slic3r::ConfigOptionStrings*>(opt);
    if (!vs) return;
    for (const auto& slot : vs->values) {
        REQUIRE(slot.find("filament_max_volumetric_speed") == std::string::npos);
    }
}
