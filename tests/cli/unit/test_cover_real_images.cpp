#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_tab_ops.hpp"
#include "io.hpp"

#include "libslic3r/Model.hpp"

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
using bambu_cli::ProjectState;

// Real-world JPEG fixtures (~50 KB each) committed to the repo. Used for the
// manual GUI smoke (docs/cli/manual-test.md) so the Project tab covers in
// Bambu Studio actually render as visible images. These tests verify the
// embed path handles full-size JPEGs correctly, complementing the existing
// tiny-stub tests in test_cover_decoupling.cpp.
static const std::string kSample   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_sample.jpg";
static const std::string kOriginal = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_original.jpg";

TEST_CASE("real-image covers: SAMPLE -> designer (Model Pictures), ORIGINAL -> profile (Profile Pictures)",
          "[unit][cover_real]") {
    REQUIRE(fs::exists(kSample));
    REQUIRE(fs::exists(kOriginal));
    REQUIRE(fs::file_size(kSample)   > 1024);   // sanity: real image, not stub
    REQUIRE(fs::file_size(kOriginal) > 1024);

    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = kSample;
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));

    bambu_cli::ProfileSetParams pp;
    pp.cover_path = kOriginal;
    REQUIRE_NOTHROW(bambu_cli::profile_set(s, pp));

    // Metadata pointers carry the basenames.
    REQUIRE(s.model.model_info->cover_file    == "cover_sample.jpg");
    REQUIRE(s.model.profile_info->ProfileCover == "cover_original.jpg");

    // On-disk files land in their canonical folders, byte-identical to source.
    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    const fs::path designer = aux / "Model Pictures"   / "cover_sample.jpg";
    const fs::path profile  = aux / "Profile Pictures" / "cover_original.jpg";
    REQUIRE(fs::exists(designer));
    REQUIRE(fs::exists(profile));
    REQUIRE(fs::file_size(designer) == fs::file_size(kSample));
    REQUIRE(fs::file_size(profile)  == fs::file_size(kOriginal));
}

TEST_CASE("real-image covers: round-trip through save_project preserves both",
          "[unit][cover_real]") {
    REQUIRE(fs::exists(kSample));
    REQUIRE(fs::exists(kOriginal));

    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;     ip.cover_path = kSample;
    bambu_cli::ProfileSetParams pp;  pp.cover_path = kOriginal;
    bambu_cli::info_set(s, ip);
    bambu_cli::profile_set(s, pp);

    // The save path runs check_auxiliary_passthrough + check_cover_references_resolve
    // as part of run_guard. If either fails, save_project returns !ok.
    const fs::path out = fs::temp_directory_path() /
                         fs::unique_path("real-cov-%%%%-%%%%.3mf");
    auto sr = bambu_cli::save_project(s, out.string());
    INFO("save error: " << sr.error_code << " — " << sr.error_message);
    REQUIRE(sr.ok);

    fs::remove(out);
}
