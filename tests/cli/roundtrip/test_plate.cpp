// Roundtrip tests for bambu-cli plate add/remove (cross-project port from
// OrcaSlicer tests/cli/roundtrip/test_plate.cpp). Verifies that an added
// plate survives save/load with placeholder thumbnails, and that removing a
// plate drops the orphaned PNG entries from the output archive.
#include "../test_helpers.hpp"
#include "../archive_invariants.hpp"

#include "io.hpp"
#include "project_ops.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

#include <set>
#include <string>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

TEST_CASE("bambu-cli: add_plate round-trip preserves new plate with placeholder thumbnails",
          "[bambu-cli][roundtrip][plate]")
{
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const std::string in  = fresh_temp_path("_rt_add.3mf");
    const std::string out = fresh_temp_path("_rt_add_out.3mf");
    fs::copy_file(ref, in, fs::copy_options::overwrite_existing);

    bambu_cli::ProjectState s;
    REQUIRE(bambu_cli::load_project(in, s).ok);
    auto n_before = s.plate_data.size();
    REQUIRE(bambu_cli::add_plate(s, "RT_NewPlate").ok);
    REQUIRE(bambu_cli::save_project(s, out).ok);

    bambu_cli::ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    REQUIRE(s2.plate_data.size() == n_before + 1);
    bool found = false;
    for (auto* p : s2.plate_data)
        if (p && p->plate_name == "RT_NewPlate") found = true;
    REQUIRE(found);

    // Archive-level: same guarantees the runtime guard enforces -- every rels
    // Target resolves, every plate_N.png + plate_N_small.png decode as
    // 128x128 PNGs. Catches placeholder-injection regressions for the new plate.
    bambu_cli_test::assert_relationships_resolve(out);
    bambu_cli_test::assert_plate_thumbnails_128(out);

    fs::remove(in);
    fs::remove(out);
}

TEST_CASE("bambu-cli: remove_plate drops orphaned PNGs from the output archive",
          "[bambu-cli][roundtrip][plate]") {
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const std::string in  = fresh_temp_path("_rt_rm.3mf");
    const std::string out = fresh_temp_path("_rt_rm_out.3mf");
    fs::copy_file(ref, in, fs::copy_options::overwrite_existing);

    // Ensure the source has at least 3 plates by adding one and saving in place.
    {
        bambu_cli::ProjectState s;
        REQUIRE(bambu_cli::load_project(in, s).ok);
        REQUIRE(bambu_cli::add_plate(s, "ToRemove").ok);
        REQUIRE(bambu_cli::save_project(s, in).ok);
    }

    bambu_cli::ProjectState s;
    REQUIRE(bambu_cli::load_project(in, s).ok);
    auto n_before = s.plate_data.size();
    const std::string last_name = s.plate_data.back()->plate_name;
    REQUIRE(bambu_cli::remove_plate(s, last_name).ok);
    REQUIRE(bambu_cli::save_project(s, out).ok);

    // Re-open and check archive contents directly.
    auto entries = list_zip_entries(out);
    std::set<std::string> names(entries.begin(), entries.end());

    // No plate_<n_before>.png nor plate_<n_before>_small.png should remain.
    const std::string orphan    = "Metadata/plate_" + std::to_string(n_before) + ".png";
    const std::string orphan_sm = "Metadata/plate_" + std::to_string(n_before) + "_small.png";
    REQUIRE_FALSE(names.count(orphan));
    REQUIRE_FALSE(names.count(orphan_sm));

    fs::remove(in);
    fs::remove(out);
}
