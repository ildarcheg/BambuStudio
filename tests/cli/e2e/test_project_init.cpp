#include "test_helpers.hpp"
#include "archive_invariants.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

#include <miniz.h>
#include <cstring>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

TEST_CASE("project init: happy path clones committed reference 3mf", "[m1][project_init]") {
    const std::string out = fresh_temp_path(".3mf");
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));

    auto r = spawn_cli({"project", "init", out, "--template", ref});
    INFO("stderr: " << r.stderr_text);
    INFO("stdout: " << r.stdout_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(fs::exists(out));

    bambu_cli_test::run_all_basic(out);

    fs::remove(out);
}

TEST_CASE("project init: missing template -> exit 2 file_not_found", "[m1][project_init]") {
    const std::string out = fresh_temp_path(".3mf");
    auto r = spawn_cli({"project", "init", out, "--template", "Z:/does/not/exist.3mf"});
    REQUIRE(r.exit_code == 2);
    REQUIRE(r.stderr_text.find("file_not_found") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));
}

TEST_CASE("project init: corrupted template (missing plate_1_small.png) -> exit 8", "[m1][project_init][guard]") {
    const std::string corrupted = fresh_temp_path("_corrupted.3mf");
    const std::string out       = fresh_temp_path(".3mf");
    const std::string ref       = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));

    // Copy ref to corrupted, then remove plate_1_small.png entry via miniz rewrite.
    fs::copy_file(ref, corrupted, fs::copy_options::overwrite_existing);
    {
        mz_zip_archive in;  std::memset(&in,  0, sizeof(in));
        mz_zip_archive out_z; std::memset(&out_z, 0, sizeof(out_z));
        REQUIRE(mz_zip_reader_init_file(&in, corrupted.c_str(), 0));
        std::string tmp = corrupted + ".tmp";
        REQUIRE(mz_zip_writer_init_file(&out_z, tmp.c_str(), 0));
        mz_uint n = mz_zip_reader_get_num_files(&in);
        for (mz_uint i = 0; i < n; ++i) {
            char name[1024]; mz_zip_reader_get_filename(&in, i, name, sizeof(name));
            if (std::string(name) == "Metadata/plate_1_small.png") continue;
            mz_zip_writer_add_from_zip_reader(&out_z, &in, i);
        }
        mz_zip_writer_finalize_archive(&out_z);
        mz_zip_writer_end(&out_z);
        mz_zip_reader_end(&in);
        fs::remove(corrupted);
        fs::rename(tmp, corrupted);
    }

    auto r = spawn_cli({"project", "init", out, "--template", corrupted});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 8);
    REQUIRE(r.stderr_text.find("invariant_violation") != std::string::npos);
    REQUIRE(r.stderr_text.find("thumbnails") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));

    fs::remove(corrupted);
}

TEST_CASE("save_project: re-init over existing destination round-trips",
          "[e2e][m2_baksave]") {
    const fs::path out = fs::temp_directory_path() / "m2_baksave.3mf";
    fs::remove(out);
    fs::remove(out.string() + ".bak");
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));

    auto r1 = spawn_cli({"project", "init", out.string(), "--template", ref});
    INFO("stderr1: " << r1.stderr_text);
    INFO("stdout1: " << r1.stdout_text);
    REQUIRE(r1.exit_code == 0);
    REQUIRE(fs::exists(out));

    // Re-init over the existing destination -- exercises the .bak swap path.
    auto r2 = spawn_cli({"project", "init", out.string(), "--template", ref});
    INFO("stderr2: " << r2.stderr_text);
    INFO("stdout2: " << r2.stdout_text);
    REQUIRE(r2.exit_code == 0);
    REQUIRE(fs::exists(out));
    REQUIRE_FALSE(fs::exists(out.string() + ".bak"));   // cleaned up

    // Validate that the produced archive still passes archive invariants.
    bambu_cli_test::run_all_basic(out.string());

    fs::remove(out);
}
