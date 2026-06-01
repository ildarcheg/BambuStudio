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

// ============================================================
// Task 25: manifest-validation E2E tests
// ============================================================

TEST_CASE("project apply: missing manifest file yields exit 2",
          "[project_apply][e2e][manifest]") {
    const std::string in  = fresh_temp_path("_apply_nomf_in.3mf");
    const std::string out = fresh_temp_path("_apply_nomf_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_option::overwrite_if_exists);

    // Point at a path that does not exist.
    std::string mf = fresh_temp_path("_apply_nomf_NONEXISTENT.json");

    auto r = spawn_cli({"--json", "project", "apply", in,
                        "--manifest", mf, "--output", out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 2);
    REQUIRE(r.stderr_text.find("file_not_found") != std::string::npos);
    // Output must not have been created since we never reached save.
    REQUIRE_FALSE(fs::exists(out));

    fs::remove(in);
}

TEST_CASE("project apply: bad JSON in manifest yields exit 3",
          "[project_apply][e2e][manifest]") {
    const std::string in  = fresh_temp_path("_apply_badjson_in.3mf");
    const std::string out = fresh_temp_path("_apply_badjson_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_option::overwrite_if_exists);

    std::string mfdir = fresh_temp_path("_apply_badjson_d");
    fs::create_directories(mfdir);
    std::string mf = write_manifest(mfdir, "{ this is not valid JSON }");

    auto r = spawn_cli({"--json", "project", "apply", in,
                        "--manifest", mf, "--output", out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 3);
    REQUIRE(r.stderr_text.find("parse_failure") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));

    fs::remove(in);
    fs::remove_all(mfdir);
}

TEST_CASE("project apply: wrong manifest version yields exit 1",
          "[project_apply][e2e][manifest]") {
    const std::string in  = fresh_temp_path("_apply_badver_in.3mf");
    const std::string out = fresh_temp_path("_apply_badver_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_option::overwrite_if_exists);

    std::string mfdir = fresh_temp_path("_apply_badver_d");
    fs::create_directories(mfdir);
    // version 99 is not supported.
    std::string mf = write_manifest(mfdir,
        R"({"version":99,"operations":[]})");

    auto r = spawn_cli({"--json", "project", "apply", in,
                        "--manifest", mf, "--output", out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));

    fs::remove(in);
    fs::remove_all(mfdir);
}

TEST_CASE("project apply: unknown op in manifest yields exit 1",
          "[project_apply][e2e][manifest]") {
    const std::string in  = fresh_temp_path("_apply_badop_in.3mf");
    const std::string out = fresh_temp_path("_apply_badop_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_option::overwrite_if_exists);

    std::string mfdir = fresh_temp_path("_apply_badop_d");
    fs::create_directories(mfdir);
    // "banana.split" is not a registered op.
    std::string mf = write_manifest(mfdir,
        R"({"version":1,"operations":[{"op":"banana.split"}]})");

    auto r = spawn_cli({"--json", "project", "apply", in,
                        "--manifest", mf, "--output", out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));

    fs::remove(in);
    fs::remove_all(mfdir);
}

// ============================================================
// Task 26: schema-vs-semantic exit-7 E2E tests
//
// For each exit-7 verb, a ManifestFieldError (schema typo) must route to
// exit 1 (usage_error), NOT exit 7.  The per-op invalid_argument /
// runtime_error override fires ONLY for genuine semantic failures.
// ============================================================

TEST_CASE("project apply: split-to-parts unknown field -> exit 1",
          "[project_apply][e2e][exit7]") {
    const std::string in    = fresh_temp_path("_apply_split_typo.3mf");
    const std::string mfdir = fresh_temp_path("_apply_split_typo_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_option::overwrite_if_exists);
    std::string mf = write_manifest(mfdir,
        R"({"version":1,"operations":[{"op":"object.split-to-parts","name":"AnyName","junk":1}]})");
    auto r = spawn_cli({"project", "apply", in, "--manifest", mf});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 1);   // ManifestFieldError -> exit 1, NOT exit 7
    REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);
    fs::remove(in);
    fs::remove_all(mfdir);
}

TEST_CASE("project apply: merge-parts unknown field -> exit 1",
          "[project_apply][e2e][exit7]") {
    const std::string in    = fresh_temp_path("_apply_merge_typo.3mf");
    const std::string mfdir = fresh_temp_path("_apply_merge_typo_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_option::overwrite_if_exists);
    std::string mf = write_manifest(mfdir,
        R"({"version":1,"operations":[{"op":"object.merge-parts","name":"AnyName",)"
        R"("parts":["a","b"],"into":"X","junk":1}]})");
    auto r = spawn_cli({"project", "apply", in, "--manifest", mf});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);
    fs::remove(in);
    fs::remove_all(mfdir);
}

TEST_CASE("project apply: object.auto-orient unknown field -> exit 1",
          "[project_apply][e2e][exit7]") {
    const std::string in    = fresh_temp_path("_apply_oao_typo.3mf");
    const std::string mfdir = fresh_temp_path("_apply_oao_typo_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_option::overwrite_if_exists);
    std::string mf = write_manifest(mfdir,
        R"({"version":1,"operations":[{"op":"object.auto-orient","name":"AnyName","junk":1}]})");
    auto r = spawn_cli({"project", "apply", in, "--manifest", mf});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);
    fs::remove(in);
    fs::remove_all(mfdir);
}

TEST_CASE("project apply: plate.auto-orient unknown field -> exit 1",
          "[project_apply][e2e][exit7]") {
    const std::string in    = fresh_temp_path("_apply_pao_typo.3mf");
    const std::string mfdir = fresh_temp_path("_apply_pao_typo_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_option::overwrite_if_exists);
    std::string mf = write_manifest(mfdir,
        R"({"version":1,"operations":[{"op":"plate.auto-orient","plate":"Plate 01 test","junk":1}]})");
    auto r = spawn_cli({"project", "apply", in, "--manifest", mf});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);
    fs::remove(in);
    fs::remove_all(mfdir);
}

// Semantic trigger — split-to-parts on a single-component cube.
// Adds cube.stl via object.add in a first apply invocation, then attempts
// split-to-parts in a second; single-component throws std::invalid_argument
// which the override map remaps to exit 7 (invalid_state).
TEST_CASE("project apply: split-to-parts on single-component mesh -> exit 7",
          "[project_apply][e2e][exit7]") {
    const std::string in    = fresh_temp_path("_apply_split_sem.3mf");
    const std::string mfdir = fresh_temp_path("_apply_split_sem_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_option::overwrite_if_exists);

    // Step 1: add cube.stl as a named object via project apply so we have a
    // known single-component target on the plate.
    {
        const std::string cube_stl = canonical_committed_stl_dir() + "/cube.stl";
        REQUIRE(fs::exists(cube_stl));
        // Build the manifest body with a forward-slash-normalised STL path
        // embedded as an absolute path so g_manifest_dir resolution is bypassed.
        std::string stl_escaped = cube_stl;
        // On Windows backslashes inside JSON strings must be doubled.
        std::string stl_json;
        for (char c : stl_escaped)
            stl_json += (c == '\\') ? std::string("\\\\") : std::string(1, c);

        std::string body = std::string("{\"version\":1,\"operations\":[{\"op\":\"object.add\",")
            + "\"plate\":\"Plate 01 test\","
            + "\"stl\":\"" + stl_json + "\","
            + "\"name\":\"split_target\"}]}";
        std::string mf_setup = write_manifest(mfdir, body);
        auto setup_r = spawn_cli({"project", "apply", in, "--manifest", mf_setup});
        INFO("setup stderr: " << setup_r.stderr_text);
        REQUIRE(setup_r.exit_code == 0);
    }

    // Step 2: attempt to split — single-component cube throws
    // std::invalid_argument inside split_object_to_parts; the override entry
    // for object.split-to-parts remaps that to exit 7 / invalid_state.
    {
        std::string mf = write_manifest(mfdir,
            R"({"version":1,"operations":[{"op":"object.split-to-parts","name":"split_target"}]})");
        auto r = spawn_cli({"project", "apply", in, "--manifest", mf});
        INFO("split stderr: " << r.stderr_text);
        REQUIRE(r.exit_code == 7);
        REQUIRE(r.stderr_text.find("invalid_state") != std::string::npos);
    }

    fs::remove(in);
    fs::remove_all(mfdir);
}
