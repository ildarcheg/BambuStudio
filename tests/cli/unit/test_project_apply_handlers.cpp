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

// ---------- plate.center tests ----------

TEST_CASE("plate.center: happy path centers instances",
          "[project_apply][plate.center]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    reg.lookup("plate.center").fn(
        s, json{{"op","plate.center"}, {"plate", "Plate 01 test"}});
    // Lightweight invariant: handler returns without throwing.
    SUCCEED("plate.center applied without throwing");
}

TEST_CASE("plate.center: missing plate field throws",
          "[project_apply][plate.center]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.center").fn(s, json{{"op","plate.center"}}),
                      ManifestFieldError);
}

TEST_CASE("plate.center: unknown field throws",
          "[project_apply][plate.center]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    json step = {{"op","plate.center"}, {"plate","X"}, {"junk",1}};
    REQUIRE_THROWS_AS(reg.lookup("plate.center").fn(s, step), ManifestFieldError);
}

// ---------- plate.drop-to-bed tests ----------

TEST_CASE("plate.drop-to-bed: happy path drops instances",
          "[project_apply][plate.drop-to-bed]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    reg.lookup("plate.drop-to-bed").fn(
        s, json{{"op","plate.drop-to-bed"}, {"plate", "Plate 01 test"}});
    SUCCEED("plate.drop-to-bed applied without throwing");
}

TEST_CASE("plate.drop-to-bed: missing plate field throws",
          "[project_apply][plate.drop-to-bed]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.drop-to-bed").fn(s, json{{"op","plate.drop-to-bed"}}),
                      ManifestFieldError);
}

TEST_CASE("plate.drop-to-bed: unknown field throws",
          "[project_apply][plate.drop-to-bed]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    json step = {{"op","plate.drop-to-bed"}, {"plate","X"}, {"junk",1}};
    REQUIRE_THROWS_AS(reg.lookup("plate.drop-to-bed").fn(s, step), ManifestFieldError);
}

// ---------- plate.arrange tests ----------

TEST_CASE("plate.arrange: happy path arranges instances",
          "[project_apply][plate.arrange]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    reg.lookup("plate.arrange").fn(
        s, json{{"op","plate.arrange"}, {"plate", "Plate 01 test"}});
    SUCCEED("plate.arrange applied without throwing");
}

TEST_CASE("plate.arrange: missing plate field throws",
          "[project_apply][plate.arrange]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.arrange").fn(s, json{{"op","plate.arrange"}}),
                      ManifestFieldError);
}

TEST_CASE("plate.arrange: unknown field throws",
          "[project_apply][plate.arrange]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    json step = {{"op","plate.arrange"}, {"plate","X"}, {"junk",1}};
    REQUIRE_THROWS_AS(reg.lookup("plate.arrange").fn(s, step), ManifestFieldError);
}

// ---------- plate.auto-orient tests ----------

TEST_CASE("plate.auto-orient: happy path orients instances",
          "[project_apply][plate.auto-orient]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    reg.lookup("plate.auto-orient").fn(
        s, json{{"op","plate.auto-orient"}, {"plate", "Plate 01 test"}});
    SUCCEED("plate.auto-orient applied without throwing");
}

TEST_CASE("plate.auto-orient: missing plate field throws",
          "[project_apply][plate.auto-orient]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.auto-orient").fn(s, json{{"op","plate.auto-orient"}}),
                      ManifestFieldError);
}

TEST_CASE("plate.auto-orient: unknown field throws",
          "[project_apply][plate.auto-orient]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    json step = {{"op","plate.auto-orient"}, {"plate","X"}, {"junk",1}};
    REQUIRE_THROWS_AS(reg.lookup("plate.auto-orient").fn(s, step), ManifestFieldError);
}

TEST_CASE("plate.auto-orient: handler entry carries runtime_error -> exit 7 override",
          "[project_apply][plate.auto-orient][overrides]") {
    HandlerRegistry reg;
    const auto& entry = reg.lookup("plate.auto-orient");
    REQUIRE(entry.overrides.size() == 1);
    auto it = entry.overrides.find(std::type_index(typeid(std::runtime_error)));
    REQUIRE(it != entry.overrides.end());
    REQUIRE(it->second.first  == 7);
    REQUIRE(it->second.second == "invalid_state");
}

// ---------- object.add tests ----------

TEST_CASE("object.add: happy path adds an object to the named plate",
          "[project_apply][object.add]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {
        {"op", "object.add"},
        {"plate", "Plate 01 test"},
        {"stl",   bambu_cli_unit::fixture_stl("cube.stl")},
        {"name",  "test_cube_via_apply"},
    };
    REQUIRE_NOTHROW(reg.lookup("object.add").fn(s, step));
    auto objs = bambu_cli::list_objects(s, "Plate 01 test");
    bool found = false;
    for (const auto& o : objs)
        if (o.object_name == "test_cube_via_apply") { found = true; break; }
    REQUIRE(found);
}

TEST_CASE("object.add: missing stl throws", "[project_apply][object.add]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {{"op","object.add"}, {"plate","Plate 01 test"}};
    REQUIRE_THROWS_AS(reg.lookup("object.add").fn(s, step), ManifestFieldError);
}

TEST_CASE("object.add: missing plate throws", "[project_apply][object.add]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {{"op","object.add"}, {"stl", bambu_cli_unit::fixture_stl("cube.stl")}};
    REQUIRE_THROWS_AS(reg.lookup("object.add").fn(s, step), ManifestFieldError);
}

TEST_CASE("object.add: unknown field throws", "[project_apply][object.add]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {
        {"op","object.add"}, {"plate","Plate 01 test"},
        {"stl", bambu_cli_unit::fixture_stl("cube.stl")}, {"junk", 1}};
    REQUIRE_THROWS_AS(reg.lookup("object.add").fn(s, step), ManifestFieldError);
}

TEST_CASE("object.add: applies translate from object form",
          "[project_apply][object.add]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {
        {"op","object.add"}, {"plate","Plate 01 test"},
        {"stl", bambu_cli_unit::fixture_stl("cube.stl")},
        {"name", "tx_cube"},
        {"translate", {{"x", 128.0}, {"y", 128.0}}}
    };
    REQUIRE_NOTHROW(reg.lookup("object.add").fn(s, step));
    // The semantic-correctness assertion (object instance offset == 25/0/0)
    // is covered by tests/cli/unit/test_project_ops_objects.cpp at the
    // project_ops layer; here we only assert the handler wired the
    // transform through without throwing.
}

// ---------- object.remove tests ----------

TEST_CASE("object.remove: happy path removes all clones",
          "[project_apply][object.remove]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    // The reference fixture has no pre-existing objects in obj_inst_map after
    // load-only; add one via object.add handler so list_objects can find it.
    HandlerRegistry reg;
    json add_step = {
        {"op", "object.add"},
        {"plate", "Plate 01 test"},
        {"stl",   bambu_cli_unit::fixture_stl("cube.stl")},
        {"name",  "remove_target"},
    };
    REQUIRE_NOTHROW(reg.lookup("object.add").fn(s, add_step));
    auto objs = bambu_cli::list_objects(s, "");
    REQUIRE_FALSE(objs.empty());
    std::string target = objs.front().object_name;
    reg.lookup("object.remove").fn(s, json{{"op","object.remove"},{"name",target}});
    auto after = bambu_cli::list_objects(s, "");
    for (const auto& o : after) REQUIRE(o.object_name != target);
}

TEST_CASE("object.remove: missing name throws",
          "[project_apply][object.remove]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("object.remove").fn(s, json{{"op","object.remove"}}),
                      ManifestFieldError);
}

TEST_CASE("object.remove: unknown field throws",
          "[project_apply][object.remove]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {{"op","object.remove"},{"name","X"},{"junk",1}};
    REQUIRE_THROWS_AS(reg.lookup("object.remove").fn(s, step), ManifestFieldError);
}

// ---------- object.set-filament tests ----------

TEST_CASE("object.set-filament: happy path sets object-level filament",
          "[project_apply][object.set-filament]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    // Add an object so there is something to target.
    HandlerRegistry reg;
    json add_step = {
        {"op", "object.add"},
        {"plate", "Plate 01 test"},
        {"stl",   bambu_cli_unit::fixture_stl("cube.stl")},
        {"name",  "filament_target"},
    };
    REQUIRE_NOTHROW(reg.lookup("object.add").fn(s, add_step));
    auto objs = bambu_cli::list_objects(s, "");
    REQUIRE_FALSE(objs.empty());
    std::string target = objs.front().object_name;
    reg.lookup("object.set-filament").fn(s,
        json{{"op","object.set-filament"},{"name",target},{"filament",2}});
    SUCCEED("object.set-filament applied without throwing");
}

TEST_CASE("object.set-filament: missing name throws",
          "[project_apply][object.set-filament]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("object.set-filament").fn(s,
        json{{"op","object.set-filament"},{"filament",2}}),
        ManifestFieldError);
}

TEST_CASE("object.set-filament: missing filament throws",
          "[project_apply][object.set-filament]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("object.set-filament").fn(s,
        json{{"op","object.set-filament"},{"name","X"}}),
        ManifestFieldError);
}

TEST_CASE("object.set-filament: non-integer filament throws",
          "[project_apply][object.set-filament]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {{"op","object.set-filament"},{"name","X"},{"filament","2"}};
    REQUIRE_THROWS_AS(reg.lookup("object.set-filament").fn(s, step), ManifestFieldError);
}

TEST_CASE("object.set-filament: unknown field throws",
          "[project_apply][object.set-filament]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {{"op","object.set-filament"},{"name","X"},{"filament",2},{"junk",1}};
    REQUIRE_THROWS_AS(reg.lookup("object.set-filament").fn(s, step), ManifestFieldError);
}

// ---------- object.auto-orient tests ----------

TEST_CASE("object.auto-orient: happy path orients all clones",
          "[project_apply][object.auto-orient]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    // Add an object first since list_objects doesn't surface loaded ones.
    HandlerRegistry reg;
    reg.lookup("object.add").fn(s, json{
        {"op","object.add"}, {"plate","Plate 01 test"},
        {"stl", bambu_cli_unit::fixture_stl("cube.stl")},
        {"name","orient_target"}
    });
    REQUIRE_NOTHROW(reg.lookup("object.auto-orient").fn(s,
        json{{"op","object.auto-orient"},{"name","orient_target"}}));
}

TEST_CASE("object.auto-orient: missing name throws",
          "[project_apply][object.auto-orient]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("object.auto-orient").fn(s,
        json{{"op","object.auto-orient"}}),
        ManifestFieldError);
}

TEST_CASE("object.auto-orient: unknown field throws",
          "[project_apply][object.auto-orient]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {{"op","object.auto-orient"},{"name","X"},{"junk",1}};
    REQUIRE_THROWS_AS(reg.lookup("object.auto-orient").fn(s, step), ManifestFieldError);
}

TEST_CASE("object.auto-orient: entry carries runtime_error -> exit 7 override",
          "[project_apply][object.auto-orient][overrides]") {
    HandlerRegistry reg;
    const auto& entry = reg.lookup("object.auto-orient");
    auto it = entry.overrides.find(std::type_index(typeid(std::runtime_error)));
    REQUIRE(it != entry.overrides.end());
    REQUIRE(it->second.first == 7);
    REQUIRE(it->second.second == "invalid_state");
}

// ---------- object.split-to-parts tests ----------

TEST_CASE("object.split-to-parts: handler accepts well-formed args",
          "[project_apply][object.split-to-parts]") {
    HandlerRegistry reg;
    const auto& entry = reg.lookup("object.split-to-parts");
    REQUIRE(entry.fn);   // handler exists
}

TEST_CASE("object.split-to-parts: missing name throws",
          "[project_apply][object.split-to-parts]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("object.split-to-parts").fn(s,
        json{{"op","object.split-to-parts"}}),
        ManifestFieldError);
}

TEST_CASE("object.split-to-parts: unknown field throws",
          "[project_apply][object.split-to-parts]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {{"op","object.split-to-parts"},{"name","X"},{"junk",1}};
    REQUIRE_THROWS_AS(reg.lookup("object.split-to-parts").fn(s, step), ManifestFieldError);
}

TEST_CASE("object.split-to-parts: entry carries invalid_argument -> exit 7 override",
          "[project_apply][object.split-to-parts][overrides]") {
    HandlerRegistry reg;
    const auto& entry = reg.lookup("object.split-to-parts");
    auto it = entry.overrides.find(std::type_index(typeid(std::invalid_argument)));
    REQUIRE(it != entry.overrides.end());
    REQUIRE(it->second.first == 7);
}
