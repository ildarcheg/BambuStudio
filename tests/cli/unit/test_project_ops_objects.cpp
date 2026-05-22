#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_ops.hpp"
#include "exceptions.hpp"

#include <libslic3r/Model.hpp>

#include <stdexcept>

using bambu_cli::ProjectState;

static std::string first_plate(const ProjectState& s) {
    REQUIRE_FALSE(s.plate_data.empty());
    return s.plate_data.front()->plate_name;
}

TEST_CASE("add_object_to_plate: basic add stamps source_file on volume",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");

    bambu_cli::ObjectRef ref;
    auto r = bambu_cli::add_object_to_plate(
        s, first_plate(s), stl, "", -1, nullptr, 1, &ref);
    REQUIRE(r.ok);

    REQUIRE(s.model.objects.size() == 1);
    auto* obj = s.model.objects[0];
    REQUIRE(obj->name == "cube");
    REQUIRE_FALSE(obj->volumes.empty());
    REQUIRE(obj->volumes[0]->source.input_file == stl);
}

TEST_CASE("add_object_to_plate: missing STL -> file_not_found",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(
        bambu_cli::add_object_to_plate(
            s, first_plate(s), "Z:/no/such/file.stl", "", -1, nullptr, 1, nullptr),
        bambu_cli::FileNotFoundError);
}

TEST_CASE("add_object_to_plate: unknown plate -> unknown_reference",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    REQUIRE_THROWS_AS(
        bambu_cli::add_object_to_plate(
            s, "NoSuchPlate", stl, "", -1, nullptr, 1, nullptr),
        std::out_of_range);
}

TEST_CASE("add_object_to_plate: filament out of range rolls back",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    size_t pre = s.model.objects.size();
    REQUIRE_THROWS_AS(
        bambu_cli::add_object_to_plate(
            s, first_plate(s), stl, "", /*filament=*/99, nullptr, 1, nullptr),
        std::invalid_argument);
    REQUIRE(s.model.objects.size() == pre);   // rollback
}

TEST_CASE("add_object_to_plate: filament=0 -> usage_error",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    REQUIRE_THROWS_AS(
        bambu_cli::add_object_to_plate(
            s, first_plate(s), stl, "", /*filament=*/0, nullptr, 1, nullptr),
        std::invalid_argument);
}

TEST_CASE("add_object_to_plate: --count 3 produces 3 ModelObjects",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    auto r = bambu_cli::add_object_to_plate(
        s, first_plate(s), stl, "", -1, nullptr, /*count=*/3, nullptr);
    REQUIRE(r.ok);
    REQUIRE(s.model.objects.size() == 3);
    for (const auto* o : s.model.objects) REQUIRE(o->name == "cube");
}

TEST_CASE("add_object_to_plate: count<=0 clamps to 1", "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    auto r = bambu_cli::add_object_to_plate(
        s, first_plate(s), stl, "", -1, nullptr, /*count=*/0, nullptr);
    REQUIRE(r.ok);
    REQUIRE(s.model.objects.size() == 1);
}

TEST_CASE("add_object_to_plate: manual translate off-bed -> placement_failure",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    bambu_cli::ManualTransform tf;
    tf.has_translate = true;
    tf.tx = 9999.0; tf.ty = 0.0; tf.tz = 0.0;
    REQUIRE_THROWS_AS(
        bambu_cli::add_object_to_plate(
            s, first_plate(s), stl, "", -1, &tf, 1, nullptr),
        bambu_cli::PlacementFailure);
    REQUIRE(s.model.objects.empty());   // rolled back
}

TEST_CASE("remove_object: removes all N copies by name", "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    REQUIRE(bambu_cli::add_object_to_plate(
        s, first_plate(s), stl, "", -1, nullptr, 3, nullptr).ok);
    REQUIRE(s.model.objects.size() == 3);
    auto r = bambu_cli::remove_object(s, "cube");
    REQUIRE(r.ok);
    REQUIRE(s.model.objects.empty());
}

TEST_CASE("remove_object: name not found -> unknown_reference",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::remove_object(s, "no_such"), std::out_of_range);
}

TEST_CASE("set_object_filament: stamps extruder on all copies (group semantics)",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    REQUIRE(bambu_cli::add_object_to_plate(
        s, first_plate(s), stl, "", -1, nullptr, 2, nullptr).ok);

    auto r = bambu_cli::set_object_filament(s, "cube", 3);
    REQUIRE(r.ok);
    for (auto* o : s.model.objects) {
        const auto* opt = o->config.option("extruder");
        REQUIRE(opt != nullptr);
        REQUIRE(static_cast<const Slic3r::ConfigOptionInt*>(opt)->value == 3);
    }
}

TEST_CASE("set_object_filament: retrofit re-stamps source.input_file",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    REQUIRE(bambu_cli::add_object_to_plate(
        s, first_plate(s), stl, "", -1, nullptr, 1, nullptr).ok);

    // Simulate a 3mf load that lost the volume source attribution.
    for (auto* v : s.model.objects[0]->volumes) v->source.input_file.clear();
    REQUIRE(bambu_cli::set_object_filament(s, "cube", 2).ok);
    REQUIRE(s.model.objects[0]->volumes[0]->source.input_file == stl);
}

TEST_CASE("set_object_filament: out-of-range -> usage_error",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    REQUIRE(bambu_cli::add_object_to_plate(
        s, first_plate(s), stl, "", -1, nullptr, 1, nullptr).ok);
    REQUIRE_THROWS_AS(bambu_cli::set_object_filament(s, "cube", 99),
                      std::invalid_argument);
}

TEST_CASE("set_object_filament: object not found -> unknown_reference",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::set_object_filament(s, "missing", 1),
                      std::out_of_range);
}

TEST_CASE("list_objects: returns plate-attributed entries with extruders",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    REQUIRE(bambu_cli::add_object_to_plate(
        s, first_plate(s), stl, "", /*filament=*/2, nullptr, 1, nullptr).ok);
    auto objs = bambu_cli::list_objects(s, "");
    REQUIRE(objs.size() == 1);
    REQUIRE(objs[0].object_name == "cube");
    REQUIRE(objs[0].plate_name  == first_plate(s));
    REQUIRE(objs[0].extruder    == 2);
}

// Same-plate inclusion + nonexistent-plate exclusion are easier to
// state than cross-plate exclusion; both are covered. Cross-plate
// exclusion is now covered by the next TEST_CASE.
TEST_CASE("list_objects: --plate filter — same-plate inclusion + nonexistent-plate exclusion",
          "[unit][objects]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::add_plate(s, "Plate-2").ok);
    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    // Both objects on the SAME plate to avoid the loaded_id collision.
    REQUIRE(bambu_cli::add_object_to_plate(
        s, first_plate(s), stl, "obj1", -1, nullptr, 1, nullptr).ok);
    REQUIRE(bambu_cli::add_object_to_plate(
        s, first_plate(s), stl, "obj2", -1, nullptr, 1, nullptr).ok);

    auto on_plate1 = bambu_cli::list_objects(s, first_plate(s));
    REQUIRE(on_plate1.size() == 2);

    auto on_nowhere = bambu_cli::list_objects(s, "NoSuchPlate");
    REQUIRE(on_nowhere.empty());

    auto all = bambu_cli::list_objects(s, "");
    REQUIRE(all.size() == 2);
}

TEST_CASE("list_objects: --plate filter excludes objects from other plates",
          "[unit][objects][cross_plate]") {
    // Regression test for the loaded_id collision bug: previously,
    // base_loaded_id = pd->obj_inst_map.size() + 1 produced the same
    // loaded_id for objects added to different freshly-empty plates,
    // making cross-plate filtering return both objects.  Fixed by
    // computing the global max(loaded_id) + 1 instead.
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::add_plate(s, "Plate-2").ok);

    const std::string stl = bambu_cli_unit::fixture_stl("cube.stl");
    REQUIRE(bambu_cli::add_object_to_plate(
        s, first_plate(s), stl, "p1obj", -1, nullptr, 1, nullptr).ok);
    REQUIRE(bambu_cli::add_object_to_plate(
        s, "Plate-2", stl, "p2obj", -1, nullptr, 1, nullptr).ok);

    // Sanity: both objects exist.
    REQUIRE(s.model.objects.size() == 2);

    // The fix: filtering by plate-1 returns ONLY p1obj.
    auto on_p1 = bambu_cli::list_objects(s, first_plate(s));
    REQUIRE(on_p1.size() == 1);
    REQUIRE(on_p1[0].object_name == "p1obj");

    // Filtering by Plate-2 returns ONLY p2obj.
    auto on_p2 = bambu_cli::list_objects(s, "Plate-2");
    REQUIRE(on_p2.size() == 1);
    REQUIRE(on_p2[0].object_name == "p2obj");

    // No filter returns both.
    auto all = bambu_cli::list_objects(s, "");
    REQUIRE(all.size() == 2);
}

// -------------------------------------------------------------------
// Phase E: set-filament --part NAME (string, not integer).
// ModelVolume::split names parts as "<obj_name>_1", "<obj_name>_2", ...
// Use two_cubes.stl + split-to-parts to get real volume names.
// -------------------------------------------------------------------
TEST_CASE("set_object_filament: part NAME writes to named volume config, not object config",
          "[unit][m3_part_filament]") {
    bambu_cli::ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/two_cubes.stl";

    // Add two_cubes.stl as "twin" then split -> volumes "twin_1", "twin_2".
    REQUIRE(bambu_cli::add_object_to_plate(
        s, s.plate_data.front()->plate_name, stl, "twin", -1, nullptr, 1, nullptr).ok);
    REQUIRE(bambu_cli::split_object_to_parts(s, "twin") == 2);

    auto r = bambu_cli::set_object_filament(s, "twin", 2, "twin_1");
    REQUIRE(r.ok);

    // Find the "twin" object — it is the one with volumes.
    const Slic3r::ModelObject* obj = nullptr;
    for (const auto* o : s.model.objects)
        if (o && o->name == "twin") { obj = o; break; }
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);

    // twin_1's extruder must be 2.
    const Slic3r::ModelVolume* v1 = nullptr;
    const Slic3r::ModelVolume* v2 = nullptr;
    for (const auto* v : obj->volumes) {
        if (v->name == "twin_1") v1 = v;
        if (v->name == "twin_2") v2 = v;
    }
    REQUIRE(v1 != nullptr);
    REQUIRE(v2 != nullptr);
    const auto* eopt1 = v1->config.option("extruder");
    REQUIRE(eopt1 != nullptr);
    REQUIRE(static_cast<const Slic3r::ConfigOptionInt*>(eopt1)->value == 2);

    // twin_2 must NOT have been touched.
    const auto* eopt2 = v2->config.option("extruder");
    if (eopt2 != nullptr)
        REQUIRE(static_cast<const Slic3r::ConfigOptionInt*>(eopt2)->value != 2);

    // Object-level extruder must NOT be 2 from this call.
    const auto* obj_eopt = obj->config.option("extruder");
    if (obj_eopt != nullptr)
        REQUIRE(static_cast<const Slic3r::ConfigOptionInt*>(obj_eopt)->value != 2);
}

TEST_CASE("set_object_filament: unknown part name -> unknown_reference",
          "[unit][m3_part_filament]") {
    bambu_cli::ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/two_cubes.stl";
    REQUIRE(bambu_cli::add_object_to_plate(
        s, s.plate_data.front()->plate_name, stl, "twin", -1, nullptr, 1, nullptr).ok);
    REQUIRE(bambu_cli::split_object_to_parts(s, "twin") == 2);

    REQUIRE_THROWS_AS(
        bambu_cli::set_object_filament(s, "twin", 1, "nonexistent_volume"),
        std::out_of_range);
}
