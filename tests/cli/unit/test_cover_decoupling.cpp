#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_tab_ops.hpp"
#include "io.hpp"

#include "libslic3r/Model.hpp"

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
using bambu_cli::ProjectState;

static const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";
static const std::string kJpg = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.jpg";

TEST_CASE("cover decoupling: designer cover lands in Model Pictures, profile in Profile Pictures",
          "[unit][cover_decouple]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = kPng;
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));

    bambu_cli::ProfileSetParams pp;
    pp.cover_path = kJpg;
    REQUIRE_NOTHROW(bambu_cli::profile_set(s, pp));

    REQUIRE(s.model.model_info);
    REQUIRE(s.model.profile_info);
    REQUIRE(s.model.model_info->cover_file == fs::path(kPng).filename().string());
    REQUIRE(s.model.profile_info->ProfileCover == fs::path(kJpg).filename().string());

    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    REQUIRE(fs::exists(aux / "Model Pictures"   / fs::path(kPng).filename()));
    REQUIRE(fs::exists(aux / "Profile Pictures" / fs::path(kJpg).filename()));

    REQUIRE_FALSE(fs::exists(aux / "Model Pictures"   / "cover.png"));
    REQUIRE_FALSE(fs::exists(aux / "Profile Pictures" / "cover.png"));
}

TEST_CASE("cover decoupling: info clear cover leaves profile cover intact",
          "[unit][cover_decouple]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;     ip.cover_path = kPng;
    bambu_cli::ProfileSetParams pp;  pp.cover_path = kJpg;
    bambu_cli::info_set(s, ip);
    bambu_cli::profile_set(s, pp);

    bambu_cli::info_clear(s, {"cover"});

    REQUIRE(s.model.model_info->cover_file.empty());
    REQUIRE(s.model.profile_info->ProfileCover == fs::path(kJpg).filename().string());

    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    REQUIRE(fs::exists(aux / "Profile Pictures" / fs::path(kJpg).filename()));
}

TEST_CASE("cover decoupling: profile clear cover leaves designer cover intact",
          "[unit][cover_decouple]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;     ip.cover_path = kPng;
    bambu_cli::ProfileSetParams pp;  pp.cover_path = kJpg;
    bambu_cli::info_set(s, ip);
    bambu_cli::profile_set(s, pp);

    bambu_cli::profile_clear(s, {"cover"});

    REQUIRE(s.model.profile_info->ProfileCover.empty());
    REQUIRE(s.model.model_info->cover_file == fs::path(kPng).filename().string());

    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    REQUIRE(fs::exists(aux / "Model Pictures" / fs::path(kPng).filename()));
}

TEST_CASE("cover decoupling: JPEG cover accepted (was PNG-only)",
          "[unit][cover_decouple]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = kJpg;
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));
    REQUIRE(s.model.model_info->cover_file == fs::path(kJpg).filename().string());
}
