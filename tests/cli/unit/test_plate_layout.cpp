#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_ops.hpp"
#include "exceptions.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <stdexcept>
#include <cmath>

using bambu_cli::ProjectState;

namespace {

// Build a tiny project from the canonical reference, then add one named
// object at a known offset on the named plate. Returns indices of the
// inserted (obj_idx, instance_idx).
std::pair<int,int> add_cube_at(ProjectState& s,
                               const std::string& plate_name,
                               const Slic3r::Vec3d& offset_world) {
    bambu_cli::ObjectRef ref;
    bambu_cli::ManualTransform tf;
    tf.has_translate = true;
    tf.tx = offset_world.x();
    tf.ty = offset_world.y();
    tf.tz = offset_world.z();
    auto r = bambu_cli::add_object_to_plate(
        s, plate_name,
        bambu_cli_unit::fixture_stl("cube.stl"),
        "TestCube", -1, &tf, 1, &ref);
    REQUIRE(r.ok);
    return {ref.object_idx, ref.instance_idx};
}

// Plate centroid in world coords. Mirrors production's plate_bed_info()
// math — uses min/max bed extent rather than indexed corners so it
// stays correct on non-rectangular beds (Delta) even though the
// canonical fixture is a rectangular Bambu X1 bed.
Slic3r::Vec2d expected_plate_center_world(const ProjectState& s,
                                          const std::string& plate_name) {
    const auto& pa = s.project_config.option<Slic3r::ConfigOptionPoints>(
        "printable_area")->values;
    REQUIRE(pa.size() >= 3);
    double cx = 0, cy = 0;
    double min_x = pa.front().x(), max_x = min_x;
    double min_y = pa.front().y(), max_y = min_y;
    for (const auto& p : pa) {
        cx += p.x(); cy += p.y();
        min_x = std::min(min_x, p.x()); max_x = std::max(max_x, p.x());
        min_y = std::min(min_y, p.y()); max_y = std::max(max_y, p.y());
    }
    cx /= pa.size(); cy /= pa.size();

    int idx_1based = 0;
    for (size_t i = 0; i < s.plate_data.size(); ++i)
        if (s.plate_data[i] && s.plate_data[i]->plate_name == plate_name)
            { idx_1based = static_cast<int>(i) + 1; break; }
    REQUIRE(idx_1based > 0);

    auto origin = bambu_cli::plate_world_origin(
        idx_1based, static_cast<int>(s.plate_data.size()),
        max_x - min_x, max_y - min_y);
    return Slic3r::Vec2d(origin.x() + cx, origin.y() + cy);
}

// Name of the first plate in the canonical reference 3mf. The plan assumed
// "Plate-1" but the actual fixture uses "Plate 01 test"; query via
// `bambu-cli plate list <ref>.3mf` if this ever changes.
const std::string REF_PLATE_1 = "Plate 01 test";

} // namespace

TEST_CASE("plate_center: single off-center instance moves to plate centroid",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(10, 10, 0));
    auto* inst = s.model.objects[oi]->instances[ii];
    const double pre_z = inst->get_offset().z();

    auto r = bambu_cli::plate_center(s, REF_PLATE_1);
    REQUIRE(r.ok);

    const auto target = expected_plate_center_world(s, REF_PLATE_1);
    REQUIRE(inst->get_offset().x() == Approx(target.x()).margin(0.01));
    REQUIRE(inst->get_offset().y() == Approx(target.y()).margin(0.01));
    REQUIRE(inst->get_offset().z() == Approx(pre_z));   // Z unchanged
}

TEST_CASE("plate_center: empty plate is a success no-op",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto r = bambu_cli::plate_center(s, REF_PLATE_1);
    REQUIRE(r.ok);
}

TEST_CASE("plate_center: unknown plate -> std::out_of_range",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::plate_center(s, "NoSuchPlate"),
                      std::out_of_range);
}

TEST_CASE("plate_center: multiple instances on plate stack at centroid "
          "(per-instance, independent)", "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(10, 10, 0));
    add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(80, 80, 0));

    auto r = bambu_cli::plate_center(s, REF_PLATE_1);
    REQUIRE(r.ok);

    const auto target = expected_plate_center_world(s, REF_PLATE_1);
    int seen = 0;
    for (auto* obj : s.model.objects) {
        if (obj->name != "TestCube") continue;
        for (auto* inst : obj->instances) {
            REQUIRE(inst->get_offset().x() == Approx(target.x()).margin(0.01));
            REQUIRE(inst->get_offset().y() == Approx(target.y()).margin(0.01));
            ++seen;
        }
    }
    REQUIRE(seen == 2);
}

TEST_CASE("plate_center: plate-2 honors world stride",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::add_plate(s, "Plate-2").ok);
    add_cube_at(s, "Plate-2", Slic3r::Vec3d(10, 10, 0));

    auto r = bambu_cli::plate_center(s, "Plate-2");
    REQUIRE(r.ok);

    const auto target = expected_plate_center_world(s, "Plate-2");
    // Plate 2 centroid should be far from plate 1 centroid (BBS stride).
    const auto p1_target = expected_plate_center_world(s, REF_PLATE_1);
    REQUIRE(std::fabs(target.x() - p1_target.x()) > 100.0);

    for (auto* obj : s.model.objects) {
        if (obj->name != "TestCube") continue;
        auto* inst = obj->instances.back();
        REQUIRE(inst->get_offset().x() == Approx(target.x()).margin(0.01));
        REQUIRE(inst->get_offset().y() == Approx(target.y()).margin(0.01));
    }
}

TEST_CASE("plate_drop_to_bed: instance above bed sinks to z=0",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(50, 50, 25));
    auto* inst = s.model.objects[oi]->instances[ii];

    auto r = bambu_cli::plate_drop_to_bed(s, REF_PLATE_1);
    REQUIRE(r.ok);

    // After drop, world-space min-Z of the mesh must be ~0.
    Slic3r::BoundingBoxf3 bb =
        s.model.objects[oi]->instance_bounding_box(ii, false);
    REQUIRE(bb.min.z() == Approx(0.0).margin(0.001));
}

TEST_CASE("plate_drop_to_bed: instance sunk into bed rises to z=0",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(50, 50, -5));

    auto r = bambu_cli::plate_drop_to_bed(s, REF_PLATE_1);
    REQUIRE(r.ok);

    Slic3r::BoundingBoxf3 bb =
        s.model.objects[oi]->instance_bounding_box(ii, false);
    REQUIRE(bb.min.z() == Approx(0.0).margin(0.001));
}

TEST_CASE("plate_drop_to_bed: XY unchanged",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(33, 44, 20));
    auto* inst = s.model.objects[oi]->instances[ii];
    const double pre_x = inst->get_offset().x();
    const double pre_y = inst->get_offset().y();

    REQUIRE(bambu_cli::plate_drop_to_bed(s, REF_PLATE_1).ok);
    REQUIRE(inst->get_offset().x() == Approx(pre_x));
    REQUIRE(inst->get_offset().y() == Approx(pre_y));
}

TEST_CASE("plate_drop_to_bed: rotated instance — uses world matrix not naive bbox",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(50, 50, 30));
    auto* inst = s.model.objects[oi]->instances[ii];
    // Rotate 45° around X — this changes the world-space min-Z relative
    // to the unrotated bbox. The implementation must compose
    // instance × volume transforms, not just translate.
    inst->set_rotation(Slic3r::Vec3d(M_PI * 0.25, 0, 0));

    REQUIRE(bambu_cli::plate_drop_to_bed(s, REF_PLATE_1).ok);

    Slic3r::BoundingBoxf3 bb =
        s.model.objects[oi]->instance_bounding_box(ii, false);
    REQUIRE(bb.min.z() == Approx(0.0).margin(0.001));
}

TEST_CASE("plate_drop_to_bed: unknown plate -> std::out_of_range",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::plate_drop_to_bed(s, "NoSuchPlate"),
                      std::out_of_range);
}

TEST_CASE("plate_drop_to_bed: empty plate is success no-op",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::plate_drop_to_bed(s, REF_PLATE_1).ok);
}
