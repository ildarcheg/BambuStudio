#include <catch2/catch.hpp>
#include "project_ops.hpp"

using bambu_cli::plate_world_origin;

TEST_CASE("plate_world_origin: plate 1 is the origin", "[m1_audit][stride]") {
    auto o = plate_world_origin(1, 256.0, 256.0);
    REQUIRE(o.x() == Approx(0.0));
    REQUIRE(o.y() == Approx(0.0));
    REQUIRE(o.z() == Approx(0.0));
}

TEST_CASE("plate_world_origin: stride uses 1.2 multiplier on 256mm bed",
          "[m1_audit][stride]") {
    auto o2 = plate_world_origin(2, 256.0, 256.0);
    REQUIRE(o2.x() == Approx(256.0 * 1.2));
    REQUIRE(o2.y() == Approx(0.0));
}

TEST_CASE("plate_world_origin: stride scales with bed width (non-256 bed)",
          "[m1_audit][stride]") {
    // 400mm bed -> stride = 480
    auto o2 = plate_world_origin(2, 400.0, 400.0);
    REQUIRE(o2.x() == Approx(480.0));

    // 180mm bed -> stride = 216
    auto o2_small = plate_world_origin(2, 180.0, 180.0);
    REQUIRE(o2_small.x() == Approx(216.0));
}

TEST_CASE("plate_world_origin: row wraps after sqrt-cols on 256mm bed",
          "[m1_audit][stride]") {
    // sqrt(4) = 2, so 2x2 grid: plates 1,2 row 0; plates 3,4 row 1.
    auto o4 = plate_world_origin(4, 256.0, 256.0);
    REQUIRE(o4.x() == Approx(256.0 * 1.2));        // col 1
    REQUIRE(o4.y() == Approx(-256.0 * 1.2));       // row 1 (Y descends)
}
