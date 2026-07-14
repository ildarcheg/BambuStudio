#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_ops.hpp"
#include <libslic3r/Model.hpp>
#include <stdexcept>

using bambu_cli::ProjectState;

TEST_CASE("split_object_to_parts: two-cube mesh splits into 2 volumes", "[unit][split]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    // Add two_cubes.stl (2 disconnected components) as an object
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/two_cubes.stl";
    // Need to add it to a plate first
    REQUIRE(bambu_cli::add_object_to_plate(
        s, s.plate_data.front()->plate_name, stl, "twin", -1, nullptr, 1, nullptr).ok);
    REQUIRE(s.model.objects.size() == 1);
    REQUIRE(s.model.objects[0]->volumes.size() == 1);

    size_t parts = bambu_cli::split_object_to_parts(s, "twin");
    REQUIRE(parts == 2);
    REQUIRE(s.model.objects[0]->volumes.size() == 2);
}

TEST_CASE("split_object_to_parts: unknown name -> out_of_range", "[unit][split]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::split_object_to_parts(s, "no_such"), std::out_of_range);
}

TEST_CASE("split_object_to_parts: multi-volume object -> invalid_argument", "[unit][split]") {
    // Setup: add two objects with same name so one ModelObject has... wait, that's
    // not how it works. Instead, let's set up an object that already has 2 volumes
    // by splitting first, then try to split again.
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/two_cubes.stl";
    REQUIRE(bambu_cli::add_object_to_plate(
        s, s.plate_data.front()->plate_name, stl, "twin", -1, nullptr, 1, nullptr).ok);
    // First split: succeeds, now 2 volumes
    REQUIRE(bambu_cli::split_object_to_parts(s, "twin") == 2);
    // Second split: 2 volumes -> must fail
    REQUIRE_THROWS_AS(bambu_cli::split_object_to_parts(s, "twin"), std::invalid_argument);
}

TEST_CASE("split_object_to_parts: single-component mesh -> invalid_argument", "[unit][split]") {
    // cube.stl is single-component — split should throw
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    REQUIRE(bambu_cli::add_object_to_plate(
        s, s.plate_data.front()->plate_name, stl, "box", -1, nullptr, 1, nullptr).ok);
    REQUIRE_THROWS_AS(bambu_cli::split_object_to_parts(s, "box"), std::invalid_argument);
}

TEST_CASE("split_object_to_parts: source.input_file re-stamped after split", "[unit][split]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/two_cubes.stl";
    REQUIRE(bambu_cli::add_object_to_plate(
        s, s.plate_data.front()->plate_name, stl, "twin", -1, nullptr, 1, nullptr).ok);

    size_t parts = bambu_cli::split_object_to_parts(s, "twin");
    REQUIRE(parts == 2);
    // All resulting volumes must have source.input_file set
    for (const auto* vol : s.model.objects[0]->volumes) {
        REQUIRE_FALSE(vol->source.input_file.empty());
        REQUIRE(vol->source.input_file == stl);
    }
}
