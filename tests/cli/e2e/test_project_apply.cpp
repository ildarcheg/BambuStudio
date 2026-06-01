#include "test_helpers.hpp"
#include "archive_invariants.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <fstream>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

namespace {
std::string write_manifest(const std::string& dir, const std::string& body) {
    std::string path = dir + "/m.json";
    std::ofstream(path) << body;
    return path;
}
} // namespace

TEST_CASE("project apply --dry-run: happy path skips save",
          "[project_apply][e2e][dry-run]") {
    const std::string in       = fresh_temp_path("_apply_dryhappy_in.3mf");
    const std::string out      = fresh_temp_path("_apply_dryhappy_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_option::overwrite_if_exists);

    std::string mfdir = fresh_temp_path("_apply_dryhappy_d");
    fs::create_directories(mfdir);
    std::string mf = write_manifest(mfdir,
        R"({"version":1,"operations":[{"op":"plate.add","name":"new1"},)"
        R"({"op":"plate.add","name":"new2"}]})");

    auto r = spawn_cli({"--json", "project", "apply", in,
                        "--manifest", mf, "--output", out, "--dry-run"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_text.find("\"dry_run\":true") != std::string::npos);
    // Output file must NOT have been created.
    REQUIRE_FALSE(fs::exists(out));

    fs::remove(in);
    fs::remove_all(mfdir);
}

TEST_CASE("project apply --dry-run: failing path skips save, propagates exit code",
          "[project_apply][e2e][dry-run]") {
    const std::string in  = fresh_temp_path("_apply_dryfail_in.3mf");
    const std::string out = fresh_temp_path("_apply_dryfail_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_option::overwrite_if_exists);

    std::string mfdir = fresh_temp_path("_apply_dryfail_d");
    fs::create_directories(mfdir);
    // step 2 attempts to rename a plate that doesn't exist -> exit 6
    std::string mf = write_manifest(mfdir,
        R"({"version":1,"operations":[)"
        R"({"op":"plate.add","name":"P-new"},)"
        R"({"op":"plate.rename","from":"NOPE","to":"X"}]})");

    auto r = spawn_cli({"--json", "project", "apply", in,
                        "--manifest", mf, "--output", out, "--dry-run"});
    REQUIRE(r.exit_code == 6);
    REQUIRE(r.stderr_text.find("\"step\":2") != std::string::npos);
    REQUIRE(r.stderr_text.find("\"op\":\"plate.rename\"") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));

    fs::remove(in);
    fs::remove_all(mfdir);
}
