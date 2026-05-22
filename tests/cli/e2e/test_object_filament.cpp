#include "test_helpers.hpp"
#include "archive_invariants.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

static std::string first_plate_name(const std::string& path) {
    auto r = spawn_cli({"--json", "plate", "list", path});
    REQUIRE(r.exit_code == 0);
    auto p = r.stdout_text.find("\"plates\":[\"");
    if (p == std::string::npos) return "";
    p += std::string("\"plates\":[\"").size();
    auto q = r.stdout_text.find("\"", p);
    return r.stdout_text.substr(p, q - p);
}

TEST_CASE("object add --filament 2: <part> has BOTH extruder=2 AND source_file (Bug B regression)",
          "[m5][object_filament][bug_b]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    auto r = spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl, "--filament", "2"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    bambu_cli_test::run_all_basic(out);
    bambu_cli_test::assert_parts_have_source_file(out);
    bambu_cli_test::assert_object_extruder(out, "cube", 2);

    fs::remove(out);
}

TEST_CASE("object add --filament 5: out of range (only 4 slots) -> exit 1", "[m5][object_filament]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    auto r = spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl, "--filament", "5"});
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);
    fs::remove(out);
}

TEST_CASE("object add --filament 0: invalid (1-based) -> exit 1", "[m5][object_filament]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    auto r = spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl, "--filament", "0"});
    REQUIRE(r.exit_code == 1);
    fs::remove(out);
}
