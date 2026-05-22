#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_ops.hpp"
#include "exceptions.hpp"
#include <libslic3r/Model.hpp>
#include <stdexcept>
#include <vector>
#include <string>

using bambu_cli::ProjectState;
using bambu_cli::MergePartsParams;

// Helper: add two_cubes.stl, split it -> 2 volumes named "twin_1" and "twin_2"
static void add_and_split(ProjectState& s, const std::string& obj_name = "twin") {
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/two_cubes.stl";
    REQUIRE(bambu_cli::add_object_to_plate(
        s, s.plate_data.front()->plate_name, stl, obj_name, -1, nullptr, 1, nullptr).ok);
    REQUIRE(bambu_cli::split_object_to_parts(s, obj_name) == 2);
    // After split: object has volumes named "twin_1" and "twin_2" (split() appends _N)
}

TEST_CASE("merge_object_parts: happy path -- 2 volumes merge to 1", "[unit][merge]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);

    MergePartsParams p;
    p.parts   = {"twin_1", "twin_2"};
    p.into    = "merged";
    p.filament = 1;

    std::string msg = bambu_cli::merge_object_parts(s, "twin", p);
    REQUIRE(s.model.objects[0]->volumes.size() == 1);
    REQUIRE(s.model.objects[0]->volumes[0]->name == "merged");
    REQUIRE(msg.find("merged") != std::string::npos);
}

TEST_CASE("merge_object_parts: step b -- unknown object -> out_of_range", "[unit][merge]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    MergePartsParams p; p.parts = {"v1"}; p.into = "m";
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "ghost", p), std::out_of_range);
}

TEST_CASE("merge_object_parts: step c -- unknown part -> out_of_range", "[unit][merge]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    MergePartsParams p; p.parts = {"no_such_part"}; p.into = "m";
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), std::out_of_range);
}

TEST_CASE("merge_object_parts: step d -- --into already exists -> DuplicateNameError", "[unit][merge]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    // "twin_1" already exists -- use it as the --into name
    MergePartsParams p; p.parts = {"twin_2"}; p.into = "twin_1";
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), bambu_cli::DuplicateNameError);
}

TEST_CASE("merge_object_parts: step d before step e (fail-fast)", "[unit][merge]") {
    // --into collision (step d) + --filament out of range (step e):
    // step d must throw first
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    MergePartsParams p; p.parts = {"twin_2"}; p.into = "twin_1"; p.filament = 99;
    // DuplicateNameError (step d) must be thrown, NOT out_of_range (step e)
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), bambu_cli::DuplicateNameError);
}

TEST_CASE("merge_object_parts: step e -- filament out of range -> out_of_range", "[unit][merge]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    MergePartsParams p; p.parts = {"twin_1", "twin_2"}; p.into = "m"; p.filament = 99;
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), std::out_of_range);
}

TEST_CASE("merge_object_parts: step h -- filament disagreement -> invalid_argument", "[unit][merge]") {
    // Set different extruders on the two volumes, then merge without --filament
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    auto* obj = s.model.objects[0];
    REQUIRE(obj->volumes.size() == 2);
    obj->volumes[0]->config.set("extruder", 1);
    obj->volumes[1]->config.set("extruder", 2);

    MergePartsParams p; p.parts = {"twin_1", "twin_2"}; p.into = "m"; p.filament = -1;
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), std::invalid_argument);
}

TEST_CASE("merge_object_parts: step i -- non-extruder config key -> invalid_argument", "[unit][merge]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    auto* obj = s.model.objects[0];
    REQUIRE(obj->volumes.size() == 2);
    // Set a non-extruder key on one volume's config
    Slic3r::ConfigSubstitutionContext ctx{Slic3r::ForwardCompatibilitySubstitutionRule::Disable};
    obj->volumes[0]->config.set_deserialize("seam_position", "nearest", ctx);

    MergePartsParams p; p.parts = {"twin_1", "twin_2"}; p.into = "m"; p.filament = 1;
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), std::invalid_argument);
}

TEST_CASE("merge_object_parts: deterministic placement -- same result regardless of --parts order",
          "[unit][merge]") {
    // Two states with identical setup; merge with reversed --parts order.
    // The merged volume must end up at the same index (lowest source index = 0).
    ProjectState s1, s2;
    bambu_cli_unit::load_reference_into(s1);
    bambu_cli_unit::load_reference_into(s2);
    add_and_split(s1);
    add_and_split(s2);

    MergePartsParams p1; p1.parts = {"twin_1", "twin_2"}; p1.into = "m"; p1.filament = 1;
    MergePartsParams p2; p2.parts = {"twin_2", "twin_1"}; p2.into = "m"; p2.filament = 1;

    bambu_cli::merge_object_parts(s1, "twin", p1);
    bambu_cli::merge_object_parts(s2, "twin", p2);

    // Both should result in 1 volume named "m" at index 0
    REQUIRE(s1.model.objects[0]->volumes.size() == 1);
    REQUIRE(s2.model.objects[0]->volumes.size() == 1);
    REQUIRE(s1.model.objects[0]->volumes[0]->name == "m");
    REQUIRE(s2.model.objects[0]->volumes[0]->name == "m");
}

TEST_CASE("merge_object_parts: single-volume shim sets extruder on obj.config", "[unit][merge]") {
    // After merge, if only 1 volume remains, extruder must be written to obj.config too
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);

    MergePartsParams p; p.parts = {"twin_1", "twin_2"}; p.into = "m"; p.filament = 2;
    bambu_cli::merge_object_parts(s, "twin", p);

    // obj.config must have extruder=2
    auto* obj = s.model.objects[0];
    REQUIRE(obj->volumes.size() == 1);
    const auto* eopt = obj->config.option("extruder");
    REQUIRE(eopt != nullptr);
    REQUIRE(static_cast<const Slic3r::ConfigOptionInt*>(eopt)->value == 2);
}

// ============================================================
// Phase F.3: fail-fast pairing tests (7 new pairings + d→e already exists)
// Each test sets up a state that fails at step N while step N+1 would NOT
// fail given the same inputs, proving steps execute in declared order.
// ============================================================

TEST_CASE("merge_object_parts: step a before step b (fail-fast)", "[unit][merge]") {
    // empty --parts (step a: invalid_argument); object "twin" exists so step b
    // would succeed — step a must throw first.
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    MergePartsParams p; p.parts = {}; p.into = "merged"; p.filament = 1;
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), std::invalid_argument);
}

TEST_CASE("merge_object_parts: step b before step c (fail-fast)", "[unit][merge]") {
    // unknown object "ghost" (step b: out_of_range); parts {"twin_1"} DO exist
    // in "twin" so if step b were skipped and obj pointed there, step c would
    // succeed — only step b fails.
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    MergePartsParams p; p.parts = {"twin_1"}; p.into = "merged"; p.filament = 1;
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "ghost", p), std::out_of_range);
}

TEST_CASE("merge_object_parts: step c before step d (fail-fast)", "[unit][merge]") {
    // unknown part "no_such" (step c: out_of_range); p.into="twin_1" already exists
    // so step d would throw DuplicateNameError if reached — step c throws first.
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    MergePartsParams p; p.parts = {"no_such"}; p.into = "twin_1"; p.filament = 1;
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), std::out_of_range);
}

TEST_CASE("merge_object_parts: step e before step f (fail-fast)", "[unit][merge]") {
    // filament=99 out of range (step e: out_of_range); twin_1 is NEGATIVE_VOLUME
    // so step f would throw invalid_argument if reached — step e throws first.
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    s.model.objects[0]->volumes[0]->set_type(Slic3r::ModelVolumeType::NEGATIVE_VOLUME);
    MergePartsParams p; p.parts = {"twin_1", "twin_2"}; p.into = "merged"; p.filament = 99;
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), std::out_of_range);
}

TEST_CASE("merge_object_parts: step f before step g (fail-fast)", "[unit][merge]") {
    // twin_1 is NEGATIVE_VOLUME with non-empty mesh (step f: invalid_argument);
    // mesh is non-empty so step g would NOT throw — step f throws first.
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    s.model.objects[0]->volumes[0]->set_type(Slic3r::ModelVolumeType::NEGATIVE_VOLUME);
    MergePartsParams p; p.parts = {"twin_1", "twin_2"}; p.into = "merged"; p.filament = 1;
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), std::invalid_argument);
}

TEST_CASE("merge_object_parts: step g before step h (fail-fast)", "[unit][merge]") {
    // twin_1 has empty mesh (step g: invalid_argument); twin_1 and twin_2 have
    // the same default extruder so step h (filament agreement) would NOT throw —
    // step g throws first.
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    s.model.objects[0]->volumes[0]->reset_mesh();
    MergePartsParams p; p.parts = {"twin_1", "twin_2"}; p.into = "merged"; p.filament = -1;
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), std::invalid_argument);
}

TEST_CASE("merge_object_parts: step h before step i (fail-fast)", "[unit][merge]") {
    // different extruders on twin_1/twin_2 with filament=-1 (step h: invalid_argument);
    // no non-extruder config keys so step i would NOT throw — step h throws first.
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    auto* obj = s.model.objects[0];
    REQUIRE(obj->volumes.size() == 2);
    obj->volumes[0]->config.set("extruder", 1);
    obj->volumes[1]->config.set("extruder", 2);
    MergePartsParams p; p.parts = {"twin_1", "twin_2"}; p.into = "merged"; p.filament = -1;
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), std::invalid_argument);
}

// ============================================================
// Phase F.3: dedicated tests for step f (MODEL_PART) and step g (empty mesh)
// ============================================================

TEST_CASE("merge_object_parts: non-MODEL_PART source throws", "[unit][merge]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    // Change twin_1 to NEGATIVE_VOLUME — not a MODEL_PART
    s.model.objects[0]->volumes[0]->set_type(Slic3r::ModelVolumeType::NEGATIVE_VOLUME);
    MergePartsParams p; p.parts = {"twin_1"}; p.into = "merged"; p.filament = 1;
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), std::invalid_argument);
}

TEST_CASE("merge_object_parts: empty-mesh source throws", "[unit][merge]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_and_split(s);
    // Reset twin_1's mesh to empty (zero facets)
    s.model.objects[0]->volumes[0]->reset_mesh();
    MergePartsParams p; p.parts = {"twin_1"}; p.into = "merged"; p.filament = 1;
    REQUIRE_THROWS_AS(bambu_cli::merge_object_parts(s, "twin", p), std::invalid_argument);
}
