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
