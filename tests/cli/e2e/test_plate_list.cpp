#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

TEST_CASE("plate list: returns at least one plate for the reference 3mf", "[m3][plate_list]") {
    auto r = spawn_cli({"plate", "list", canonical_committed_3mf()});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    // Text output: one plate per line. Reference has 1 plate.
    REQUIRE_FALSE(r.stdout_text.empty());
}

TEST_CASE("plate list: after add, lists both plates", "[m3][plate_list]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    REQUIRE(spawn_cli({"plate", "add", out, "--name", "second"}).exit_code == 0);

    auto r = spawn_cli({"--json", "plate", "list", out});
    REQUIRE(r.exit_code == 0);
    // Either plate_count=2 or "second" must appear in JSON output.
    bool has_count = r.stdout_text.find("\"plate_count\":2") != std::string::npos;
    bool has_name  = r.stdout_text.find("second")           != std::string::npos;
    REQUIRE((has_count || has_name));

    fs::remove(out);
}
