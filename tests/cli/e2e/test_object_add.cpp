#include "test_helpers.hpp"
#include "archive_invariants.hpp"

#include "libslic3r/miniz_extension.hpp"
#include <miniz.h>

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <cstring>
#include <regex>
#include <set>
#include <string>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

// Helper: name of the single plate in the committed reference 3mf.
// BS does not name plates by default; the prior spec uses "Plate 1" or empty.
// We probe via plate list and use the first non-empty (or empty) name.
static std::string first_plate_name(const std::string& path) {
    auto r = spawn_cli({"--json", "plate", "list", path});
    REQUIRE(r.exit_code == 0);
    // crude: pull first "plates":["..."] entry
    auto p = r.stdout_text.find("\"plates\":[\"");
    if (p == std::string::npos) return "";
    p += std::string("\"plates\":[\"").size();
    auto q = r.stdout_text.find("\"", p);
    return r.stdout_text.substr(p, q - p);
}

TEST_CASE("object add: STL appears on plate; source_file stamped", "[m4][object_add]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    REQUIRE(fs::exists(stl));

    const std::string plate = first_plate_name(out);

    auto r = spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    bambu_cli_test::run_all_basic(out);
    bambu_cli_test::assert_parts_have_source_file(out);

    SECTION("object list reports the new object") {
        auto lr = spawn_cli({"--json", "object", "list", out});
        REQUIRE(lr.exit_code == 0);
        REQUIRE(lr.stdout_text.find("cube") != std::string::npos);
    }

    fs::remove(out);
}

TEST_CASE("object add: missing STL -> exit 2", "[m4][object_add]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    auto r = spawn_cli({"object", "add", out, "--plate", first_plate_name(out),
                        "--stl", "Z:/no/such/file.stl"});
    REQUIRE(r.exit_code == 2);
    fs::remove(out);
}

TEST_CASE("object add: unknown plate -> exit 6", "[m4][object_add]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";

    auto r = spawn_cli({"object", "add", out, "--plate", "no-such-plate", "--stl", stl});
    REQUIRE(r.exit_code == 6);
    fs::remove(out);
}

// Cross-project audit regression: identify_id (= loaded_id) must stay unique
// across plates after objects are added via `plate add` + `object add`. Ported
// from OrcaSlicer tests/cli/e2e/test_object.cpp ("audit item 2") which pinned
// the same archive-level property after their counterpart fix.
//
// Bambu fix history: commit faa5f4dd6 ("fix(cli): global loaded_id assignment
// in add_object_to_plate") moved per-plate base_loaded_id sizing to a global
// max+1 computation. This test is the archive-level cousin of the existing
// in-memory list_objects --plate test at
// tests/cli/unit/test_project_ops_objects.cpp:225 (same root cause, deeper
// check: serialize -> reload would catch a regression that purely-in-memory
// reasoning missed).
TEST_CASE("object add: identify_id stays unique across freshly-empty plates",
          "[e2e][object_add][cross_plate]")
{
    const std::string out = fresh_temp_path("_identify_id.3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    const std::string cube = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    REQUIRE(fs::exists(cube));

    REQUIRE(spawn_cli({"plate", "add", out, "--name", "A"}).exit_code == 0);
    REQUIRE(spawn_cli({"plate", "add", out, "--name", "B"}).exit_code == 0);
    REQUIRE(spawn_cli({"object", "add", out,
                       "--plate", "A",
                       "--stl",   cube,
                       "--name",  "objA"}).exit_code == 0);
    REQUIRE(spawn_cli({"object", "add", out,
                       "--plate", "B",
                       "--stl",   cube,
                       "--name",  "objB"}).exit_code == 0);

    // Scrape every identify_id value from Metadata/model_settings.config and
    // assert every occurrence is distinct.
    mz_zip_archive zip{};
    REQUIRE(Slic3r::open_zip_reader(&zip, out));
    int idx = mz_zip_reader_locate_file(&zip, "Metadata/model_settings.config", nullptr, 0);
    REQUIRE(idx >= 0);
    size_t size = 0;
    void* p = mz_zip_reader_extract_to_heap(&zip, mz_uint(idx), &size, 0);
    REQUIRE(p != nullptr);
    std::string contents(reinterpret_cast<const char*>(p), size);
    mz_free(p);
    Slic3r::close_zip_reader(&zip);

    std::regex id_re("key=\"identify_id\"\\s+value=\"(\\d+)\"");
    std::set<std::string> ids;
    int total = 0;
    for (auto it = std::sregex_iterator(contents.begin(), contents.end(), id_re);
         it != std::sregex_iterator(); ++it) {
        ids.insert((*it)[1].str());
        ++total;
    }
    INFO("found " << total << " identify_id entries, " << ids.size() << " distinct");
    REQUIRE(total > 0);
    REQUIRE(ids.size() == size_t(total));

    fs::remove(out);
}
