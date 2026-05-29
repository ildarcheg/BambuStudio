#include "test_helpers.hpp"
#include "archive_invariants.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

// Canonical reference 3mf names its first plate "Plate 01 test"
// (not "Plate-1" as the plan assumed). Query via
// `bambu-cli plate list <ref>.3mf` if this ever changes.
static const std::string REF_PLATE = "Plate 01 test";

TEST_CASE("plate center: succeeds on existing plate", "[e2e][plate_layout]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    auto r = spawn_cli({"plate", "center", "--plate", REF_PLATE, out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    bambu_cli_test::run_all_basic(out);
    fs::remove(out);
}

TEST_CASE("plate drop-to-bed: succeeds on existing plate",
          "[e2e][plate_layout]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    auto r = spawn_cli({"plate", "drop-to-bed", "--plate", REF_PLATE, out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    fs::remove(out);
}

TEST_CASE("plate arrange: succeeds on existing plate",
          "[e2e][plate_layout]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    auto r = spawn_cli({"plate", "arrange", "--plate", REF_PLATE, out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    fs::remove(out);
}

TEST_CASE("plate auto-orient: succeeds on existing plate",
          "[e2e][plate_layout]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    auto r = spawn_cli({"plate", "auto-orient", "--plate", REF_PLATE, out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    fs::remove(out);
}

TEST_CASE("plate <verb>: unknown plate -> exit 6",
          "[e2e][plate_layout]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    for (const auto& verb : {"center", "drop-to-bed", "arrange", "auto-orient"}) {
        auto r = spawn_cli({"plate", verb, "--plate", "NoSuchPlate", out});
        INFO("verb=" << verb << " stderr: " << r.stderr_text);
        REQUIRE(r.exit_code == 6);
        REQUIRE(r.stderr_text.find("unknown_reference") != std::string::npos);
    }
    fs::remove(out);
}
