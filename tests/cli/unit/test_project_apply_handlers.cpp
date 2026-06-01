#include "commands/project_apply_internal.hpp"
#include "io.hpp"
#include "unit_helpers.hpp"

#include "exceptions.hpp"
#include "project_ops.hpp"
#include "project_state.hpp"

#include <catch2/catch.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>

using nlohmann::json;
using bambu_cli::HandlerRegistry;
using bambu_cli::ProjectState;
using bambu_cli::ManifestFieldError;
using bambu_cli::DuplicateNameError;

TEST_CASE("plate.add: happy path appends a new plate", "[project_apply][plate.add]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);
    const auto initial = bambu_cli::list_plate_names(s).size();

    HandlerRegistry reg;
    json step = {{"op", "plate.add"}, {"name", "P2"}};
    reg.lookup("plate.add").fn(s, step);

    auto names = bambu_cli::list_plate_names(s);
    REQUIRE(names.size() == initial + 1);
    REQUIRE(names.back() == "P2");
}

TEST_CASE("plate.add: missing name throws ManifestFieldError",
          "[project_apply][plate.add]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);

    HandlerRegistry reg;
    json step = {{"op", "plate.add"}};
    REQUIRE_THROWS_AS(reg.lookup("plate.add").fn(s, step), ManifestFieldError);
}

TEST_CASE("plate.add: unknown field throws ManifestFieldError",
          "[project_apply][plate.add]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);

    HandlerRegistry reg;
    json step = {{"op", "plate.add"}, {"name", "P2"}, {"filement", 2}};
    REQUIRE_THROWS_AS(reg.lookup("plate.add").fn(s, step), ManifestFieldError);
}

TEST_CASE("plate.add: non-string name throws ManifestFieldError",
          "[project_apply][plate.add]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);

    HandlerRegistry reg;
    json step = {{"op", "plate.add"}, {"name", 42}};
    REQUIRE_THROWS_AS(reg.lookup("plate.add").fn(s, step), ManifestFieldError);
}

TEST_CASE("plate.add: duplicate name throws DuplicateNameError",
          "[project_apply][plate.add]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);
    bambu_cli::add_plate(s, "P2");

    HandlerRegistry reg;
    json step = {{"op", "plate.add"}, {"name", "P2"}};
    REQUIRE_THROWS_AS(reg.lookup("plate.add").fn(s, step), DuplicateNameError);
}

TEST_CASE("HandlerRegistry::lookup: unknown op throws ManifestFieldError",
          "[project_apply][registry]") {
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.bogus"), ManifestFieldError);
}

TEST_CASE("HandlerRegistry::lookup: plate.add overrides empty",
          "[project_apply][registry]") {
    HandlerRegistry reg;
    REQUIRE(reg.lookup("plate.add").overrides.empty());
}

// ---------- plate.remove tests ----------

TEST_CASE("plate.remove: happy path drops an empty plate", "[project_apply][plate.remove]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);
    bambu_cli::add_plate(s, "P2");
    HandlerRegistry reg;
    json step = {{"op", "plate.remove"}, {"name", "P2"}};
    reg.lookup("plate.remove").fn(s, step);
    auto names = bambu_cli::list_plate_names(s);
    REQUIRE(std::find(names.begin(), names.end(), "P2") == names.end());
}

TEST_CASE("plate.remove: missing name throws", "[project_apply][plate.remove]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.remove").fn(s, json{{"op","plate.remove"}}), ManifestFieldError);
}

TEST_CASE("plate.remove: unknown field throws", "[project_apply][plate.remove]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    json step = {{"op", "plate.remove"}, {"name", "P2"}, {"x", 1}};
    REQUIRE_THROWS_AS(reg.lookup("plate.remove").fn(s, step), ManifestFieldError);
}

TEST_CASE("plate.remove: non-string name throws", "[project_apply][plate.remove]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    json step = {{"op", "plate.remove"}, {"name", 42}};
    REQUIRE_THROWS_AS(reg.lookup("plate.remove").fn(s, step), ManifestFieldError);
}

// ---------- plate.rename tests ----------

TEST_CASE("plate.rename: happy path renames", "[project_apply][plate.rename]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    bambu_cli::add_plate(s, "P2");
    HandlerRegistry reg;
    reg.lookup("plate.rename").fn(s, json{{"op","plate.rename"}, {"from","P2"}, {"to","P-NEW"}});
    auto names = bambu_cli::list_plate_names(s);
    REQUIRE(std::find(names.begin(), names.end(), "P-NEW") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "P2")    == names.end());
}

TEST_CASE("plate.rename: missing from throws", "[project_apply][plate.rename]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.rename").fn(s, json{{"op","plate.rename"},{"to","X"}}), ManifestFieldError);
}

TEST_CASE("plate.rename: missing to throws", "[project_apply][plate.rename]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.rename").fn(s, json{{"op","plate.rename"},{"from","X"}}), ManifestFieldError);
}

TEST_CASE("plate.rename: unknown field throws", "[project_apply][plate.rename]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    json step = {{"op","plate.rename"},{"from","X"},{"to","Y"},{"junk",1}};
    REQUIRE_THROWS_AS(reg.lookup("plate.rename").fn(s, step), ManifestFieldError);
}
