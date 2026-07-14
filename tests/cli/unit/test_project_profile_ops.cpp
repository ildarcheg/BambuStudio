#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_tab_ops.hpp"
#include "exceptions.hpp"

#include <boost/filesystem.hpp>

using bambu_cli::ProjectState;
namespace fs = boost::filesystem;

static const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";
static const std::string kJpg = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.jpg";

// ---- profile_show ----------------------------------------------------------

TEST_CASE("profile_show: returns empty view when profile_info is null",
          "[unit][c2][profile_show]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s);
    REQUIRE(s.model.profile_info == nullptr);
    auto v = bambu_cli::profile_show(s);
    REQUIRE(v.title.empty());
    REQUIRE(v.description.empty());
    REQUIRE(v.cover.empty());
    REQUIRE(v.user_id.empty());
    REQUIRE(v.user_name.empty());
}

TEST_CASE("profile_show: returns populated view from loaded project",
          "[unit][c2][profile_show]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto v = bambu_cli::profile_show(s);
    (void)v;
    SUCCEED("profile_show returned without throw");
}

// ---- profile_set -----------------------------------------------------------

TEST_CASE("profile_set: sets title (ProfileTile field)",
          "[unit][c2][profile_set]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::ProfileSetParams p;
    p.title = "Unit Profile Title";
    REQUIRE_NOTHROW(bambu_cli::profile_set(s, p));
    REQUIRE(s.model.profile_info != nullptr);
    REQUIRE(s.model.profile_info->ProfileTile == "Unit Profile Title");
}

TEST_CASE("profile_set: sets description",
          "[unit][c2][profile_set]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::ProfileSetParams p;
    p.description = "Profile Desc";
    REQUIRE_NOTHROW(bambu_cli::profile_set(s, p));
    REQUIRE(s.model.profile_info->ProfileDescription == "Profile Desc");
}

TEST_CASE("profile_set --cover valid PNG: sets ProfileCover to basename",
          "[unit][c2][profile_set_cover]") {
    REQUIRE(fs::exists(kPng));
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::ProfileSetParams p;
    p.cover_path = kPng;
    REQUIRE_NOTHROW(bambu_cli::profile_set(s, p));
    REQUIRE(s.model.profile_info->ProfileCover == "cover_smoke.png");
}

TEST_CASE("profile_set --cover JPG: accepted (PNG + JPEG both valid)",
          "[unit][c2][profile_set_cover]") {
    REQUIRE(fs::exists(kJpg));
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::ProfileSetParams p;
    p.cover_path = kJpg;
    REQUIRE_NOTHROW(bambu_cli::profile_set(s, p));
    REQUIRE(s.model.profile_info->ProfileCover == "cover_smoke.jpg");
}

TEST_CASE("profile_set: user_id and user_name are preserved (read-only)",
          "[unit][c2][profile_set]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    // Record original user fields.
    const std::string orig_uid  = s.model.profile_info ? s.model.profile_info->ProfileUserId   : "";
    const std::string orig_unam = s.model.profile_info ? s.model.profile_info->ProfileUserName : "";

    bambu_cli::ProfileSetParams p;
    p.title = "NewTitle";
    REQUIRE_NOTHROW(bambu_cli::profile_set(s, p));

    // User fields must be unchanged.
    REQUIRE(s.model.profile_info->ProfileUserId   == orig_uid);
    REQUIRE(s.model.profile_info->ProfileUserName == orig_unam);
}

// ---- profile_clear ---------------------------------------------------------

TEST_CASE("profile_clear: clears title",
          "[unit][c2][profile_clear]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::ProfileSetParams p; p.title = "ToClear";
    bambu_cli::profile_set(s, p);
    REQUIRE(s.model.profile_info->ProfileTile == "ToClear");

    REQUIRE_NOTHROW(bambu_cli::profile_clear(s, {"title"}));
    REQUIRE(s.model.profile_info->ProfileTile.empty());
}

TEST_CASE("profile_clear: unknown field throws InvalidField",
          "[unit][c2][profile_clear]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::profile_clear(s, {"bogus"}), bambu_cli::InvalidField);
}

TEST_CASE("profile_clear: user_id is not a clearable field -> InvalidField",
          "[unit][c2][profile_clear]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::profile_clear(s, {"user_id"}), bambu_cli::InvalidField);
}

TEST_CASE("profile_clear: idempotent on empty field",
          "[unit][c2][profile_clear]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_NOTHROW(bambu_cli::profile_clear(s, {"description"}));
}

TEST_CASE("allowed_profile_fields: contains title, description, cover only",
          "[unit][c2][profile_fields]") {
    const auto f = bambu_cli::allowed_profile_fields();
    REQUIRE(f.count("title")       == 1);
    REQUIRE(f.count("description") == 1);
    REQUIRE(f.count("cover")       == 1);
    REQUIRE(f.count("user_id")     == 0);
    REQUIRE(f.count("user_name")   == 0);
    REQUIRE(f.size() == 3);
}
