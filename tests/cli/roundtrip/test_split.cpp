// Roundtrip tests for bambu-cli split-to-parts (cross-project port from
// OrcaSlicer tests/cli/roundtrip/test_split.cpp). Verifies that volume
// count AND per-volume extruder survive a full load -> split -> save ->
// reload cycle via libslic3r's bbs_3mf reader.
#include "../test_helpers.hpp"

#include "io.hpp"
#include "project_ops.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>

#include <string>

namespace fs = boost::filesystem;
using namespace bambu_cli_test;

namespace {
Slic3r::ModelObject* find_object(bambu_cli::ProjectState& s, const std::string& name) {
    for (auto* obj : s.model.objects)
        if (obj && obj->name == name) return obj;
    return nullptr;
}
}

TEST_CASE("volume count survives save/load roundtrip",
          "[bambu-cli][roundtrip][split]") {
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const std::string out = fresh_temp_path("_rt_split_count.3mf");
    fs::copy_file(ref, out, fs::copy_options::overwrite_existing);

    {
        bambu_cli::ProjectState s;
        REQUIRE(bambu_cli::load_project(out, s).ok);
        const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/two_cubes.stl";
        REQUIRE(bambu_cli::add_object_to_plate(
            s, s.plate_data.front()->plate_name, stl, "rt", -1, nullptr, 1, nullptr).ok);
        REQUIRE(bambu_cli::split_object_to_parts(s, "rt") == 2);
        REQUIRE(bambu_cli::save_project(s, out).ok);
    }

    bambu_cli::ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto* obj = find_object(s2, "rt");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    REQUIRE(obj->volumes[0]->name == std::string("rt_1"));
    REQUIRE(obj->volumes[1]->name == std::string("rt_2"));

    fs::remove(out);
}

TEST_CASE("per-part extruder survives save/load roundtrip",
          "[bambu-cli][roundtrip][split]") {
    using namespace Slic3r;
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const std::string out = fresh_temp_path("_rt_split_extruder.3mf");
    fs::copy_file(ref, out, fs::copy_options::overwrite_existing);

    {
        bambu_cli::ProjectState s;
        REQUIRE(bambu_cli::load_project(out, s).ok);
        const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/two_cubes.stl";
        REQUIRE(bambu_cli::add_object_to_plate(
            s, s.plate_data.front()->plate_name, stl, "rtx", -1, nullptr, 1, nullptr).ok);
        REQUIRE(bambu_cli::split_object_to_parts(s, "rtx") == 2);
        REQUIRE(bambu_cli::set_object_filament(s, "rtx", 1, "rtx_1").ok);
        REQUIRE(bambu_cli::set_object_filament(s, "rtx", 2, "rtx_2").ok);
        REQUIRE(bambu_cli::save_project(s, out).ok);
    }

    bambu_cli::ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto* obj = find_object(s2, "rtx");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    auto* e1 = obj->volumes[0]->config.get().opt<ConfigOptionInt>("extruder");
    auto* e2 = obj->volumes[1]->config.get().opt<ConfigOptionInt>("extruder");
    REQUIRE(e1 != nullptr);
    REQUIRE(e2 != nullptr);
    REQUIRE(e1->value == 1);
    REQUIRE(e2->value == 2);

    fs::remove(out);
}
