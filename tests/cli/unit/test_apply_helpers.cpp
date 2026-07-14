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

using bambu_cli::parse_transform;
using bambu_cli::ManualTransform;

TEST_CASE("parse_transform: empty step returns no flags set", "[apply_helpers][parse_transform]") {
    json step = json::object();
    ManualTransform t = parse_transform(step);
    REQUIRE_FALSE(t.has_translate);
    REQUIRE_FALSE(t.has_rotate);
    REQUIRE_FALSE(t.has_scale);
}

TEST_CASE("parse_transform: translate object form", "[apply_helpers][parse_transform]") {
    json step = {{"translate", {{"x", 10}, {"y", 20}}}};
    ManualTransform t = parse_transform(step);
    REQUIRE(t.has_translate);
    REQUIRE(t.tx == Approx(10));
    REQUIRE(t.ty == Approx(20));
    REQUIRE(t.tz == Approx(0));   // default
}

TEST_CASE("parse_transform: rotate object form, degrees", "[apply_helpers][parse_transform]") {
    json step = {{"rotate", {{"z", 90}}}};
    ManualTransform t = parse_transform(step);
    REQUIRE(t.has_rotate);
    REQUIRE(t.rx == Approx(0));
    REQUIRE(t.ry == Approx(0));
    REQUIRE(t.rz == Approx(90));
}

TEST_CASE("parse_transform: scale per-axis", "[apply_helpers][parse_transform]") {
    json step = {{"scale", {{"x", 1.5}, {"y", 1.0}, {"z", 1.0}}}};
    ManualTransform t = parse_transform(step);
    REQUIRE(t.has_scale);
    REQUIRE(t.sx == Approx(1.5));
    REQUIRE(t.sy == Approx(1.0));
    REQUIRE(t.sz == Approx(1.0));
}

TEST_CASE("parse_transform: scale uniform numeric shorthand",
          "[apply_helpers][parse_transform]") {
    json step = {{"scale", 1.5}};
    ManualTransform t = parse_transform(step);
    REQUIRE(t.has_scale);
    REQUIRE(t.sx == Approx(1.5));
    REQUIRE(t.sy == Approx(1.5));
    REQUIRE(t.sz == Approx(1.5));
}

TEST_CASE("parse_transform: empty section treated as not present",
          "[apply_helpers][parse_transform]") {
    json step = {{"translate", json::object()}};
    ManualTransform t = parse_transform(step);
    REQUIRE_FALSE(t.has_translate);
}

TEST_CASE("parse_transform: all three sections", "[apply_helpers][parse_transform]") {
    json step = {
        {"translate", {{"x", 1}, {"y", 2}, {"z", 3}}},
        {"rotate",    {{"x", 10}, {"y", 20}, {"z", 30}}},
        {"scale",     {{"x", 1.5}, {"y", 1.5}, {"z", 1.5}}},
    };
    ManualTransform t = parse_transform(step);
    REQUIRE(t.has_translate);
    REQUIRE(t.has_rotate);
    REQUIRE(t.has_scale);
    REQUIRE(t.tx == Approx(1));   REQUIRE(t.ty == Approx(2));   REQUIRE(t.tz == Approx(3));
    REQUIRE(t.rx == Approx(10));  REQUIRE(t.ry == Approx(20));  REQUIRE(t.rz == Approx(30));
    REQUIRE(t.sx == Approx(1.5)); REQUIRE(t.sy == Approx(1.5)); REQUIRE(t.sz == Approx(1.5));
}

TEST_CASE("parse_transform: unknown axis key throws",
          "[apply_helpers][parse_transform]") {
    json step = {{"translate", {{"q", 1}}}};
    REQUIRE_THROWS_AS(parse_transform(step), ManifestFieldError);
}

TEST_CASE("parse_transform: translate as number throws",
          "[apply_helpers][parse_transform]") {
    json step = {{"translate", 5}};
    REQUIRE_THROWS_AS(parse_transform(step), ManifestFieldError);
}
