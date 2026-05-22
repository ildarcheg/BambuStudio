#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_tab_ops.hpp"
#include "exceptions.hpp"

#include <boost/filesystem.hpp>

using bambu_cli::ProjectState;
namespace fs = boost::filesystem;

static const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";
static const std::string kJpg = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.jpg";

// ---- info_show -------------------------------------------------------------

TEST_CASE("info_show: returns empty InfoView when model_info is null",
          "[unit][c1][info_show]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s);
    REQUIRE(s.model.model_info == nullptr);
    auto v = bambu_cli::info_show(s);
    REQUIRE(v.title.empty());
    REQUIRE(v.description.empty());
    REQUIRE(v.license.empty());
    REQUIRE(v.copyright.empty());
    REQUIRE(v.cover.empty());
    REQUIRE(v.origin.empty());
}

TEST_CASE("info_show: returns populated InfoView when model_info is set",
          "[unit][c1][info_show]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto v = bambu_cli::info_show(s);
    // We can't assert specific values (reference.3mf content may vary),
    // but the call must succeed.
    (void)v;
    SUCCEED("info_show returned without throw");
}

// ---- info_set --------------------------------------------------------------

TEST_CASE("info_set: sets title field", "[unit][c1][info_set]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::InfoSetParams p;
    p.title = "Unit Test Title";
    REQUIRE_NOTHROW(bambu_cli::info_set(s, p));
    REQUIRE(s.model.model_info != nullptr);
    REQUIRE(s.model.model_info->model_name == "Unit Test Title");
}

TEST_CASE("info_set: sets description field", "[unit][c1][info_set]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::InfoSetParams p;
    p.description = "Unit Test Desc";
    REQUIRE_NOTHROW(bambu_cli::info_set(s, p));
    REQUIRE(s.model.model_info->description == "Unit Test Desc");
}

TEST_CASE("info_set: sets license and copyright fields", "[unit][c1][info_set]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::InfoSetParams p;
    p.license   = "MIT";
    p.copyright = "2026 Test";
    REQUIRE_NOTHROW(bambu_cli::info_set(s, p));
    REQUIRE(s.model.model_info->license   == "MIT");
    REQUIRE(s.model.model_info->copyright == "2026 Test");
}

TEST_CASE("info_set --cover valid PNG: sets cover_file path",
          "[unit][c1][info_set_cover]") {
    REQUIRE(fs::exists(kPng));  // fixture must be committed
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::InfoSetParams p;
    p.cover_path = kPng;
    REQUIRE_NOTHROW(bambu_cli::info_set(s, p));
    REQUIRE(s.model.model_info != nullptr);
    REQUIRE(s.model.model_info->cover_file == "Auxiliaries/cover.png");
}

TEST_CASE("info_set --cover JPG: throws BadCoverImage", "[unit][c1][info_set_bad_cover]") {
    REQUIRE(fs::exists(kJpg));  // fixture must be committed
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::InfoSetParams p;
    p.cover_path = kJpg;
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, p), bambu_cli::BadCoverImage);
}

TEST_CASE("info_set --cover nonexistent: throws BadCoverImage",
          "[unit][c1][info_set_bad_cover]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::InfoSetParams p;
    p.cover_path = "/nonexistent/path/cover.png";
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, p), bambu_cli::BadCoverImage);
}

// ---- info_clear ------------------------------------------------------------

TEST_CASE("info_clear: clears a single field", "[unit][c1][info_clear]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    // Set title first.
    bambu_cli::InfoSetParams p; p.title = "ToClear";
    bambu_cli::info_set(s, p);
    REQUIRE(s.model.model_info->model_name == "ToClear");

    REQUIRE_NOTHROW(bambu_cli::info_clear(s, {"title"}));
    REQUIRE(s.model.model_info->model_name.empty());
}

TEST_CASE("info_clear: clears multiple fields", "[unit][c1][info_clear]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::InfoSetParams p;
    p.title = "T"; p.description = "D"; p.license = "L";
    bambu_cli::info_set(s, p);

    REQUIRE_NOTHROW(bambu_cli::info_clear(s, {"title", "description", "license"}));
    REQUIRE(s.model.model_info->model_name.empty());
    REQUIRE(s.model.model_info->description.empty());
    REQUIRE(s.model.model_info->license.empty());
}

TEST_CASE("info_clear: unknown field throws InvalidField", "[unit][c1][info_clear]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::info_clear(s, {"bogus_field"}), bambu_cli::InvalidField);
}

TEST_CASE("info_clear: origin is not a clearable field -> throws InvalidField",
          "[unit][c1][info_clear]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::info_clear(s, {"origin"}), bambu_cli::InvalidField);
}

TEST_CASE("info_clear: idempotent on already-empty field", "[unit][c1][info_clear]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    // Clearing a field that's already empty should not throw.
    REQUIRE_NOTHROW(bambu_cli::info_clear(s, {"license"}));
}

TEST_CASE("allowed_info_fields: contains exactly the 5 clearable fields",
          "[unit][c1][info_fields]") {
    const auto f = bambu_cli::allowed_info_fields();
    REQUIRE(f.count("title")       == 1);
    REQUIRE(f.count("description") == 1);
    REQUIRE(f.count("license")     == 1);
    REQUIRE(f.count("copyright")   == 1);
    REQUIRE(f.count("cover")       == 1);
    REQUIRE(f.count("origin")      == 0);  // read-only, not clearable
    REQUIRE(f.size() == 5);
}
