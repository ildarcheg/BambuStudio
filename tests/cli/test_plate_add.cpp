#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <regex>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

TEST_CASE("plate add: appends new plate; runtime guard passes", "[m3][plate_add]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    auto r = spawn_cli({"plate", "add", out, "--name", "second-plate"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(fs::exists(out));

    SECTION("archive contains plate_2.png and plate_2_small.png (G3)") {
        REQUIRE_FALSE(read_zip_entry(out, "Metadata/plate_2.png").empty());
        REQUIRE_FALSE(read_zip_entry(out, "Metadata/plate_2_small.png").empty());
    }

    SECTION("inspect reports plate_count = 2") {
        auto ir = spawn_cli({"--json", "inspect", out});
        REQUIRE(ir.exit_code == 0);
        REQUIRE(ir.stdout_text.find("\"plate_count\":2") != std::string::npos);
    }

    fs::remove(out);
}

TEST_CASE("plate add: duplicate name -> exit 5", "[m3][plate_add]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    REQUIRE(spawn_cli({"plate", "add", out, "--name", "p1"}).exit_code == 0);
    auto r = spawn_cli({"plate", "add", out, "--name", "p1"});
    REQUIRE(r.exit_code == 5);
    REQUIRE(r.stderr_text.find("duplicate_name") != std::string::npos);

    fs::remove(out);
}
