#include "test_helpers.hpp"
#include "archive_invariants.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

TEST_CASE("object auto-orient: succeeds with --name", "[e2e][object_auto_orient]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    // Add a cube first (so the project has at least one named object).
    auto add = spawn_cli({"object", "add", out, "--plate", "Plate 01 test",
                          "--stl", std::string(BAMBU_CLI_FIXTURE_STL_DIR)
                          + "/cube.stl", "--name", "AOCube"});
    INFO("add stderr: " << add.stderr_text);
    REQUIRE(add.exit_code == 0);

    auto r = spawn_cli({"object", "auto-orient", out, "--name", "AOCube"});
    INFO("orient stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    bambu_cli_test::run_all_basic(out);
    fs::remove(out);
}

TEST_CASE("object auto-orient: unknown name -> exit 6",
          "[e2e][object_auto_orient]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    auto r = spawn_cli({"object", "auto-orient", out, "--name", "Missing"});
    REQUIRE(r.exit_code == 6);
    REQUIRE(r.stderr_text.find("unknown_reference") != std::string::npos);
    fs::remove(out);
}
