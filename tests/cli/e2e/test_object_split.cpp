#include "test_helpers.hpp"
#include "archive_invariants.hpp"
#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

static std::string first_plate(const std::string& path) {
    auto r = spawn_cli({"--json", "plate", "list", path});
    REQUIRE(r.exit_code == 0);
    auto p = r.stdout_text.find("\"plates\":[\"");
    if (p == std::string::npos) return "";
    p += std::string("\"plates\":[\"").size();
    auto q = r.stdout_text.find("\"", p);
    return r.stdout_text.substr(p, q - p);
}

TEST_CASE("object split-to-parts: two_cubes.stl splits into 2 volumes", "[e2e][split]") {
    const std::string tmp = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), tmp, fs::copy_option::overwrite_if_exists);
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/two_cubes.stl";
    REQUIRE(fs::exists(stl));

    // Add two_cubes.stl as an object
    auto ra = spawn_cli({"object", "add", tmp, "--plate", first_plate(tmp),
                         "--stl", stl, "--name", "twin"});
    INFO("add stderr: " << ra.stderr_text);
    REQUIRE(ra.exit_code == 0);

    // Split it
    auto rs = spawn_cli({"object", "split-to-parts", tmp, "--name", "twin"});
    INFO("split stderr: " << rs.stderr_text);
    REQUIRE(rs.exit_code == 0);
    REQUIRE(rs.stdout_text.find("twin") != std::string::npos);
    REQUIRE(rs.stdout_text.find("2 parts") != std::string::npos);

    // Verify archive integrity
    bambu_cli_test::run_all_basic(tmp);

    fs::remove(tmp);
}

TEST_CASE("object split-to-parts: bad name -> exit 6", "[e2e][split]") {
    const std::string tmp = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), tmp, fs::copy_option::overwrite_if_exists);

    auto r = spawn_cli({"object", "split-to-parts", tmp, "--name", "ghost"});
    REQUIRE(r.exit_code == 6);
    fs::remove(tmp);
}

TEST_CASE("object split-to-parts: single-component mesh -> exit 7", "[e2e][split]") {
    const std::string tmp = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), tmp, fs::copy_option::overwrite_if_exists);
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";

    auto ra = spawn_cli({"object", "add", tmp, "--plate", first_plate(tmp),
                         "--stl", stl, "--name", "box"});
    REQUIRE(ra.exit_code == 0);

    auto r = spawn_cli({"object", "split-to-parts", tmp, "--name", "box"});
    REQUIRE(r.exit_code == 7);
    fs::remove(tmp);
}

TEST_CASE("object split-to-parts: --output writes to separate file", "[e2e][split]") {
    const std::string tmp = fresh_temp_path(".3mf");
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), tmp, fs::copy_option::overwrite_if_exists);
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/two_cubes.stl";

    auto ra = spawn_cli({"object", "add", tmp, "--plate", first_plate(tmp),
                         "--stl", stl, "--name", "twin"});
    REQUIRE(ra.exit_code == 0);

    auto rs = spawn_cli({"object", "split-to-parts", tmp, "--name", "twin", "--output", out});
    REQUIRE(rs.exit_code == 0);
    REQUIRE(fs::exists(out));

    fs::remove(tmp);
    fs::remove(out);
}
