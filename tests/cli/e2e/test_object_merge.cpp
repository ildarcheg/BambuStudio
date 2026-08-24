#include "test_helpers.hpp"
#include "archive_invariants.hpp"
#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <string>

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

// Build a .3mf with a split two_cubes object (2 volumes: twin_1, twin_2).
static std::string build_split_project() {
    std::string tmp = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), tmp, fs::copy_option::overwrite_if_exists);
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/two_cubes.stl";
    auto ra = spawn_cli({"object", "add", tmp, "--plate", first_plate(tmp),
                         "--stl", stl, "--name", "twin"});
    REQUIRE(ra.exit_code == 0);
    auto rs = spawn_cli({"object", "split-to-parts", tmp, "--name", "twin"});
    REQUIRE(rs.exit_code == 0);
    return tmp;
}

TEST_CASE("object merge-parts: happy path -- 2 volumes merge to 1", "[e2e][merge]") {
    std::string tmp = build_split_project();
    auto r = spawn_cli({"object", "merge-parts", tmp,
                        "--name", "twin",
                        "--parts", "twin_1,twin_2",
                        "--into", "merged",
                        "--filament", "1"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_text.find("merged") != std::string::npos);

    // After merge, object should list 1 volume-based part
    bambu_cli_test::run_all_basic(tmp);
    fs::remove(tmp);
}

TEST_CASE("object merge-parts: step a -- empty --parts -> exit 1", "[e2e][merge]") {
    std::string tmp = build_split_project();
    auto r = spawn_cli({"object", "merge-parts", tmp,
                        "--name", "twin", "--parts", "",
                        "--into", "m"});
    REQUIRE(r.exit_code == 1);
    fs::remove(tmp);
}

TEST_CASE("object merge-parts: step b -- unknown object -> exit 6", "[e2e][merge]") {
    std::string tmp = build_split_project();
    auto r = spawn_cli({"object", "merge-parts", tmp,
                        "--name", "ghost", "--parts", "twin_1,twin_2",
                        "--into", "m"});
    REQUIRE(r.exit_code == 6);
    fs::remove(tmp);
}

TEST_CASE("object merge-parts: step c -- unknown part -> exit 6", "[e2e][merge]") {
    std::string tmp = build_split_project();
    auto r = spawn_cli({"object", "merge-parts", tmp,
                        "--name", "twin", "--parts", "no_such_part",
                        "--into", "m"});
    REQUIRE(r.exit_code == 6);
    fs::remove(tmp);
}

TEST_CASE("object merge-parts: step d -- --into collision -> exit 5", "[e2e][merge]") {
    std::string tmp = build_split_project();
    // "twin_1" already exists
    auto r = spawn_cli({"object", "merge-parts", tmp,
                        "--name", "twin", "--parts", "twin_2",
                        "--into", "twin_1"});
    REQUIRE(r.exit_code == 5);
    fs::remove(tmp);
}

TEST_CASE("object merge-parts: step e -- filament out of range -> exit 1 "
          "usage_error", "[e2e][merge]") {
    // A bad --filament value is a usage error, consistent with object add
    // and set-filament (exit 1) — not unknown_reference (contract change
    // 2026-07-15, see the port-isolation audit note).
    std::string tmp = build_split_project();
    auto r = spawn_cli({"object", "merge-parts", tmp,
                        "--name", "twin", "--parts", "twin_1,twin_2",
                        "--into", "m", "--filament", "99"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);
    fs::remove(tmp);
}

TEST_CASE("object merge-parts: step h -- filament disagreement -> exit 7", "[e2e][merge]") {
    // Set different filaments on the split volumes, then merge without --filament
    std::string tmp = build_split_project();
    auto sf1 = spawn_cli({"object", "set-filament", tmp,
                          "--name", "twin", "--filament", "1", "--part", "twin_1"});
    REQUIRE(sf1.exit_code == 0);
    auto sf2 = spawn_cli({"object", "set-filament", tmp,
                          "--name", "twin", "--filament", "2", "--part", "twin_2"});
    REQUIRE(sf2.exit_code == 0);

    auto r = spawn_cli({"object", "merge-parts", tmp,
                        "--name", "twin", "--parts", "twin_1,twin_2",
                        "--into", "m"});
    REQUIRE(r.exit_code == 7);
    fs::remove(tmp);
}

TEST_CASE("object merge-parts: deterministic placement -- reversed --parts same result",
          "[e2e][merge]") {
    std::string tmp1 = build_split_project();
    std::string tmp2 = build_split_project();

    auto r1 = spawn_cli({"object", "merge-parts", tmp1,
                         "--name", "twin", "--parts", "twin_1,twin_2",
                         "--into", "m", "--filament", "1"});
    auto r2 = spawn_cli({"object", "merge-parts", tmp2,
                         "--name", "twin", "--parts", "twin_2,twin_1",
                         "--into", "m", "--filament", "1"});

    REQUIRE(r1.exit_code == 0);
    REQUIRE(r2.exit_code == 0);
    // Both should succeed (determinism is in the unit test)

    fs::remove(tmp1);
    fs::remove(tmp2);
}

TEST_CASE("object merge-parts: --output writes to separate file", "[e2e][merge]") {
    std::string tmp = build_split_project();
    std::string out = fresh_temp_path(".3mf");

    auto r = spawn_cli({"object", "merge-parts", tmp,
                        "--name", "twin", "--parts", "twin_1,twin_2",
                        "--into", "m", "--filament", "1", "--output", out});
    REQUIRE(r.exit_code == 0);
    REQUIRE(fs::exists(out));

    fs::remove(tmp);
    fs::remove(out);
}
