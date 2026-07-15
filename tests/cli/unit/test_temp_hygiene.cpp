// Temp-file hygiene: every path handed out by fresh_temp_path is recorded,
// and cleanup_recorded_temp_paths() sweeps them (files and directories).
// An atexit hook runs the same sweep at process end, so temp artifacts
// leaked by a mid-test assertion failure (Catch2 throws past the trailing
// fs::remove calls) no longer accumulate in %TEMP%.
#include <catch2/catch.hpp>
#include "../test_helpers.hpp"

#include <boost/filesystem.hpp>
#include <fstream>

namespace fs = boost::filesystem;
using namespace bambu_cli_test;

TEST_CASE("fresh_temp_path: recorded paths are swept by "
          "cleanup_recorded_temp_paths", "[unit][temp_hygiene]") {
    const std::string f = fresh_temp_path("_hyg.txt");
    const std::string d = fresh_temp_path("_hyg_dir");
    { std::ofstream o(f, std::ios::binary); o << "x"; }
    fs::create_directories(d + "/sub");
    { std::ofstream o(d + "/sub/leaf.txt", std::ios::binary); o << "y"; }
    REQUIRE(fs::exists(f));
    REQUIRE(fs::exists(d));

    cleanup_recorded_temp_paths();

    REQUIRE_FALSE(fs::exists(f));
    REQUIRE_FALSE(fs::exists(d));
}
