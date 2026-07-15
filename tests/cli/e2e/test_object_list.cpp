// Dedicated e2e coverage for `object list` as a first-class verb (audit
// test-coverage gap: it was previously exercised only as an assertion
// vehicle inside other tests). These pin existing behavior.
#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

static std::string first_plate(const std::string& path) {
    auto r = spawn_cli({"--json", "plate", "list", path});
    auto p = r.stdout_text.find("\"plates\":[\"");
    if (p == std::string::npos) return "";
    p += std::string("\"plates\":[\"").size();
    return r.stdout_text.substr(p, r.stdout_text.find("\"", p) - p);
}

TEST_CASE("object list: empty project -> exit 0, object_count 0",
          "[e2e][object_list]") {
    auto r = spawn_cli({"--json", "object", "list", canonical_committed_3mf()});
    INFO("stdout: " << r.stdout_text);
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_text.find("\"object_count\":0") != std::string::npos);
    REQUIRE(r.stdout_text.find("\"objects\":[]") != std::string::npos);
}

TEST_CASE("object list: enumerates objects, honors --plate filter",
          "[e2e][object_list]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate(out);

    REQUIRE(spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl,
                       "--name", "AlphaObj"}).exit_code == 0);
    REQUIRE(spawn_cli({"plate", "add", out, "--name", "ListPlate2"}).exit_code == 0);
    REQUIRE(spawn_cli({"object", "add", out, "--plate", "ListPlate2",
                       "--stl", stl, "--name", "BetaObj"}).exit_code == 0);

    SECTION("unfiltered list sees both objects") {
        auto r = spawn_cli({"--json", "object", "list", out});
        INFO("stdout: " << r.stdout_text);
        REQUIRE(r.exit_code == 0);
        REQUIRE(r.stdout_text.find("\"object_count\":2") != std::string::npos);
        REQUIRE(r.stdout_text.find("AlphaObj") != std::string::npos);
        REQUIRE(r.stdout_text.find("BetaObj")  != std::string::npos);
    }
    SECTION("--plate filter narrows to that plate's objects") {
        auto r = spawn_cli({"--json", "object", "list", out,
                            "--plate", "ListPlate2"});
        INFO("stdout: " << r.stdout_text);
        REQUIRE(r.exit_code == 0);
        REQUIRE(r.stdout_text.find("\"object_count\":1") != std::string::npos);
        REQUIRE(r.stdout_text.find("BetaObj")  != std::string::npos);
        REQUIRE(r.stdout_text.find("AlphaObj") == std::string::npos);
    }
    SECTION("unknown plate filter -> empty result, exit 0") {
        auto r = spawn_cli({"--json", "object", "list", out,
                            "--plate", "NoSuchPlate"});
        REQUIRE(r.exit_code == 0);
        REQUIRE(r.stdout_text.find("\"object_count\":0") != std::string::npos);
    }
    SECTION("text mode exits 0") {
        auto r = spawn_cli({"object", "list", out});
        REQUIRE(r.exit_code == 0);
        REQUIRE(r.stdout_text.find("AlphaObj") != std::string::npos);
    }

    fs::remove(out);
}
