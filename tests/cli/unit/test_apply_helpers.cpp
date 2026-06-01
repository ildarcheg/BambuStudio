#include "apply_helpers.hpp"
#include "exceptions.hpp"

#include <catch2/catch.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;
using bambu_cli::ManifestFieldError;
using bambu_cli::require_only;

TEST_CASE("require_only: accepts when all keys known", "[apply_helpers][require_only]") {
    json step = {{"op", "plate.add"}, {"name", "P1"}};
    REQUIRE_NOTHROW(require_only(step, {"op", "name"}));
}

TEST_CASE("require_only: rejects unknown field", "[apply_helpers][require_only]") {
    json step = {{"op", "plate.add"}, {"name", "P1"}, {"filement", 2}};
    REQUIRE_THROWS_AS(require_only(step, {"op", "name"}), ManifestFieldError);
}

TEST_CASE("require_only: error names the offending field", "[apply_helpers][require_only]") {
    json step = {{"op", "plate.add"}, {"filement", 2}};
    try {
        require_only(step, {"op", "name"});
        FAIL("expected ManifestFieldError");
    } catch (const ManifestFieldError& e) {
        std::string what = e.what();
        REQUIRE(what.find("filement") != std::string::npos);
    }
}

TEST_CASE("require_only: empty step accepted regardless of known list",
          "[apply_helpers][require_only]") {
    json step = json::object();
    REQUIRE_NOTHROW(require_only(step, {"op", "name"}));
}

using bambu_cli::parse_filament;

TEST_CASE("parse_filament: returns the integer when present", "[apply_helpers][parse_filament]") {
    json step = {{"filament", 2}};
    REQUIRE(parse_filament(step, "filament") == 2);
}

TEST_CASE("parse_filament: missing field throws", "[apply_helpers][parse_filament]") {
    json step = json::object();
    REQUIRE_THROWS_AS(parse_filament(step, "filament"), ManifestFieldError);
}

TEST_CASE("parse_filament: string-shaped value throws", "[apply_helpers][parse_filament]") {
    json step = {{"filament", "2"}};
    REQUIRE_THROWS_AS(parse_filament(step, "filament"), ManifestFieldError);
}

TEST_CASE("parse_filament: float value throws", "[apply_helpers][parse_filament]") {
    json step = {{"filament", 2.5}};
    REQUIRE_THROWS_AS(parse_filament(step, "filament"), ManifestFieldError);
}

TEST_CASE("parse_filament: zero or negative throws", "[apply_helpers][parse_filament]") {
    json step1 = {{"filament", 0}};
    json step2 = {{"filament", -1}};
    REQUIRE_THROWS_AS(parse_filament(step1, "filament"), ManifestFieldError);
    REQUIRE_THROWS_AS(parse_filament(step2, "filament"), ManifestFieldError);
}
