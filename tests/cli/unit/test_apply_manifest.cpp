#include "commands/project_apply_internal.hpp"

#include "exceptions.hpp"

#include <catch2/catch.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;
using bambu_cli::ManifestFieldError;
using bambu_cli::parse_and_validate_manifest;

TEST_CASE("manifest: empty operations array is valid",
          "[project_apply][manifest]") {
    json m = {{"version", 1}, {"operations", json::array()}};
    REQUIRE_NOTHROW(parse_and_validate_manifest(m));
}

TEST_CASE("manifest: missing version throws",
          "[project_apply][manifest]") {
    json m = {{"operations", json::array()}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: version != 1 throws",
          "[project_apply][manifest]") {
    json m = {{"version", 2}, {"operations", json::array()}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: version not integer throws",
          "[project_apply][manifest]") {
    json m = {{"version", "1"}, {"operations", json::array()}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: missing operations throws",
          "[project_apply][manifest]") {
    json m = {{"version", 1}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: operations not array throws",
          "[project_apply][manifest]") {
    json m = {{"version", 1}, {"operations", "foo"}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: unknown top-level key throws",
          "[project_apply][manifest]") {
    json m = {{"version", 1}, {"operations", json::array()}, {"foo", 1}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: not a top-level object throws",
          "[project_apply][manifest]") {
    json m = json::array({1, 2, 3});
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: per-step op must be string",
          "[project_apply][manifest]") {
    json m = {{"version", 1}, {"operations", json::array({json::object({{"op", 42}})})}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: per-step missing op",
          "[project_apply][manifest]") {
    json m = {{"version", 1}, {"operations", json::array({json::object({{"name", "P1"}})})}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: size cap at 10000 accepted, 10001 rejected",
          "[project_apply][manifest]") {
    json ops_ok = json::array();
    for (int i = 0; i < 10000; ++i) ops_ok.push_back({{"op", "plate.add"}, {"name", "p" + std::to_string(i)}});
    json m_ok = {{"version", 1}, {"operations", ops_ok}};
    REQUIRE_NOTHROW(parse_and_validate_manifest(m_ok));

    json ops_bad = ops_ok;
    ops_bad.push_back({{"op", "plate.add"}, {"name", "overflow"}});
    json m_bad = {{"version", 1}, {"operations", ops_bad}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m_bad), ManifestFieldError);
}
