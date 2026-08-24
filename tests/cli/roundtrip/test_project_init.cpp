// Roundtrip tests for bambu-cli project load/save (cross-project port from
// OrcaSlicer tests/cli/roundtrip/test_project_init.cpp). Verifies that the
// committed reference 3mf round-trips through load_project + save_project
// unchanged at the plate / object level, and that the G2 rebuild of
// PlateData::objects_and_instances populates from obj_inst_map on load.
#include "../test_helpers.hpp"

#include "io.hpp"
#include "project_ops.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

// Note: Bambu's canonical reference.3mf is an empty project (zero objects) by
// design; tests that need a populated model add an object in-memory before the
// round-trip (matches the convention in tests/cli/unit/test_project_ops_*).
// This is a divergence from Orca, whose reference has objects out of the box.

TEST_CASE("bambu-cli: load_project loads model and config from reference 3mf",
          "[bambu-cli][roundtrip][project_init]") {
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));

    bambu_cli::ProjectState s;
    auto r = bambu_cli::load_project(ref, s);
    INFO("load_project: " << r.error_message);
    REQUIRE(r.ok);
    REQUIRE_FALSE(s.plate_data.empty());
    // project_config carries at least the printer/filament/process keys
    // baked into the committed reference; an empty config would indicate
    // a load_bbs_3mf regression.
    REQUIRE_FALSE(s.project_config.keys().empty());
}

TEST_CASE("bambu-cli: save_project round-trips the reference 3mf",
          "[bambu-cli][roundtrip][project_init]") {
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const std::string out = fresh_temp_path(".3mf");

    bambu_cli::ProjectState s_in;
    REQUIRE(bambu_cli::load_project(ref, s_in).ok);
    REQUIRE(bambu_cli::save_project(s_in, out).ok);

    bambu_cli::ProjectState s_out;
    REQUIRE(bambu_cli::load_project(out, s_out).ok);
    REQUIRE(s_out.plate_data.size()    == s_in.plate_data.size());
    REQUIRE(s_out.model.objects.size() == s_in.model.objects.size());

    fs::remove(out);
}

TEST_CASE("bambu-cli: load_project rebuilds plate->objects_and_instances from obj_inst_map (G2)",
          "[bambu-cli][roundtrip][project_init]") {
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const std::string out = fresh_temp_path("_rt_g2.3mf");

    // Bootstrap: add one object on plate 1, save, reload. The reload exercises
    // the G2 rebuild of PlateData::objects_and_instances from obj_inst_map
    // populated during the bbs_3mf parse.
    {
        bambu_cli::ProjectState s;
        REQUIRE(bambu_cli::load_project(ref, s).ok);
        const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
        REQUIRE(bambu_cli::add_object_to_plate(
            s, s.plate_data.front()->plate_name, stl, "g2_cube",
            -1, nullptr, 1, nullptr).ok);
        REQUIRE(bambu_cli::save_project(s, out).ok);
    }

    bambu_cli::ProjectState s;
    REQUIRE(bambu_cli::load_project(out, s).ok);

    // After load_project's G2 rebuild, at least one plate must have a
    // non-empty objects_and_instances vector with in-range indices.
    bool any_plate_has_objects = false;
    for (auto* p : s.plate_data) {
        REQUIRE(p != nullptr);
        if (!p->objects_and_instances.empty()) {
            any_plate_has_objects = true;
            for (auto& oi_ii : p->objects_and_instances) {
                int oi = oi_ii.first;
                int ii = oi_ii.second;
                REQUIRE(oi >= 0);
                REQUIRE(oi < int(s.model.objects.size()));
                auto* obj = s.model.objects[oi];
                REQUIRE(ii >= 0);
                REQUIRE(ii < int(obj->instances.size()));
            }
        }
    }
    REQUIRE(any_plate_has_objects);

    fs::remove(out);
}
