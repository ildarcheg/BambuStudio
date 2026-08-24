// plate_index convention + invariant-guard thumbnail indexing.
//
// Convention under test: PlateData::plate_index is 0-based and equals the
// plate's position in ProjectState::plate_data — the bbs_3mf loader stores
// plater_id - 1 (bbs_3mf.cpp:1642) and remove_plate re-compacts indices to
// positions. The store path names thumbnail entries by *position*, 1-based
// (plate_<i+1>.png, bbs_3mf.cpp:6309), so guard check (b) must look up
// entries by position too.
#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "../test_helpers.hpp"
#include "project_ops.hpp"
#include "invariant_guard.hpp"
#include "io.hpp"

#include <boost/filesystem.hpp>
#include <type_traits>

namespace fs = boost::filesystem;
using bambu_cli::ProjectState;

TEST_CASE("add_plate: fresh plate on an empty plate list gets 0-based index "
          "matching its position", "[unit][plate_index]") {
    ProjectState s;   // deliberately no plates at all
    REQUIRE(bambu_cli::add_plate(s, "Fresh").ok);
    REQUIRE(s.plate_data.size() == 1);
    REQUIRE(s.plate_data[0]->plate_index == 0);
}

TEST_CASE("add_plate: appended plate index equals its position on a loaded "
          "project", "[unit][plate_index]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::add_plate(s, "Plate-2").ok);
    for (size_t i = 0; i < s.plate_data.size(); ++i)
        REQUIRE(s.plate_data[i]->plate_index == static_cast<int>(i));
}

TEST_CASE("invariant guard: missing LAST plate thumbnail is detected on a "
          "2-plate project", "[unit][invariant_thumbs]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::add_plate(s, "Plate-2").ok);

    const std::string out =
        bambu_cli_test::fresh_temp_path("_thumbguard.3mf");
    REQUIRE(bambu_cli::save_project(s, out).ok);

    // Drop the LAST plate's thumbnail from the saved archive. The guard's
    // per-plate thumbnail check exists precisely to catch this; an
    // index-vs-position mixup makes it check plate_1.png twice and never
    // look at plate_2.png.
    bambu_cli_unit::mutate_archive_remove_entry(out, "Metadata/plate_2.png");

    auto gr = bambu_cli::run_guard(out, s);
    REQUIRE_FALSE(gr.ok);
    REQUIRE(gr.failed_check == "thumbnails");

    fs::remove(out);
}

TEST_CASE("obj_inst_map: canonical single-domain shape — key == loaded_id "
          "for loaded and CLI-added entries alike", "[unit][obj_inst_map]") {
    // The loader emplaces {3mf object_id -> (instance_id, identify_id)};
    // add_object_to_plate used to key by in-memory obj_idx. Two different
    // key domains in one map = collision-prone (both are small ints), and
    // no consumer reads the keys anyway — every reader uses value.second
    // (the loaded_id). Canonical CLI form: key == value.second, so the map
    // is a per-plate {loaded_id -> (instance_id, loaded_id)} with globally
    // unique keys by construction.
    ProjectState s;
    auto lr = bambu_cli::load_project(BAMBU_CLI_FIXTURE_TEST_REFERENCE_3MF, s);
    REQUIRE(lr.ok);
    REQUIRE_FALSE(s.model.objects.empty());   // fixture carries one object

    bambu_cli::ObjectRef ref;
    bambu_cli::ManualTransform tf;
    tf.has_translate = true; tf.tx = 60; tf.ty = 60; tf.tz = 0;
    REQUIRE(bambu_cli::add_object_to_plate(
        s, s.plate_data.front()->plate_name,
        bambu_cli_unit::fixture_stl("cube.stl"),
        "MapShapeCube", -1, &tf, 2, &ref).ok);

    size_t entries = 0;
    for (const auto* pd : s.plate_data) {
        if (!pd) continue;
        for (const auto& kv : pd->obj_inst_map) {
            INFO("plate '" << pd->plate_name << "' key=" << kv.first
                 << " value=(" << kv.second.first << "," << kv.second.second
                 << ")");
            REQUIRE(kv.first == kv.second.second);
            REQUIRE(kv.second.second > 0);
            ++entries;
        }
    }
    // 1 loaded + 2 added instances must all be represented.
    REQUIRE(entries >= 3);
}

// ProjectState owns raw PlateData* (custom dtor deletes them). A defaulted
// move-assign would leak the target's plates: vector move-assign frees
// nothing. Nothing in the codebase move-assigns a ProjectState, so the
// operation is deleted outright — this pin turns any future reintroduction
// into a compile error instead of a silent leak.
static_assert(!std::is_move_assignable<bambu_cli::ProjectState>::value,
              "ProjectState move-assign must stay deleted (raw-owning plate_data)");
static_assert(!std::is_copy_assignable<bambu_cli::ProjectState>::value,
              "ProjectState copy-assign must stay deleted");
