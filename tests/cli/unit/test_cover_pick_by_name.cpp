#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_tab_ops.hpp"
#include "exceptions.hpp"

#include "libslic3r/Model.hpp"

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
using bambu_cli::ProjectState;

static const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";

TEST_CASE("--cover-name: selects existing image in Model Pictures",
          "[unit][cover_name]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::AuxAddParams ap;
    ap.folder    = bambu_cli::AuxFolder::ModelPictures;
    ap.file_path = kPng;
    bambu_cli::aux_add(s, ap);

    bambu_cli::InfoSetParams ip;
    ip.cover_name = "cover_smoke.png";
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));
    REQUIRE(s.model.model_info->cover_file == "cover_smoke.png");
}

TEST_CASE("--cover-name: throws std::out_of_range when name not present in folder",
          "[unit][cover_name]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_name = "absent.png";
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), std::out_of_range);
}

TEST_CASE("--cover-name: profile_set targets Profile Pictures",
          "[unit][cover_name]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::AuxAddParams ap;
    ap.folder    = bambu_cli::AuxFolder::ProfilePictures;
    ap.file_path = kPng;
    bambu_cli::aux_add(s, ap);

    bambu_cli::ProfileSetParams pp;
    pp.cover_name = "cover_smoke.png";
    REQUIRE_NOTHROW(bambu_cli::profile_set(s, pp));
    REQUIRE(s.model.profile_info->ProfileCover == "cover_smoke.png");
}
