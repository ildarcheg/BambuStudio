#include "../test_helpers.hpp"
#include "../archive_invariants.hpp"
#include "../unit/unit_helpers.hpp"

#include "io.hpp"
#include "project_ops.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

#include <cmath>
#include <map>
#include <string>

using namespace bambu_cli_test;
using bambu_cli::ProjectState;
namespace fs = boost::filesystem;

namespace {

// Canonical reference 3mf's first plate name (not "Plate-1" — the plan's
// assumption was incorrect).
const std::string REF_PLATE_1 = "Plate 01 test";

// Snapshot of every instance's (offset, rotation) post-mutation, indexed
// by object name + instance index. Robust to instance reorder.
struct InstSnap { Slic3r::Vec3d offset; Slic3r::Vec3d rotation; };

std::map<std::pair<std::string,size_t>, InstSnap> snapshot(const ProjectState& s) {
    std::map<std::pair<std::string,size_t>, InstSnap> out;
    for (const auto* obj : s.model.objects)
        for (size_t ii = 0; ii < obj->instances.size(); ++ii)
            out[{obj->name, ii}] = InstSnap{
                obj->instances[ii]->get_offset(),
                obj->instances[ii]->get_rotation()};
    return out;
}

// Assert pre == post on all six fields (offset XYZ + rotation XYZ).
void require_snap_eq(const InstSnap& pre, const InstSnap& post,
                     double margin = 0.01) {
    REQUIRE(post.offset.x()   == Approx(pre.offset.x()).margin(margin));
    REQUIRE(post.offset.y()   == Approx(pre.offset.y()).margin(margin));
    REQUIRE(post.offset.z()   == Approx(pre.offset.z()).margin(margin));
    REQUIRE(post.rotation.x() == Approx(pre.rotation.x()).margin(margin));
    REQUIRE(post.rotation.y() == Approx(pre.rotation.y()).margin(margin));
    REQUIRE(post.rotation.z() == Approx(pre.rotation.z()).margin(margin));
}

} // namespace

TEST_CASE("roundtrip: plate_center survives save/load",
          "[roundtrip][plate_layout]") {
    const std::string in  = fresh_temp_path("_rt_center.3mf");
    const std::string out = fresh_temp_path("_rt_center_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);

    ProjectState s;
    REQUIRE(bambu_cli::load_project(in, s).ok);

    bambu_cli::ManualTransform tf;
    tf.has_translate = true; tf.tx = 10; tf.ty = 10;
    bambu_cli::ObjectRef ref;
    REQUIRE(bambu_cli::add_object_to_plate(
        s, REF_PLATE_1, std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl",
        "RT_Cube", -1, &tf, 1, &ref).ok);
    REQUIRE(bambu_cli::plate_center(s, REF_PLATE_1).ok);
    REQUIRE(bambu_cli::save_project(s, out).ok);

    auto pre = snapshot(s);
    ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto post = snapshot(s2);

    REQUIRE(post.count({"RT_Cube", 0}) == 1);
    require_snap_eq(pre[{"RT_Cube", 0}], post[{"RT_Cube", 0}]);

    fs::remove(in); fs::remove(out);
}

TEST_CASE("roundtrip: plate_drop_to_bed survives save/load",
          "[roundtrip][plate_layout]") {
    const std::string in  = fresh_temp_path("_rt_drop.3mf");
    const std::string out = fresh_temp_path("_rt_drop_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);

    ProjectState s;
    REQUIRE(bambu_cli::load_project(in, s).ok);
    bambu_cli::ManualTransform tf;
    tf.has_translate = true; tf.tx = 50; tf.ty = 50; tf.tz = 30;
    bambu_cli::ObjectRef ref;
    REQUIRE(bambu_cli::add_object_to_plate(
        s, REF_PLATE_1, std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl",
        "RT_Drop", -1, &tf, 1, &ref).ok);
    REQUIRE(bambu_cli::plate_drop_to_bed(s, REF_PLATE_1).ok);
    REQUIRE(bambu_cli::save_project(s, out).ok);

    auto pre = snapshot(s);
    ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto post = snapshot(s2);

    REQUIRE(post.count({"RT_Drop", 0}) == 1);
    require_snap_eq(pre[{"RT_Drop", 0}], post[{"RT_Drop", 0}]);

    fs::remove(in); fs::remove(out);
}

TEST_CASE("roundtrip: plate_arrange survives save/load",
          "[roundtrip][plate_layout]") {
    const std::string in  = fresh_temp_path("_rt_arr.3mf");
    const std::string out = fresh_temp_path("_rt_arr_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);

    ProjectState s;
    REQUIRE(bambu_cli::load_project(in, s).ok);
    // Use distinct object names per copy: snapshot key is
    // (object_name, instance_idx). Two ModelObjects with the same name
    // and one instance each would both produce key (name, 0) and collide.
    const std::vector<std::string> names{"RT_Arr_A", "RT_Arr_B"};
    for (size_t i = 0; i < names.size(); ++i) {
        bambu_cli::ManualTransform tf;
        tf.has_translate = true;
        tf.tx = 120 + static_cast<double>(i) * 3;
        tf.ty = 120 + static_cast<double>(i) * 3;
        bambu_cli::ObjectRef ref;
        REQUIRE(bambu_cli::add_object_to_plate(
            s, REF_PLATE_1, std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl",
            names[i], -1, &tf, 1, &ref).ok);
    }
    REQUIRE(bambu_cli::plate_arrange(s, REF_PLATE_1).ok);
    REQUIRE(bambu_cli::save_project(s, out).ok);

    auto pre = snapshot(s);
    ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto post = snapshot(s2);

    for (const auto& name : names) {
        REQUIRE(post.count({name, 0}) == 1);
        require_snap_eq(pre[{name, 0}], post[{name, 0}]);
    }
    fs::remove(in); fs::remove(out);
}

TEST_CASE("roundtrip: plate_auto_orient survives save/load",
          "[roundtrip][plate_layout]") {
    const std::string in  = fresh_temp_path("_rt_aor.3mf");
    const std::string out = fresh_temp_path("_rt_aor_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);

    ProjectState s;
    REQUIRE(bambu_cli::load_project(in, s).ok);
    bambu_cli::ManualTransform tf;
    tf.has_translate = true; tf.tx = 50; tf.ty = 50;
    bambu_cli::ObjectRef ref;
    REQUIRE(bambu_cli::add_object_to_plate(
        s, REF_PLATE_1, std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl",
        "RT_Aor", -1, &tf, 1, &ref).ok);
    s.model.objects[ref.object_idx]->instances[ref.instance_idx]
        ->set_rotation(Slic3r::Vec3d(M_PI * 0.2, 0, 0));

    REQUIRE(bambu_cli::plate_auto_orient(s, REF_PLATE_1).ok);
    REQUIRE(bambu_cli::save_project(s, out).ok);

    auto pre = snapshot(s);
    ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto post = snapshot(s2);
    REQUIRE(post.count({"RT_Aor", 0}) == 1);
    require_snap_eq(pre[{"RT_Aor", 0}], post[{"RT_Aor", 0}]);

    fs::remove(in); fs::remove(out);
}

TEST_CASE("roundtrip: object_auto_orient survives save/load",
          "[roundtrip][plate_layout]") {
    const std::string in  = fresh_temp_path("_rt_oao.3mf");
    const std::string out = fresh_temp_path("_rt_oao_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);

    ProjectState s;
    REQUIRE(bambu_cli::load_project(in, s).ok);
    bambu_cli::ManualTransform tf;
    tf.has_translate = true; tf.tx = 50; tf.ty = 50;
    bambu_cli::ObjectRef ref;
    REQUIRE(bambu_cli::add_object_to_plate(
        s, REF_PLATE_1, std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl",
        "RT_OAO", -1, &tf, 1, &ref).ok);
    s.model.objects[ref.object_idx]->instances[ref.instance_idx]
        ->set_rotation(Slic3r::Vec3d(M_PI * 0.2, 0, 0));

    REQUIRE(bambu_cli::object_auto_orient(s, "RT_OAO").ok);
    REQUIRE(bambu_cli::save_project(s, out).ok);

    auto pre = snapshot(s);
    ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto post = snapshot(s2);
    REQUIRE(post.count({"RT_OAO", 0}) == 1);
    require_snap_eq(pre[{"RT_OAO", 0}], post[{"RT_OAO", 0}]);

    fs::remove(in); fs::remove(out);
}
