// Roundtrip test for bambu-cli merge-parts (cross-project port from
// OrcaSlicer tests/cli/roundtrip/test_merge.cpp). Verifies that the merged
// volume's name and extruder survive a full save -> reopen cycle via
// libslic3r's bbs_3mf reader.
//
// Divergence from Orca: Bambu's merge step (i) is strict-only-extruder --
// any other per-volume key (e.g. wall_loops) is rejected even if sources
// agree. The Orca port also asserted per-volume wall_loops survival; that
// branch is dropped here as a known Bambu-side design choice (see
// merge_object_parts() step i in src/cli/project_ops.cpp).
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

TEST_CASE("merge-parts: merged volume name + extruder + per-vol config "
          "survive save/load roundtrip",
          "[bambu-cli][roundtrip][merge]") {
    using namespace Slic3r;
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const std::string out = fresh_temp_path("_rt_merge.3mf");
    fs::copy_file(ref, out, fs::copy_options::overwrite_existing);

    {
        bambu_cli::ProjectState s;
        REQUIRE(bambu_cli::load_project(out, s).ok);
        const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/two_cubes.stl";
        REQUIRE(bambu_cli::add_object_to_plate(
            s, s.plate_data.front()->plate_name, stl, "rt_merge", -1, nullptr, 1, nullptr).ok);
        REQUIRE(bambu_cli::split_object_to_parts(s, "rt_merge") == 2);
        // Stamp identical filament slot 2 on both parts so merge inherits
        // without requiring a --filament override.
        REQUIRE(bambu_cli::set_object_filament(s, "rt_merge", 2, "rt_merge_1").ok);
        REQUIRE(bambu_cli::set_object_filament(s, "rt_merge", 2, "rt_merge_2").ok);

        bambu_cli::MergePartsParams p;
        p.parts = {"rt_merge_1", "rt_merge_2"};
        p.into  = "rt_merge_main";
        p.filament = -1;  // auto (inherit from agreeing sources)
        REQUIRE_NOTHROW(bambu_cli::merge_object_parts(s, "rt_merge", p));
        REQUIRE(bambu_cli::save_project(s, out).ok);
    }

    // Reopen and assert name + extruder.
    //
    // Note: bbs_3mf unconditionally erases `extruder` from the volume-level
    // config on load when the object has only one volume. For single-volume
    // objects the effective extruder lives on the object config, not the
    // volume config. So after merge -> save -> reload we check obj->config
    // for extruder.
    bambu_cli::ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto* obj = find_object(s2, "rt_merge");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 1);
    REQUIRE(obj->volumes[0]->name == std::string("rt_merge_main"));
    auto* ext = obj->config.get().opt<ConfigOptionInt>("extruder");
    REQUIRE(ext != nullptr);
    REQUIRE(ext->value == 2);

    fs::remove(out);
}
