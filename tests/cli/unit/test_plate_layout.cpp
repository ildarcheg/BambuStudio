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

TEST_CASE("plate_auto_orient: pre-rotated object ends up bed-flat",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(50, 50, 30));
    auto* inst = s.model.objects[oi]->instances[ii];
    inst->set_rotation(Slic3r::Vec3d(M_PI * 0.2, M_PI * 0.15, 0));

    REQUIRE(bambu_cli::plate_auto_orient(s, REF_PLATE_1).ok);

    // After auto-orient + implicit drop, world-space min-Z ~ 0.
    Slic3r::BoundingBoxf3 bb =
        s.model.objects[oi]->instance_bounding_box(ii, false);
    REQUIRE(bb.min.z() == Approx(0.0).margin(0.01));
}

TEST_CASE("plate_auto_orient: already-flat object stays bed-flat",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(50, 50, 0));

    REQUIRE(bambu_cli::plate_auto_orient(s, REF_PLATE_1).ok);

    Slic3r::BoundingBoxf3 bb =
        s.model.objects[oi]->instance_bounding_box(ii, false);
    REQUIRE(bb.min.z() == Approx(0.0).margin(0.01));
}

TEST_CASE("plate_auto_orient: empty plate is success no-op",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::plate_auto_orient(s, REF_PLATE_1).ok);
}

TEST_CASE("plate_auto_orient: unknown plate -> std::out_of_range",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::plate_auto_orient(s, "NoSuchPlate"),
                      std::out_of_range);
}

TEST_CASE("object_auto_orient: single instance — oriented and dropped",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(50, 50, 30));
    s.model.objects[oi]->instances[ii]->set_rotation(
        Slic3r::Vec3d(M_PI * 0.2, 0, 0));

    REQUIRE(bambu_cli::object_auto_orient(s, "TestCube").ok);

    Slic3r::BoundingBoxf3 bb =
        s.model.objects[oi]->instance_bounding_box(ii, false);
    REQUIRE(bb.min.z() == Approx(0.0).margin(0.01));
}

TEST_CASE("object_auto_orient: N instances across two plates — each "
          "dropped independently", "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::add_plate(s, "Plate-2").ok);
    auto p1 = add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(40, 40, 20));
    auto p2 = add_cube_at(s, "Plate-2", Slic3r::Vec3d(60, 60, 35));
    const double pre_x_p1 = s.model.objects[p1.first]->instances[p1.second]
        ->get_offset().x();
    const double pre_x_p2 = s.model.objects[p2.first]->instances[p2.second]
        ->get_offset().x();

    REQUIRE(bambu_cli::object_auto_orient(s, "TestCube").ok);

    // Both instances dropped to z=0; XY unchanged on each.
    auto bb1 = s.model.objects[p1.first]->instance_bounding_box(p1.second, false);
    auto bb2 = s.model.objects[p2.first]->instance_bounding_box(p2.second, false);
    REQUIRE(bb1.min.z() == Approx(0.0).margin(0.01));
    REQUIRE(bb2.min.z() == Approx(0.0).margin(0.01));
    REQUIRE(s.model.objects[p1.first]->instances[p1.second]->get_offset().x()
            == Approx(pre_x_p1));
    REQUIRE(s.model.objects[p2.first]->instances[p2.second]->get_offset().x()
            == Approx(pre_x_p2));
}

TEST_CASE("object_auto_orient: unknown name -> std::out_of_range",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::object_auto_orient(s, "NoSuchObject"),
                      std::out_of_range);
}

TEST_CASE("plate_arrange: two overlapping copies become non-overlapping",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto a = add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(120, 120, 0));
    auto b = add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(125, 125, 0));

    REQUIRE(bambu_cli::plate_arrange(s, REF_PLATE_1).ok);

    auto bba = s.model.objects[a.first]->instance_bounding_box(a.second, false);
    auto bbb = s.model.objects[b.first]->instance_bounding_box(b.second, false);
    // XY AABBs must not intersect after arrange.
    const bool x_overlap = bba.max.x() > bbb.min.x() && bbb.max.x() > bba.min.x();
    const bool y_overlap = bba.max.y() > bbb.min.y() && bbb.max.y() > bba.min.y();
    REQUIRE_FALSE((x_overlap && y_overlap));
}

TEST_CASE("plate_arrange: empty plate is success no-op",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::plate_arrange(s, REF_PLATE_1).ok);
}

TEST_CASE("plate_arrange: unknown plate -> std::out_of_range",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::plate_arrange(s, "NoSuchPlate"),
                      std::out_of_range);
}

TEST_CASE("plate_arrange: plate-2 honors world stride",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::add_plate(s, "Plate-2").ok);
    auto a = add_cube_at(s, "Plate-2", Slic3r::Vec3d(10, 10, 0));
    auto b = add_cube_at(s, "Plate-2", Slic3r::Vec3d(15, 15, 0));

    REQUIRE(bambu_cli::plate_arrange(s, "Plate-2").ok);

    // Plate-2's world origin is the BBS stride from plate 1 (bed * 1.2).
    // For a 256x256 Bambu X1 bed, the stride is ~307. Use a tight
    // threshold (>= 280) that distinguishes "stride fully added back in
    // step 10" from "stride half-applied or forgotten".
    auto bba = s.model.objects[a.first]->instance_bounding_box(a.second, false);
    auto bbb = s.model.objects[b.first]->instance_bounding_box(b.second, false);
    REQUIRE(bba.min.x() >= 280.0);
    REQUIRE(bbb.min.x() >= 280.0);
}

TEST_CASE("plate_arrange: malformed bed_exclude_area (count not multiple of 4) "
          "-> std::invalid_argument", "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    add_cube_at(s, REF_PLATE_1, Slic3r::Vec3d(50, 50, 0));
    // Inject 5 points — not divisible by 4. Pin the "stricter than GUI"
    // divergence documented in the spec.
    auto* opt = s.project_config.option<Slic3r::ConfigOptionPoints>(
        "bed_exclude_area", true);
    opt->values = {Slic3r::Vec2d(0, 0), Slic3r::Vec2d(10, 0),
                   Slic3r::Vec2d(10, 10), Slic3r::Vec2d(0, 10),
                   Slic3r::Vec2d(5, 5)};
    REQUIRE_THROWS_AS(bambu_cli::plate_arrange(s, REF_PLATE_1),
                      std::invalid_argument);
}

TEST_CASE("plate_arrange: too-many-objects -> PlacementFailure + state rollback",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    // Add many copies of the fixture cube — enough that they can't all
    // fit on a 256x256 Bambu X1 bed even at the minimum spacing the
    // arrange engine permits. The fixture cube is small (~10mm); the
    // Round-1 200 from the plan fits with room to spare. 800 was chosen
    // empirically to reliably trip overflow with margin.
    bambu_cli::ManualTransform tf;
    tf.has_translate = true; tf.tx = 50; tf.ty = 50;
    for (int i = 0; i < 800; ++i) {
        bambu_cli::ObjectRef ref;
        REQUIRE(bambu_cli::add_object_to_plate(
            s, REF_PLATE_1, bambu_cli_unit::fixture_stl("cube.stl"),
            "ArrCube", -1, &tf, 1, &ref).ok);
    }

    // Snapshot offsets before arrange.
    std::vector<Slic3r::Vec3d> pre;
    for (auto* obj : s.model.objects)
        if (obj->name == "ArrCube")
            for (auto* inst : obj->instances)
                pre.push_back(inst->get_offset());

    REQUIRE_THROWS_AS(bambu_cli::plate_arrange(s, REF_PLATE_1),
                      bambu_cli::PlacementFailure);

    // State rollback: instance offsets unchanged after the throw.
    std::vector<Slic3r::Vec3d> post;
    for (auto* obj : s.model.objects)
        if (obj->name == "ArrCube")
            for (auto* inst : obj->instances)
                post.push_back(inst->get_offset());
    REQUIRE(pre.size() == post.size());
    for (size_t i = 0; i < pre.size(); ++i) {
        REQUIRE(post[i].x() == Approx(pre[i].x()));
        REQUIRE(post[i].y() == Approx(pre[i].y()));
    }
}

TEST_CASE("object add (default): single copy lands at plate centroid + Z drop",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    // Call add_object_to_plate with NO transform — this is the new
    // default path (replaces sqrt-grid).
    bambu_cli::ObjectRef ref;
    auto r = bambu_cli::add_object_to_plate(
        s, REF_PLATE_1,
        bambu_cli_unit::fixture_stl("cube.stl"),
        "DefaultPlaced", -1, nullptr, 1, &ref);
    REQUIRE(r.ok);

    auto* inst = s.model.objects[ref.object_idx]->instances[ref.instance_idx];
    Slic3r::BoundingBoxf3 bb =
        s.model.objects[ref.object_idx]->instance_bounding_box(ref.instance_idx, false);

    // XY centered on plate bed.
    const auto& pa = s.project_config.option<Slic3r::ConfigOptionPoints>(
        "printable_area")->values;
    double cx = 0, cy = 0;
    for (const auto& p : pa) { cx += p.x(); cy += p.y(); }
    cx /= pa.size(); cy /= pa.size();
    REQUIRE(0.5 * (bb.min.x() + bb.max.x()) == Approx(cx).margin(0.5));
    REQUIRE(0.5 * (bb.min.y() + bb.max.y()) == Approx(cy).margin(0.5));
    // Z dropped to bed.
    REQUIRE(bb.min.z() == Approx(0.0).margin(0.001));
}

TEST_CASE("object add (default, count 3): all copies stack at plate centroid",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::ObjectRef ref;
    auto r = bambu_cli::add_object_to_plate(
        s, REF_PLATE_1,
        bambu_cli_unit::fixture_stl("cube.stl"),
        "StackedDefault", -1, nullptr, 3, &ref);
    REQUIRE(r.ok);

    // Three model objects with name "StackedDefault", all offsets equal.
    std::vector<Slic3r::Vec3d> offsets;
    for (auto* obj : s.model.objects)
        if (obj->name == "StackedDefault")
            for (auto* inst : obj->instances)
                offsets.push_back(inst->get_offset());
    REQUIRE(offsets.size() == 3);
    for (size_t i = 1; i < offsets.size(); ++i) {
        REQUIRE(offsets[i].x() == Approx(offsets[0].x()).margin(0.001));
        REQUIRE(offsets[i].y() == Approx(offsets[0].y()).margin(0.001));
        REQUIRE(offsets[i].z() == Approx(offsets[0].z()).margin(0.001));
    }
}

TEST_CASE("object add (manual transform): centering NOT applied when "
          "--translate is supplied", "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::ManualTransform tf;
    tf.has_translate = true;
    tf.tx = 30.0; tf.ty = 30.0; tf.tz = 0.0;

    bambu_cli::ObjectRef ref;
    REQUIRE(bambu_cli::add_object_to_plate(
        s, REF_PLATE_1, bambu_cli_unit::fixture_stl("cube.stl"),
        "ManualPlaced", -1, &tf, 1, &ref).ok);

    auto* inst = s.model.objects[ref.object_idx]->instances[ref.instance_idx];
    // Manual mode places the cube *centered on the manual offset*, per
    // existing stack_offset semantics in add_object_to_plate. So the
    // resulting world offset should reflect the supplied (tx, ty), NOT
    // the plate centroid.
    REQUIRE(inst->get_offset().x() == Approx(30.0).margin(0.5));
    REQUIRE(inst->get_offset().y() == Approx(30.0).margin(0.5));
}
