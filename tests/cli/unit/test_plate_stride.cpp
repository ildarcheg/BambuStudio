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
    // 400mm bed -> stride = 400 * 1.2
    auto o2 = plate_world_origin(2, 400.0, 400.0);
    REQUIRE(o2.x() == Approx(400.0 * 1.2));

    // 180mm bed -> stride = 180 * 1.2
    auto o2_small = plate_world_origin(2, 180.0, 180.0);
    REQUIRE(o2_small.x() == Approx(180.0 * 1.2));
}

TEST_CASE("plate_world_origin: row wraps after sqrt-cols on 256mm bed",
          "[m1_audit][stride]") {
    // sqrt(4) = 2, so 2x2 grid: plates 1,2 row 0; plates 3,4 row 1.
    auto o4 = plate_world_origin(4, 256.0, 256.0);
    REQUIRE(o4.x() == Approx(256.0 * 1.2));        // col 1
    REQUIRE(o4.y() == Approx(-256.0 * 1.2));       // row 1 (Y descends)
}

TEST_CASE("plate_world_origin: stride_y scales with bed_height (non-square bed)",
          "[m1_audit][stride]") {
    // Non-square 400x300 bed, plate 4 -> row 1, col 1 in a 2x2 grid.
    auto o4 = plate_world_origin(4, 400.0, 300.0);
    REQUIRE(o4.x() == Approx(400.0 * 1.2));
    REQUIRE(o4.y() == Approx(-300.0 * 1.2));
}
