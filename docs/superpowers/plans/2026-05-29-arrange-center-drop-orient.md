# Arrange / Center / Drop-to-Bed / Auto-Orient Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add five new layout verbs to `bambu-cli` (four per-plate, one
per-object) mirroring the GUI's Arrange / Center / Drop-to-Bed /
Auto-Orient buttons, plus change `object add`'s default placement from
sqrt-grid to center-on-plate-with-Z-drop, so headless batch composition
produces usable `.3mf` files without GUI babysitting.

**Architecture:** All five new operations are pure mutators on
`bambu_cli::ProjectState`, declared in `src/cli/project_ops.hpp` and
implemented in `src/cli/project_ops.cpp`. They reuse libslic3r's
`arrangement::arrange()` (`Arrange.hpp`) and `orientation::orient()`
(`Orient.hpp`) — no `libslic3r_gui` link surface needed. CLI registrations
go into the existing `commands/plate.cpp` and `commands/object.cpp` TUs.
The behaviour change in `add_object_to_plate` replaces its sqrt-grid
branch with a centered-stack branch sharing the same code structure as
its existing manual-stacking branch. All five verbs run through the
existing `run_mutation` envelope; no new exit codes, exception types,
or invariant guards introduced.

**Tech Stack:** C++17, libslic3r (Model, Arrange, Orient, PlateData,
DynamicPrintConfig), CLI11 (vendored), Catch2 v2 for tests, miniz for
zip round-trip assertions.

**Spec:** [docs/superpowers/specs/2026-05-29-arrange-center-drop-orient-design.md](../specs/2026-05-29-arrange-center-drop-orient-design.md)

---

## Build & Test Conventions

Per `CLAUDE.md`, this is a Windows-primary project building under VS 2019.
Test binary is `cli_tests`. Per-task verification typically runs:

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[plate_layout]"
```

The Catch2 tag filter (`"[plate_layout]"`) scopes runs to this feature's
tests once they exist. `cli_tests` accepts `--list-tests` and standard
Catch2 `-#` to print sections during failure.

Each task ends with a commit. Use `git -c commit.gpgsign=false commit` to
match the existing CLI commit pattern.

---

## File Plan

### New files

- `src/cli/project_ops.cpp` — five new functions appended + one file-local
  helper. Existing functions untouched except `add_object_to_plate`.
  (Not a new file but called out for completeness.)
- `tests/cli/unit/test_plate_layout.cpp` — unit tests for the five new
  `project_ops` functions plus the `object add` default-placement change.
- `tests/cli/e2e/test_plate_layout_commands.cpp` — e2e tests for the four
  `plate <verb>` subcommands.
- `tests/cli/e2e/test_object_auto_orient.cpp` — e2e test for `object
  auto-orient`.
- `tests/cli/roundtrip/test_plate_layout.cpp` — round-trip tests for all
  five verbs.
- `docs/cli/notes/2026-05-29-drop-to-bed-hull-vs-mesh.md` — one-pager on
  the hull-vs-full-mesh perf decision.

### Modified files

- `src/cli/project_ops.hpp` — five new function declarations.
- `src/cli/project_ops.cpp` — replace sqrt-grid branch in
  `add_object_to_plate`; append five new functions + `collect_plate_instances`.
- `src/cli/commands/plate.cpp` — register four new subcommands.
- `src/cli/commands/object.cpp` — register one new subcommand.
- `tests/cli/CMakeLists.txt` — add the four new test source files.
- `tests/cli/e2e/test_object_add.cpp` (and any other e2e tests pinning
  sqrt-grid coordinates) — update expected XY values to centered.
- `docs/cli/manual-test.md` — add one section per new verb.
- `docs/cli/status.md` — milestone entry.

---

## Task 1: `plate_center` — smallest verb, validates helper + math

**Files:**
- Modify: `src/cli/project_ops.hpp` (append declaration)
- Modify: `src/cli/project_ops.cpp` (append `collect_plate_instances`
  static helper + `plate_center` implementation)
- Create: `tests/cli/unit/test_plate_layout.cpp`
- Modify: `tests/cli/CMakeLists.txt` (add new test source)

`collect_plate_instances` is tested indirectly through `plate_center` —
it's file-local in `project_ops.cpp` and not exposed in the header. All
four `plate_*` verbs will use it.

- [ ] **Step 1: Write the failing unit tests**

Create `tests/cli/unit/test_plate_layout.cpp`:

```cpp
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

// Plate centroid in world coords, for a project loaded from the
// canonical reference (standard Bambu X1 bed: 256x256, plate 1 origin
// 0,0). Returns the XY centroid for the named plate.
Slic3r::Vec2d expected_plate_center_world(const ProjectState& s,
                                          const std::string& plate_name) {
    // Bed extent from project_config["printable_area"] is a rectangle of
    // 4 points; centroid = average of all 4 points. Plate world origin
    // adds BBS stride for plates beyond plate 1.
    const auto& pa = s.project_config.option<Slic3r::ConfigOptionPoints>(
        "printable_area")->values;
    REQUIRE(pa.size() >= 4);
    double cx = 0, cy = 0;
    for (const auto& p : pa) { cx += p.x(); cy += p.y(); }
    cx /= pa.size(); cy /= pa.size();

    int idx_1based = 0;
    for (size_t i = 0; i < s.plate_data.size(); ++i)
        if (s.plate_data[i] && s.plate_data[i]->plate_name == plate_name)
            { idx_1based = static_cast<int>(i) + 1; break; }
    REQUIRE(idx_1based > 0);

    double bw = pa[2].x() - pa[0].x();
    double bh = pa[2].y() - pa[0].y();
    auto origin = bambu_cli::plate_world_origin(
        idx_1based, static_cast<int>(s.plate_data.size()), bw, bh);
    return Slic3r::Vec2d(origin.x() + cx, origin.y() + cy);
}

} // namespace

TEST_CASE("plate_center: single off-center instance moves to plate centroid",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, "Plate-1", Slic3r::Vec3d(10, 10, 0));
    auto* inst = s.model.objects[oi]->instances[ii];
    const double pre_z = inst->get_offset().z();

    auto r = bambu_cli::plate_center(s, "Plate-1");
    REQUIRE(r.ok);

    const auto target = expected_plate_center_world(s, "Plate-1");
    REQUIRE(inst->get_offset().x() == Approx(target.x()).margin(0.01));
    REQUIRE(inst->get_offset().y() == Approx(target.y()).margin(0.01));
    REQUIRE(inst->get_offset().z() == Approx(pre_z));   // Z unchanged
}

TEST_CASE("plate_center: empty plate is a success no-op",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto r = bambu_cli::plate_center(s, "Plate-1");
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
    add_cube_at(s, "Plate-1", Slic3r::Vec3d(10, 10, 0));
    add_cube_at(s, "Plate-1", Slic3r::Vec3d(80, 80, 0));

    auto r = bambu_cli::plate_center(s, "Plate-1");
    REQUIRE(r.ok);

    const auto target = expected_plate_center_world(s, "Plate-1");
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
    const auto p1_target = expected_plate_center_world(s, "Plate-1");
    REQUIRE(std::fabs(target.x() - p1_target.x()) > 100.0);

    for (auto* obj : s.model.objects) {
        if (obj->name != "TestCube") continue;
        auto* inst = obj->instances.back();
        REQUIRE(inst->get_offset().x() == Approx(target.x()).margin(0.01));
        REQUIRE(inst->get_offset().y() == Approx(target.y()).margin(0.01));
    }
}
```

Then edit `tests/cli/CMakeLists.txt` to add the new test file. Find the
existing `unit/test_project_ops_plates.cpp` line and append after it:

```cmake
    unit/test_plate_layout.cpp
```

- [ ] **Step 2: Add the declaration to `project_ops.hpp`**

Append before the closing `} // namespace bambu_cli` of
`src/cli/project_ops.hpp`:

```cpp
// ---- Layout operations (2026-05-29) ---------------------------------------

// Center every instance on <plate_name> to the plate-bed centroid in XY.
// Z unchanged on each instance. Empty plate: success no-op.
// Errors:
//   exit 6 (unknown_reference) — plate not found
OpResult plate_center(ProjectState& state, const std::string& plate_name);
```

- [ ] **Step 3: Run the tests, confirm they fail at compile**

```
cmake --build build --target cli_tests --config RelWithDebInfo
```

Expected: failure with "undefined reference to `bambu_cli::plate_center`".

- [ ] **Step 4: Add the `collect_plate_instances` helper + `plate_center`
  implementation to `project_ops.cpp`**

Append near the bottom of `src/cli/project_ops.cpp` (before the closing
namespace brace):

```cpp
// ---------------------------------------------------------------------------
// Layout operations (2026-05-29)
// ---------------------------------------------------------------------------

// Shared by plate_center, plate_drop_to_bed, plate_arrange, plate_auto_orient.
// Sources the (obj_idx, instance_idx) pairs from PlateData::objects_and_instances,
// which io.cpp:55-73 rebuilds at load time from loaded_id_to_loc. The
// adjacent obj_inst_map is NOT used here — its post-load key/value
// semantics (instance_idx, loaded_id) make it the wrong source for
// (obj_idx, instance_idx) iteration.
//
// Throws std::out_of_range (exit 6) if no plate matches <plate_name>.
// Empty vector means the plate exists but has no objects.
static std::vector<std::pair<int,int>>
collect_plate_instances(const ProjectState& state,
                        const std::string& plate_name) {
    for (size_t i = 0; i < state.plate_data.size(); ++i) {
        const auto* pd = state.plate_data[i];
        if (!pd) continue;
        if (pd->plate_name != plate_name) continue;
        std::vector<std::pair<int,int>> out;
        out.reserve(pd->objects_and_instances.size());
        for (const auto& p : pd->objects_and_instances)
            out.emplace_back(p.first, p.second);
        return out;
    }
    throw std::out_of_range("plate '" + plate_name + "' not found");
}

// Compute (plate_index_1based, total_plates, bed_width, bed_height) for
// the named plate, plus the bed-local centroid (cx, cy). Used by
// plate_center (and reusable for plate_arrange). Throws std::out_of_range
// if the plate isn't found, std::invalid_argument if printable_area is
// degenerate.
struct PlateBedInfo {
    int           index_1based;
    int           total_plates;
    double        bed_width;
    double        bed_height;
    double        local_cx;   // bed-local centroid X
    double        local_cy;
    Slic3r::Vec3d world_origin;
};

static PlateBedInfo plate_bed_info(const ProjectState& state,
                                   const std::string& plate_name) {
    int idx_1 = 0;
    for (size_t i = 0; i < state.plate_data.size(); ++i) {
        if (state.plate_data[i] &&
            state.plate_data[i]->plate_name == plate_name) {
            idx_1 = static_cast<int>(i) + 1;
            break;
        }
    }
    if (idx_1 == 0)
        throw std::out_of_range("plate '" + plate_name + "' not found");

    const auto* pa_opt =
        state.project_config.option<Slic3r::ConfigOptionPoints>("printable_area");
    if (!pa_opt || pa_opt->values.size() < 3)
        throw std::invalid_argument("printable_area missing or < 3 points");

    const auto& pa = pa_opt->values;
    double cx = 0, cy = 0;
    double min_x = pa.front().x(), max_x = min_x;
    double min_y = pa.front().y(), max_y = min_y;
    for (const auto& p : pa) {
        cx += p.x(); cy += p.y();
        min_x = std::min(min_x, p.x()); max_x = std::max(max_x, p.x());
        min_y = std::min(min_y, p.y()); max_y = std::max(max_y, p.y());
    }
    cx /= static_cast<double>(pa.size());
    cy /= static_cast<double>(pa.size());

    PlateBedInfo info;
    info.index_1based  = idx_1;
    info.total_plates  = static_cast<int>(state.plate_data.size());
    info.bed_width     = max_x - min_x;
    info.bed_height    = max_y - min_y;
    info.local_cx      = cx;
    info.local_cy      = cy;
    info.world_origin  = plate_world_origin(info.index_1based, info.total_plates,
                                            info.bed_width, info.bed_height);
    return info;
}

OpResult plate_center(ProjectState& state, const std::string& plate_name) {
    auto pairs = collect_plate_instances(state, plate_name);
    if (pairs.empty()) {
        OpResult r; r.ok = true; return r;
    }
    auto info = plate_bed_info(state, plate_name);
    const double target_x = info.world_origin.x() + info.local_cx;
    const double target_y = info.world_origin.y() + info.local_cy;

    for (const auto& [oi, ii] : pairs) {
        auto* inst = state.model.objects[oi]->instances[ii];
        // World-space XY centroid of the instance's mesh AABB.
        Slic3r::BoundingBoxf3 bb =
            state.model.objects[oi]->instance_bounding_box(ii, false);
        const double cx_now = 0.5 * (bb.min.x() + bb.max.x());
        const double cy_now = 0.5 * (bb.min.y() + bb.max.y());
        const auto off = inst->get_offset();
        inst->set_offset(Slic3r::Vec3d(
            off.x() + (target_x - cx_now),
            off.y() + (target_y - cy_now),
            off.z()));
    }
    OpResult r; r.ok = true; return r;
}
```

- [ ] **Step 5: Build and run tests, confirm pass**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[plate_layout]"
```

Expected: all five `plate_center` test cases pass.

- [ ] **Step 6: Commit**

```bash
git add src/cli/project_ops.hpp src/cli/project_ops.cpp \
        tests/cli/unit/test_plate_layout.cpp tests/cli/CMakeLists.txt
git -c commit.gpgsign=false commit -m "feat(cli): plate_center operation (project_ops)"
```

---

## Task 2: `plate_drop_to_bed` — per-volume hull math

**Files:**
- Modify: `src/cli/project_ops.hpp` (append declaration)
- Modify: `src/cli/project_ops.cpp` (append implementation + shared helper)
- Modify: `tests/cli/unit/test_plate_layout.cpp` (append test cases)

- [ ] **Step 1: Append failing tests**

Append to `tests/cli/unit/test_plate_layout.cpp`:

```cpp
TEST_CASE("plate_drop_to_bed: instance above bed sinks to z=0",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, "Plate-1", Slic3r::Vec3d(50, 50, 25));
    auto* inst = s.model.objects[oi]->instances[ii];

    auto r = bambu_cli::plate_drop_to_bed(s, "Plate-1");
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
    auto [oi, ii] = add_cube_at(s, "Plate-1", Slic3r::Vec3d(50, 50, -5));

    auto r = bambu_cli::plate_drop_to_bed(s, "Plate-1");
    REQUIRE(r.ok);

    Slic3r::BoundingBoxf3 bb =
        s.model.objects[oi]->instance_bounding_box(ii, false);
    REQUIRE(bb.min.z() == Approx(0.0).margin(0.001));
}

TEST_CASE("plate_drop_to_bed: XY unchanged",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, "Plate-1", Slic3r::Vec3d(33, 44, 20));
    auto* inst = s.model.objects[oi]->instances[ii];
    const double pre_x = inst->get_offset().x();
    const double pre_y = inst->get_offset().y();

    REQUIRE(bambu_cli::plate_drop_to_bed(s, "Plate-1").ok);
    REQUIRE(inst->get_offset().x() == Approx(pre_x));
    REQUIRE(inst->get_offset().y() == Approx(pre_y));
}

TEST_CASE("plate_drop_to_bed: rotated instance — uses world matrix not naive bbox",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, "Plate-1", Slic3r::Vec3d(50, 50, 30));
    auto* inst = s.model.objects[oi]->instances[ii];
    // Rotate 45° around X — this changes the world-space min-Z relative
    // to the unrotated bbox. The implementation must compose
    // instance × volume transforms, not just translate.
    inst->set_rotation(Slic3r::Vec3d(M_PI * 0.25, 0, 0));

    REQUIRE(bambu_cli::plate_drop_to_bed(s, "Plate-1").ok);

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
    REQUIRE(bambu_cli::plate_drop_to_bed(s, "Plate-1").ok);
}
```

- [ ] **Step 2: Append the declaration to `project_ops.hpp`**

Append in the same layout-ops block added in Task 1:

```cpp
// Drop every instance on <plate_name> so its world-space mesh min-Z equals 0.
// XY unchanged on each instance. Computes per-volume world matrix
// (instance × volume) and iterates the volume's convex hull only, NOT
// the full mesh — same algorithm as the GUI's
// GLVolume::world_matrix() + ModelVolume::get_convex_hull() at
// slic3r/GUI/Gizmos/GizmoObjectManipulation.cpp:36-50. Empty plate:
// success no-op.
// Errors:
//   exit 6 (unknown_reference) — plate not found
OpResult plate_drop_to_bed(ProjectState& state, const std::string& plate_name);
```

- [ ] **Step 3: Run tests, confirm they fail at compile**

```
cmake --build build --target cli_tests --config RelWithDebInfo
```

Expected: undefined reference to `plate_drop_to_bed`.

- [ ] **Step 4: Add the shared per-volume hull min-Z helper +
  `plate_drop_to_bed` implementation**

Append to `src/cli/project_ops.cpp`:

```cpp
// World-space min-Z across all volumes of a single instance, using each
// volume's convex hull (typically 10-100 verts) rather than the full mesh
// (up to ~100K verts). Mathematically equivalent — the lowest-Z vertex
// is always extreme and therefore on the hull — but materially faster
// for batch composition with many instances.
//
// Composes instance × volume transforms (matches GUI's
// GLVolume::world_matrix at slic3r/GUI/Gizmos/GizmoObjectManipulation.cpp:38).
// Missing the volume transform mis-drops multi-volume objects loaded
// from a multi-part 3MF.
static double instance_world_min_z(const Slic3r::ModelObject& obj,
                                   const Slic3r::ModelInstance& inst) {
    const Slic3r::Transform3d inst_m = inst.get_transformation().get_matrix();
    double min_z = std::numeric_limits<double>::max();
    for (const auto* mv : obj.volumes) {
        if (!mv) continue;
        const Slic3r::Transform3d world_m =
            inst_m * mv->get_transformation().get_matrix();
        const Slic3r::TriangleMesh& hull = mv->get_convex_hull();
        for (const auto& v : hull.its.vertices) {
            const Slic3r::Vec3d w = world_m * v.cast<double>();
            if (w.z() < min_z) min_z = w.z();
        }
    }
    return (min_z == std::numeric_limits<double>::max()) ? 0.0 : min_z;
}

OpResult plate_drop_to_bed(ProjectState& state, const std::string& plate_name) {
    auto pairs = collect_plate_instances(state, plate_name);
    if (pairs.empty()) {
        OpResult r; r.ok = true; return r;
    }
    for (const auto& [oi, ii] : pairs) {
        const auto& obj = *state.model.objects[oi];
        auto* inst = state.model.objects[oi]->instances[ii];
        const double mz = instance_world_min_z(obj, *inst);
        const auto off = inst->get_offset();
        inst->set_offset(Slic3r::Vec3d(off.x(), off.y(), off.z() - mz));
    }
    OpResult r; r.ok = true; return r;
}
```

- [ ] **Step 5: Build and run tests, confirm pass**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[plate_layout]"
```

Expected: all `plate_center` + `plate_drop_to_bed` cases pass.

- [ ] **Step 6: Commit**

```bash
git add src/cli/project_ops.hpp src/cli/project_ops.cpp \
        tests/cli/unit/test_plate_layout.cpp
git -c commit.gpgsign=false commit -m "feat(cli): plate_drop_to_bed operation (hull-based min-Z)"
```

---

## Task 3: `plate_auto_orient` — chains orient + drop

**Files:**
- Modify: `src/cli/project_ops.hpp`
- Modify: `src/cli/project_ops.cpp`
- Modify: `tests/cli/unit/test_plate_layout.cpp`

- [ ] **Step 1: Append failing tests**

Append to `tests/cli/unit/test_plate_layout.cpp`:

```cpp
TEST_CASE("plate_auto_orient: pre-rotated object ends up bed-flat",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, "Plate-1", Slic3r::Vec3d(50, 50, 30));
    auto* inst = s.model.objects[oi]->instances[ii];
    inst->set_rotation(Slic3r::Vec3d(M_PI * 0.2, M_PI * 0.15, 0));

    REQUIRE(bambu_cli::plate_auto_orient(s, "Plate-1").ok);

    // After auto-orient + implicit drop, world-space min-Z ~ 0.
    Slic3r::BoundingBoxf3 bb =
        s.model.objects[oi]->instance_bounding_box(ii, false);
    REQUIRE(bb.min.z() == Approx(0.0).margin(0.01));
}

TEST_CASE("plate_auto_orient: already-flat object stays bed-flat",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, "Plate-1", Slic3r::Vec3d(50, 50, 0));

    REQUIRE(bambu_cli::plate_auto_orient(s, "Plate-1").ok);

    Slic3r::BoundingBoxf3 bb =
        s.model.objects[oi]->instance_bounding_box(ii, false);
    REQUIRE(bb.min.z() == Approx(0.0).margin(0.01));
}

TEST_CASE("plate_auto_orient: empty plate is success no-op",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::plate_auto_orient(s, "Plate-1").ok);
}

TEST_CASE("plate_auto_orient: unknown plate -> std::out_of_range",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(bambu_cli::plate_auto_orient(s, "NoSuchPlate"),
                      std::out_of_range);
}
```

- [ ] **Step 2: Append the declaration to `project_ops.hpp`**

```cpp
// Auto-orient every instance on <plate_name> using
// orientation::orient(ModelInstance*) (libslic3r/Orient.hpp:163), then
// implicitly drop every instance to bed (Z translation, hull-based).
// Empty plate: success no-op.
// Errors:
//   exit 6 (unknown_reference) — plate not found
//   exit 7 (invalid_state)     — orient engine failure (propagated)
OpResult plate_auto_orient(ProjectState& state, const std::string& plate_name);
```

- [ ] **Step 3: Run tests, confirm compile failure**

```
cmake --build build --target cli_tests --config RelWithDebInfo
```

- [ ] **Step 4: Add the implementation**

Add include for Orient.hpp near the top of `src/cli/project_ops.cpp`:

```cpp
#include "libslic3r/Orient.hpp"
```

Then append:

```cpp
OpResult plate_auto_orient(ProjectState& state, const std::string& plate_name) {
    auto pairs = collect_plate_instances(state, plate_name);
    if (pairs.empty()) {
        OpResult r; r.ok = true; return r;
    }
    for (const auto& [oi, ii] : pairs) {
        auto* inst = state.model.objects[oi]->instances[ii];
        Slic3r::orientation::orient(inst);
    }
    // Implicit drop after orient — rotation typically leaves the object
    // off the bed in Z. Per spec, auto-orient always finishes with drop.
    return plate_drop_to_bed(state, plate_name);
}
```

- [ ] **Step 5: Build and run tests, confirm pass**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[plate_layout]"
```

- [ ] **Step 6: Commit**

```bash
git add src/cli/project_ops.hpp src/cli/project_ops.cpp \
        tests/cli/unit/test_plate_layout.cpp
git -c commit.gpgsign=false commit -m "feat(cli): plate_auto_orient (orient + implicit drop)"
```

---

## Task 4: `object_auto_orient` — group-by-name, per-instance

**Files:**
- Modify: `src/cli/project_ops.hpp`
- Modify: `src/cli/project_ops.cpp`
- Modify: `tests/cli/unit/test_plate_layout.cpp`

- [ ] **Step 1: Append failing tests**

Append to `tests/cli/unit/test_plate_layout.cpp`:

```cpp
TEST_CASE("object_auto_orient: single instance — oriented and dropped",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto [oi, ii] = add_cube_at(s, "Plate-1", Slic3r::Vec3d(50, 50, 30));
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
    auto p1 = add_cube_at(s, "Plate-1", Slic3r::Vec3d(40, 40, 20));
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
```

- [ ] **Step 2: Append the declaration to `project_ops.hpp`**

```cpp
// Auto-orient every instance of every ModelObject whose name matches
// <object_name> (group-by-name semantics — same as remove_object and
// set_object_filament), then drop each instance to bed independently.
// Per-instance operation; plate membership is irrelevant here.
// Errors:
//   exit 6 (unknown_reference) — no matching object found
//   exit 7 (invalid_state)     — orient engine failure (propagated)
OpResult object_auto_orient(ProjectState& state, const std::string& object_name);
```

- [ ] **Step 3: Run tests, confirm compile failure**

```
cmake --build build --target cli_tests --config RelWithDebInfo
```

- [ ] **Step 4: Add the implementation**

Append to `src/cli/project_ops.cpp`:

```cpp
OpResult object_auto_orient(ProjectState& state,
                            const std::string& object_name) {
    std::vector<int> matched_obj_idx;
    for (size_t i = 0; i < state.model.objects.size(); ++i) {
        if (state.model.objects[i] &&
            state.model.objects[i]->name == object_name)
            matched_obj_idx.push_back(static_cast<int>(i));
    }
    if (matched_obj_idx.empty())
        throw std::out_of_range("object '" + object_name + "' not found");

    for (int oi : matched_obj_idx) {
        auto& obj = *state.model.objects[oi];
        for (size_t ii = 0; ii < obj.instances.size(); ++ii) {
            auto* inst = obj.instances[ii];
            Slic3r::orientation::orient(inst);
            const double mz = instance_world_min_z(obj, *inst);
            const auto off = inst->get_offset();
            inst->set_offset(Slic3r::Vec3d(off.x(), off.y(), off.z() - mz));
        }
    }
    OpResult r; r.ok = true; return r;
}
```

- [ ] **Step 5: Build and run tests, confirm pass**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[plate_layout]"
```

- [ ] **Step 6: Commit**

```bash
git add src/cli/project_ops.hpp src/cli/project_ops.cpp \
        tests/cli/unit/test_plate_layout.cpp
git -c commit.gpgsign=false commit -m "feat(cli): object_auto_orient (group-by-name)"
```

---

## Task 5: `plate_arrange` — full engine with excludes, normalization, overflow

**Files:**
- Modify: `src/cli/project_ops.hpp`
- Modify: `src/cli/project_ops.cpp`
- Modify: `tests/cli/unit/test_plate_layout.cpp`

This is the largest verb. Steps 1–6 follow the same TDD pattern, but with
more test cases and more implementation code.

- [ ] **Step 1: Append failing tests**

```cpp
TEST_CASE("plate_arrange: two overlapping copies become non-overlapping",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    auto a = add_cube_at(s, "Plate-1", Slic3r::Vec3d(120, 120, 0));
    auto b = add_cube_at(s, "Plate-1", Slic3r::Vec3d(125, 125, 0));

    REQUIRE(bambu_cli::plate_arrange(s, "Plate-1").ok);

    auto bba = s.model.objects[a.first]->instance_bounding_box(a.second, false);
    auto bbb = s.model.objects[b.first]->instance_bounding_box(b.second, false);
    // XY AABBs must not intersect after arrange.
    const bool x_overlap = bba.max.x() > bbb.min.x() && bbb.max.x() > bba.min.x();
    const bool y_overlap = bba.max.y() > bbb.min.y() && bbb.max.y() > bba.min.y();
    REQUIRE_FALSE(x_overlap && y_overlap);
}

TEST_CASE("plate_arrange: empty plate is success no-op",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE(bambu_cli::plate_arrange(s, "Plate-1").ok);
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

    // Plate-2's world origin is the BBS stride from plate 1. Both
    // arranged instances should land in plate 2's world region (X
    // roughly > bed_width). For a Bambu X1 256x256 bed, plate 2 lives
    // around X >= 256 * 1.2 = 307.
    auto bba = s.model.objects[a.first]->instance_bounding_box(a.second, false);
    auto bbb = s.model.objects[b.first]->instance_bounding_box(b.second, false);
    REQUIRE(bba.min.x() > 200.0);
    REQUIRE(bbb.min.x() > 200.0);
}

TEST_CASE("plate_arrange: too-many-objects -> PlacementFailure + state rollback",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    // Add many copies of a large cube — enough that they can't all fit.
    bambu_cli::ManualTransform tf;
    tf.has_translate = true; tf.tx = 50; tf.ty = 50;
    // Try 200 cubes — well over what fits on a 256x256 bed at any size.
    for (int i = 0; i < 200; ++i) {
        bambu_cli::ObjectRef ref;
        REQUIRE(bambu_cli::add_object_to_plate(
            s, "Plate-1", bambu_cli_unit::fixture_stl("cube.stl"),
            "ArrCube", -1, &tf, 1, &ref).ok);
    }

    // Snapshot offsets before arrange.
    std::vector<Slic3r::Vec3d> pre;
    for (auto* obj : s.model.objects)
        if (obj->name == "ArrCube")
            for (auto* inst : obj->instances)
                pre.push_back(inst->get_offset());

    REQUIRE_THROWS_AS(bambu_cli::plate_arrange(s, "Plate-1"),
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
```

- [ ] **Step 2: Append the declaration to `project_ops.hpp`**

```cpp
// Arrange every instance on <plate_name> via arrangement::arrange()
// with parameters pulled from state.project_config (via
// update_arrange_params + update_selected_items_inflation +
// get_shrink_bedpts in libslic3r/Arrange.hpp). Bed shape and excludes
// constructed from project_config; bed_exclude_area parsed as
// consecutive groups of 4 rectangular points.
//
// Translation is normalized to plate-local before arrange() (since
// get_instance_arrange_poly returns world-coord translation per
// Model.cpp:4240) and re-added after via plate_world_origin.
//
// Empty plate: success no-op.
// Errors:
//   exit 1 (usage_error)        — printable_area missing/degenerate, or
//                                 bed_exclude_area point count not multiple of 4
//   exit 6 (unknown_reference)  — plate not found
//   exit 9 (placement_failure)  — one or more items did not fit (bed_idx != 0)
OpResult plate_arrange(ProjectState& state, const std::string& plate_name);
```

- [ ] **Step 3: Run tests, confirm compile failure**

```
cmake --build build --target cli_tests --config RelWithDebInfo
```

- [ ] **Step 4: Add includes + implementation**

Add near the top of `src/cli/project_ops.cpp` (alongside the Orient.hpp
include from Task 3):

```cpp
#include "libslic3r/Arrange.hpp"
#include "libslic3r/ModelArrange.hpp"
```

Append to `src/cli/project_ops.cpp`:

```cpp
OpResult plate_arrange(ProjectState& state, const std::string& plate_name) {
    auto pairs = collect_plate_instances(state, plate_name);
    if (pairs.empty()) {
        OpResult r; r.ok = true; return r;
    }
    const auto info = plate_bed_info(state, plate_name);   // throws on degenerate

    // Build ArrangePolygons from instances. get_instance_arrange_poly
    // emits translation in world coords (Model.cpp:4240).
    Slic3r::arrangement::ArrangePolygons items;
    items.reserve(pairs.size());
    for (const auto& [oi, ii] : pairs) {
        auto* inst = state.model.objects[oi]->instances[ii];
        Slic3r::arrangement::ArrangePolygon ap =
            Slic3r::get_instance_arrange_poly(inst, state.project_config);
        // Normalize world-coord translation to plate-local. For plate 1
        // this subtracts zero. For plate >= 2 it removes the BBS stride
        // so the arrange engine sees bed-local input (otherwise the item
        // is way outside the bed polygon at bed_idx 0).
        ap.translation -= Slic3r::Vec2crd(
            Slic3r::scaled(info.world_origin.x()),
            Slic3r::scaled(info.world_origin.y()));
        items.emplace_back(std::move(ap));
    }

    // Build excludes from bed_exclude_area (consecutive groups of 4
    // rectangular points). Stricter than GUI: malformed counts throw,
    // per spec divergence note.
    Slic3r::arrangement::ArrangePolygons excludes;
    if (const auto* exc_opt = state.project_config.option<
            Slic3r::ConfigOptionPoints>("bed_exclude_area")) {
        const auto& pts = exc_opt->values;
        if (pts.size() % 4 != 0)
            throw std::invalid_argument(
                "arrange: bed_exclude_area malformed (point count not multiple of 4)");
        for (size_t i = 0; i + 3 < pts.size(); i += 4) {
            Slic3r::arrangement::ArrangePolygon e;
            for (size_t k = 0; k < 4; ++k)
                e.poly.contour.append(Slic3r::Point(
                    Slic3r::scaled(pts[i + k].x()),
                    Slic3r::scaled(pts[i + k].y())));
            e.bed_idx       = 0;   // explicit; matches PartPlate.cpp:5815
            e.is_virt_object = true;
            excludes.emplace_back(std::move(e));
        }
    }

    // Populate ArrangeParams from project config via libslic3r helpers.
    Slic3r::arrangement::ArrangeParams params;
    Slic3r::arrangement::update_arrange_params(params, state.project_config,
                                               items);
    Slic3r::arrangement::update_selected_items_inflation(items,
        state.project_config, params);
    // Deliberate CLI divergence from GUI slice path (BambuStudio.cpp:5351):
    // headless batch composition benefits unconditionally from rotation.
    params.allow_rotations = true;

    // Bed points respecting bed_shrink_* (params just got populated).
    Slic3r::Points bedpts =
        Slic3r::arrangement::get_shrink_bedpts(state.project_config, params);
    if (bedpts.size() < 3)
        throw std::invalid_argument(
            "arrange: project has no usable printable_area");

    // Run the engine.
    Slic3r::arrangement::arrange(items, excludes, bedpts, params);

    // Overflow check BEFORE applying offsets — state rollback on throw.
    int overflow = 0;
    for (const auto& ap : items) if (ap.bed_idx != 0) ++overflow;
    if (overflow > 0)
        throw PlacementFailure("arrange: " + std::to_string(overflow) +
                               " object(s) did not fit on plate '" +
                               plate_name + "'");

    // Apply: translate bed-local result back to world via plate_origin.
    for (size_t k = 0; k < pairs.size(); ++k) {
        const auto& [oi, ii] = pairs[k];
        auto* inst = state.model.objects[oi]->instances[ii];
        const auto cur = inst->get_offset();
        inst->set_offset(Slic3r::Vec3d(
            info.world_origin.x() + Slic3r::unscaled(items[k].translation.x()),
            info.world_origin.y() + Slic3r::unscaled(items[k].translation.y()),
            cur.z()));
        inst->set_rotation(Slic3r::Vec3d(0, 0, items[k].rotation));
    }
    OpResult r; r.ok = true; return r;
}
```

- [ ] **Step 5: Build and run tests, confirm pass**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[plate_layout]"
```

If the overflow test does not produce overflow with 200 cubes, the bed
is larger than expected — increase to 400 or use a larger STL. Adjust
test count until overflow reliably trips.

- [ ] **Step 6: Commit**

```bash
git add src/cli/project_ops.hpp src/cli/project_ops.cpp \
        tests/cli/unit/test_plate_layout.cpp
git -c commit.gpgsign=false commit -m "feat(cli): plate_arrange via arrangement::arrange + excludes"
```

---

## Task 6: `object add` default placement — center + drop, replace sqrt-grid

**Files:**
- Modify: `src/cli/project_ops.cpp` (the auto-grid branch in
  `add_object_to_plate`)
- Modify: `tests/cli/unit/test_plate_layout.cpp` (new tests pinning
  centered default)
- Modify: any existing e2e tests pinning sqrt-grid coordinates (see
  Step 5 — grep determines the exact files)

- [ ] **Step 1: Write failing tests for the new default**

Append to `tests/cli/unit/test_plate_layout.cpp`:

```cpp
TEST_CASE("object add (default): single copy lands at plate centroid + Z drop",
          "[unit][plate_layout]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    // Call add_object_to_plate with NO transform — this is the new
    // default path (replaces sqrt-grid).
    bambu_cli::ObjectRef ref;
    auto r = bambu_cli::add_object_to_plate(
        s, "Plate-1",
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
        s, "Plate-1",
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
        s, "Plate-1", bambu_cli_unit::fixture_stl("cube.stl"),
        "ManualPlaced", -1, &tf, 1, &ref).ok);

    auto* inst = s.model.objects[ref.object_idx]->instances[ref.instance_idx];
    // Manual mode places the cube *centered on the manual offset*, per
    // existing stack_offset semantics in add_object_to_plate. So the
    // resulting world offset should reflect the supplied (tx, ty), NOT
    // the plate centroid.
    REQUIRE(inst->get_offset().x() == Approx(30.0).margin(0.5));
    REQUIRE(inst->get_offset().y() == Approx(30.0).margin(0.5));
}
```

- [ ] **Step 2: Run tests, confirm they fail (the centered-default ones
  fail; the manual one should already pass)**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[plate_layout]"
```

- [ ] **Step 3: Modify `add_object_to_plate`'s auto-grid branch**

In `src/cli/project_ops.cpp`, locate the auto-grid `else` block
(currently around lines 314–328, the `else` of `if (manual)`). Delete
the sqrt-grid variables (`auto_margin`, `default_cell`, `cell_x`,
`cell_y`, `grid_cols`) and the per-iteration `col`, `row` calculation.
Replace the `else` branch body with:

```cpp
} else {
    // Default placement (2026-05-29): center on plate XY + drop Z.
    // Replaces the prior sqrt-grid layout. All N copies share the same
    // offset (stacked at the bed centroid). Users who want spread-out
    // copies invoke `plate arrange` afterwards.
    const double plate_center_x = 0.5 * (plate_bed_minx + plate_bed_maxx);
    const double plate_center_y = 0.5 * (plate_bed_miny + plate_bed_maxy);
    Slic3r::Vec3d offset(
        plate_center_x - bbox.center().x(),
        plate_center_y - bbox.center().y(),
        -bbox.min.z());
    inst_k->set_offset(offset);
    inst_k->set_rotation(Slic3r::Vec3d::Zero());
    inst_k->set_scaling_factor(Slic3r::Vec3d(1, 1, 1));
}
```

Also delete the pre-loop block computing `auto_margin`, `default_cell`,
`cell_x`, `cell_y`, `grid_cols` (lines 256-263 from the spec
exploration). These are only used by the deleted auto-grid path.

- [ ] **Step 4: Run tests, confirm the three new cases pass**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[plate_layout]"
```

If pre-existing tests (e.g., in `[m6]` or `[unit][objects]`) now fail
because they pinned sqrt-grid offsets, continue to Step 5.

- [ ] **Step 5: Update existing tests that pinned sqrt-grid coordinates**

```
build\tests\cli\RelWithDebInfo\cli_tests.exe --list-failures
```

For each failing test, identify the file and the specific coordinate
assertion that's now wrong. Common patterns to update:
- `REQUIRE(offset.x() == Approx(<old grid coord>))` → recompute for
  centered placement
- "object add" e2e tests in `tests/cli/e2e/test_object_add.cpp` and
  `tests/cli/e2e/test_object_transforms.cpp` that assert post-add
  offsets

Update each to the new centered values. Run the full unit + e2e suite
after each fix:

```
build\tests\cli\RelWithDebInfo\cli_tests.exe
```

Until all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/cli/project_ops.cpp tests/cli/unit/test_plate_layout.cpp \
        tests/cli/e2e/test_object_add.cpp \
        tests/cli/e2e/test_object_transforms.cpp
# Add any additional updated test files identified in step 5.
git -c commit.gpgsign=false commit -m "feat(cli): object add defaults to centered placement (replaces sqrt-grid)"
```

---

## Task 7: CLI subcommand registrations for `plate <verb>`

**Files:**
- Modify: `src/cli/commands/plate.cpp` (add four subcommand registrations)
- Create: `tests/cli/e2e/test_plate_layout_commands.cpp`
- Modify: `tests/cli/CMakeLists.txt` (add the new e2e test source)

- [ ] **Step 1: Write the failing e2e tests**

Create `tests/cli/e2e/test_plate_layout_commands.cpp`:

```cpp
#include "test_helpers.hpp"
#include "archive_invariants.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

TEST_CASE("plate center: succeeds on existing plate", "[e2e][plate_layout]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    auto r = spawn_cli({"plate", "center", "--plate", "Plate-1", out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    bambu_cli_test::run_all_basic(out);
    fs::remove(out);
}

TEST_CASE("plate drop-to-bed: succeeds on existing plate",
          "[e2e][plate_layout]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    auto r = spawn_cli({"plate", "drop-to-bed", "--plate", "Plate-1", out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    fs::remove(out);
}

TEST_CASE("plate arrange: succeeds on existing plate",
          "[e2e][plate_layout]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    auto r = spawn_cli({"plate", "arrange", "--plate", "Plate-1", out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    fs::remove(out);
}

TEST_CASE("plate auto-orient: succeeds on existing plate",
          "[e2e][plate_layout]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    auto r = spawn_cli({"plate", "auto-orient", "--plate", "Plate-1", out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    fs::remove(out);
}

TEST_CASE("plate <verb>: unknown plate -> exit 6",
          "[e2e][plate_layout]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    for (const auto& verb : {"center", "drop-to-bed", "arrange", "auto-orient"}) {
        auto r = spawn_cli({"plate", verb, "--plate", "NoSuchPlate", out});
        INFO("verb=" << verb << " stderr: " << r.stderr_text);
        REQUIRE(r.exit_code == 6);
        REQUIRE(r.stderr_text.find("unknown_reference") != std::string::npos);
    }
    fs::remove(out);
}
```

Then add the new file to `tests/cli/CMakeLists.txt` under the e2e block:

```cmake
    e2e/test_plate_layout_commands.cpp
```

- [ ] **Step 2: Run tests, confirm e2e failures (unknown subcommand)**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[e2e][plate_layout]"
```

Expected: all five e2e tests fail with exit code != 0 (CLI11 reports
"plate: requires one of arrange, center, ..." or similar).

- [ ] **Step 3: Register the four subcommands**

In `src/cli/commands/plate.cpp`, inside
`register_plate_subcommands(CLI::App& app, OutputMode* mode_out)`, after
the existing `plate list` block, append:

```cpp
    // --- plate center -------------------------------------------------
    {
        struct A { std::string in, plate, out; };
        auto* sc = plate->add_subcommand("center",
            "center every instance on a plate in XY (Z unchanged)");
        auto a = std::make_shared<A>();
        sc->add_option("in", a->in, "input .3mf")->required();
        sc->add_option("--plate", a->plate, "target plate name")->required();
        sc->add_option("--output", a->out, "output .3mf (defaults to in-place)");
        sc->callback([a, mode_out]() {
            OutputMode mode = (mode_out && *mode_out == OutputMode::Json)
                              ? OutputMode::Json : OutputMode::Text;
            const std::string& out = a->out.empty() ? a->in : a->out;
            run_mutation(mode, a->in, out, [&](ProjectState& s) {
                plate_center(s, a->plate);
                return "plate centered: " + a->plate;
            });
        });
    }

    // --- plate drop-to-bed --------------------------------------------
    {
        struct A { std::string in, plate, out; };
        auto* sc = plate->add_subcommand("drop-to-bed",
            "drop every instance on a plate to z=0 (XY unchanged)");
        auto a = std::make_shared<A>();
        sc->add_option("in", a->in, "input .3mf")->required();
        sc->add_option("--plate", a->plate, "target plate name")->required();
        sc->add_option("--output", a->out, "output .3mf (defaults to in-place)");
        sc->callback([a, mode_out]() {
            OutputMode mode = (mode_out && *mode_out == OutputMode::Json)
                              ? OutputMode::Json : OutputMode::Text;
            const std::string& out = a->out.empty() ? a->in : a->out;
            run_mutation(mode, a->in, out, [&](ProjectState& s) {
                plate_drop_to_bed(s, a->plate);
                return "plate dropped-to-bed: " + a->plate;
            });
        });
    }

    // --- plate arrange ------------------------------------------------
    {
        struct A { std::string in, plate, out; };
        auto* sc = plate->add_subcommand("arrange",
            "arrange objects on a plate (mimics the GUI per-plate Arrange button)");
        auto a = std::make_shared<A>();
        sc->add_option("in", a->in, "input .3mf")->required();
        sc->add_option("--plate", a->plate, "target plate name")->required();
        sc->add_option("--output", a->out, "output .3mf (defaults to in-place)");
        sc->callback([a, mode_out]() {
            OutputMode mode = (mode_out && *mode_out == OutputMode::Json)
                              ? OutputMode::Json : OutputMode::Text;
            const std::string& out = a->out.empty() ? a->in : a->out;
            // PlacementFailure (exit 9) is already mapped by run_mutation's
            // default exception table — no override needed here.
            run_mutation(mode, a->in, out, [&](ProjectState& s) {
                plate_arrange(s, a->plate);
                return "plate arranged: " + a->plate;
            });
        });
    }

    // --- plate auto-orient --------------------------------------------
    {
        struct A { std::string in, plate, out; };
        auto* sc = plate->add_subcommand("auto-orient",
            "auto-orient every object on a plate and drop to bed");
        auto a = std::make_shared<A>();
        sc->add_option("in", a->in, "input .3mf")->required();
        sc->add_option("--plate", a->plate, "target plate name")->required();
        sc->add_option("--output", a->out, "output .3mf (defaults to in-place)");
        sc->callback([a, mode_out]() {
            OutputMode mode = (mode_out && *mode_out == OutputMode::Json)
                              ? OutputMode::Json : OutputMode::Text;
            const std::string& out = a->out.empty() ? a->in : a->out;
            // std::runtime_error from orient engine -> exit 7 (invalid_state).
            MutationExceptionMap overrides = {
                {std::type_index(typeid(std::runtime_error)),
                 {7, "invalid_state"}}
            };
            run_mutation(mode, a->in, out, [&](ProjectState& s) {
                plate_auto_orient(s, a->plate);
                return "plate auto-oriented: " + a->plate;
            }, overrides);
        });
    }
```

Verify that `mutation_runner.hpp` already exposes
`MutationExceptionMap` and `to_int` (it does — used by the existing
`object split-to-parts` and `merge-parts` callbacks in this same file
and `commands/object.cpp`). If `std::type_index` requires a header,
add `#include <typeindex>` near the top of `commands/plate.cpp`.

- [ ] **Step 4: Run e2e tests, confirm pass**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[e2e][plate_layout]"
```

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/plate.cpp \
        tests/cli/e2e/test_plate_layout_commands.cpp \
        tests/cli/CMakeLists.txt
git -c commit.gpgsign=false commit -m "feat(cli): plate arrange/center/drop-to-bed/auto-orient subcommands"
```

---

## Task 8: CLI registration for `object auto-orient`

**Files:**
- Modify: `src/cli/commands/object.cpp` (add subcommand)
- Create: `tests/cli/e2e/test_object_auto_orient.cpp`
- Modify: `tests/cli/CMakeLists.txt`

- [ ] **Step 1: Write the failing e2e test**

Create `tests/cli/e2e/test_object_auto_orient.cpp`:

```cpp
#include "test_helpers.hpp"
#include "archive_invariants.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

TEST_CASE("object auto-orient: succeeds with --name", "[e2e][object_auto_orient]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    // Add a cube first (so the project has at least one named object).
    auto add = spawn_cli({"object", "add", out, "--plate", "Plate-1",
                          "--stl", std::string(BAMBU_CLI_FIXTURE_STL_DIR)
                          + "/cube.stl", "--name", "AOCube"});
    INFO("add stderr: " << add.stderr_text);
    REQUIRE(add.exit_code == 0);

    auto r = spawn_cli({"object", "auto-orient", out, "--name", "AOCube"});
    INFO("orient stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    bambu_cli_test::run_all_basic(out);
    fs::remove(out);
}

TEST_CASE("object auto-orient: unknown name -> exit 6",
          "[e2e][object_auto_orient]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out,
                  fs::copy_option::overwrite_if_exists);
    auto r = spawn_cli({"object", "auto-orient", out, "--name", "Missing"});
    REQUIRE(r.exit_code == 6);
    REQUIRE(r.stderr_text.find("unknown_reference") != std::string::npos);
    fs::remove(out);
}
```

Then add the new file to `tests/cli/CMakeLists.txt` under the e2e block:

```cmake
    e2e/test_object_auto_orient.cpp
```

- [ ] **Step 2: Run, confirm failure**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[e2e][object_auto_orient]"
```

- [ ] **Step 3: Register the subcommand**

In `src/cli/commands/object.cpp`, inside
`register_object_subcommands(CLI::App& app, OutputMode* mode_out)`,
after the existing `object merge-parts` block, append:

```cpp
    // --- object auto-orient -----------------------------------------------
    // Group-by-name semantics: orients all instances of every ModelObject
    // whose name matches. Each instance drops to bed after orient.
    {
        struct A { std::string in, name, out; };
        auto* sc = object->add_subcommand("auto-orient",
            "auto-orient an object (all clones if group-by-name) and drop to bed");
        auto a = std::make_shared<A>();
        sc->add_option("in", a->in, "input .3mf")->required();
        sc->add_option("--name", a->name, "object name (all clones oriented)")->required();
        sc->add_option("--output", a->out, "output .3mf (defaults to in-place)");
        sc->callback([a, mode_out]() {
            OutputMode mode = (mode_out && *mode_out == OutputMode::Json)
                              ? OutputMode::Json : OutputMode::Text;
            const std::string& out = a->out.empty() ? a->in : a->out;
            MutationExceptionMap overrides = {
                {std::type_index(typeid(std::runtime_error)),
                 {7, "invalid_state"}}
            };
            run_mutation(mode, a->in, out, [&](ProjectState& s) {
                object_auto_orient(s, a->name);
                return "object auto-oriented: " + a->name;
            }, overrides);
        });
    }
```

Verify `#include <typeindex>` is already present (it is, from the
existing `split-to-parts` and `merge-parts` registrations).

- [ ] **Step 4: Run, confirm pass**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[e2e][object_auto_orient]"
```

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/object.cpp \
        tests/cli/e2e/test_object_auto_orient.cpp \
        tests/cli/CMakeLists.txt
git -c commit.gpgsign=false commit -m "feat(cli): object auto-orient subcommand"
```

---

## Task 9: Roundtrip tests for all five verbs

**Files:**
- Create: `tests/cli/roundtrip/test_plate_layout.cpp`
- Modify: `tests/cli/CMakeLists.txt`

- [ ] **Step 1: Write the round-trip tests**

Create `tests/cli/roundtrip/test_plate_layout.cpp`:

```cpp
#include "../test_helpers.hpp"
#include "../archive_invariants.hpp"
#include "../unit/unit_helpers.hpp"

#include "io.hpp"
#include "project_ops.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
using bambu_cli::ProjectState;
namespace fs = boost::filesystem;

namespace {

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
        s, "Plate-1", std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl",
        "RT_Cube", -1, &tf, 1, &ref).ok);
    REQUIRE(bambu_cli::plate_center(s, "Plate-1").ok);
    REQUIRE(bambu_cli::save_project(s, out).ok);

    auto pre = snapshot(s);
    ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto post = snapshot(s2);

    REQUIRE(post.count({"RT_Cube", 0}) == 1);
    REQUIRE(post[{"RT_Cube", 0}].offset.x() ==
            Approx(pre[{"RT_Cube", 0}].offset.x()).margin(0.01));
    REQUIRE(post[{"RT_Cube", 0}].offset.y() ==
            Approx(pre[{"RT_Cube", 0}].offset.y()).margin(0.01));

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
        s, "Plate-1", std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl",
        "RT_Drop", -1, &tf, 1, &ref).ok);
    REQUIRE(bambu_cli::plate_drop_to_bed(s, "Plate-1").ok);
    REQUIRE(bambu_cli::save_project(s, out).ok);

    auto pre = snapshot(s);
    ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto post = snapshot(s2);

    REQUIRE(post[{"RT_Drop", 0}].offset.z() ==
            Approx(pre[{"RT_Drop", 0}].offset.z()).margin(0.01));

    fs::remove(in); fs::remove(out);
}

TEST_CASE("roundtrip: plate_arrange survives save/load",
          "[roundtrip][plate_layout]") {
    const std::string in  = fresh_temp_path("_rt_arr.3mf");
    const std::string out = fresh_temp_path("_rt_arr_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);

    ProjectState s;
    REQUIRE(bambu_cli::load_project(in, s).ok);
    for (int i = 0; i < 2; ++i) {
        bambu_cli::ManualTransform tf;
        tf.has_translate = true; tf.tx = 120 + i * 3; tf.ty = 120 + i * 3;
        bambu_cli::ObjectRef ref;
        REQUIRE(bambu_cli::add_object_to_plate(
            s, "Plate-1", std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl",
            "RT_Arr", -1, &tf, 1, &ref).ok);
    }
    REQUIRE(bambu_cli::plate_arrange(s, "Plate-1").ok);
    REQUIRE(bambu_cli::save_project(s, out).ok);

    auto pre = snapshot(s);
    ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto post = snapshot(s2);

    for (size_t ii = 0; ii < 2; ++ii) {
        REQUIRE(post.count({"RT_Arr", ii}) == 1);
        REQUIRE(post[{"RT_Arr", ii}].offset.x() ==
                Approx(pre[{"RT_Arr", ii}].offset.x()).margin(0.01));
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
        s, "Plate-1", std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl",
        "RT_Aor", -1, &tf, 1, &ref).ok);
    s.model.objects[ref.object_idx]->instances[ref.instance_idx]
        ->set_rotation(Slic3r::Vec3d(M_PI * 0.2, 0, 0));

    REQUIRE(bambu_cli::plate_auto_orient(s, "Plate-1").ok);
    REQUIRE(bambu_cli::save_project(s, out).ok);

    auto pre = snapshot(s);
    ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto post = snapshot(s2);
    REQUIRE(post[{"RT_Aor", 0}].offset.z() ==
            Approx(pre[{"RT_Aor", 0}].offset.z()).margin(0.01));

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
        s, "Plate-1", std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl",
        "RT_OAO", -1, &tf, 1, &ref).ok);
    s.model.objects[ref.object_idx]->instances[ref.instance_idx]
        ->set_rotation(Slic3r::Vec3d(M_PI * 0.2, 0, 0));

    REQUIRE(bambu_cli::object_auto_orient(s, "RT_OAO").ok);
    REQUIRE(bambu_cli::save_project(s, out).ok);

    auto pre = snapshot(s);
    ProjectState s2;
    REQUIRE(bambu_cli::load_project(out, s2).ok);
    auto post = snapshot(s2);
    REQUIRE(post[{"RT_OAO", 0}].offset.z() ==
            Approx(pre[{"RT_OAO", 0}].offset.z()).margin(0.01));

    fs::remove(in); fs::remove(out);
}
```

Then add to `tests/cli/CMakeLists.txt` under the roundtrip block:

```cmake
    roundtrip/test_plate_layout.cpp
```

- [ ] **Step 2: Run, confirm pass**

```
cmake --build build --target cli_tests --config RelWithDebInfo
build\tests\cli\RelWithDebInfo\cli_tests.exe "[roundtrip][plate_layout]"
```

If a roundtrip test fails on offset comparison, the most likely cause is
a save-side rebuild of `objects_and_instances` that re-keys by 3MF
object_id; investigate by reading `src/cli/io.cpp:55-73` (the load-side
rebuild) and the matching save-side logic. Fix the test snapshotting
strategy if needed, not the production code, unless a real save bug is
found.

- [ ] **Step 3: Commit**

```bash
git add tests/cli/roundtrip/test_plate_layout.cpp tests/cli/CMakeLists.txt
git -c commit.gpgsign=false commit -m "test(cli): roundtrip coverage for plate layout verbs"
```

---

## Task 10: Documentation — manual-test.md, status.md, notes one-pager

**Files:**
- Modify: `docs/cli/manual-test.md` (append sections for the new verbs)
- Modify: `docs/cli/status.md` (add milestone entry)
- Create: `docs/cli/notes/2026-05-29-drop-to-bed-hull-vs-mesh.md`

- [ ] **Step 1: Append manual-test steps**

Append to `docs/cli/manual-test.md`, after the existing last step block
(around "Step 15"):

```markdown
## Step 16: plate center — center every instance on a plate

```
bambu-cli plate center --plate Plate-1 plate-1-and-2.3mf
```

Expected: exit 0; "plate centered: Plate-1". Open in Bambu Studio — every
object on Plate-1 should be at the plate centroid (stacked if multiple).
Z unchanged.

[ ] open-and-verify in Bambu Studio

## Step 17: plate drop-to-bed — drop every instance to bed

```
bambu-cli plate drop-to-bed --plate Plate-1 plate-1-and-2.3mf
```

Expected: exit 0; "plate dropped-to-bed: Plate-1". XY unchanged; every
object sits with its lowest face on the bed.

[ ] open-and-verify in Bambu Studio

## Step 18: plate arrange — mimic the per-plate Arrange button

```
bambu-cli plate arrange --plate Plate-1 plate-1-and-2.3mf
```

Expected: exit 0; "plate arranged: Plate-1". Objects on Plate-1 nest
without overlap, respecting `bed_exclude_area`. Plate-2 untouched.
If overflow: exit 9 with "arrange: N object(s) did not fit on plate
'Plate-1'".

[ ] open-and-verify in Bambu Studio

## Step 19: plate auto-orient — orient and drop every object on a plate

```
bambu-cli plate auto-orient --plate Plate-1 plate-1-and-2.3mf
```

Expected: exit 0; "plate auto-oriented: Plate-1". Each object on Plate-1
rotated to its best printing orientation and dropped to bed.

[ ] open-and-verify in Bambu Studio

## Step 20: object auto-orient — orient a single named object

```
bambu-cli object auto-orient --name Bracket plate-1-and-2.3mf
```

Expected: exit 0; "object auto-oriented: Bracket". All instances of
"Bracket" oriented and dropped, regardless of plate membership.

[ ] open-and-verify in Bambu Studio
```

- [ ] **Step 2: Append the status.md milestone entry**

Append to `docs/cli/status.md`, after the most recent milestone entry:

```markdown
## M11 — Layout operations (2026-05-29)

- [x] `plate center` (`project_ops::plate_center`)
- [x] `plate drop-to-bed` (`project_ops::plate_drop_to_bed`, hull-based min-Z)
- [x] `plate arrange` (`project_ops::plate_arrange`, libslic3r helpers)
- [x] `plate auto-orient` (`project_ops::plate_auto_orient`, orient + implicit drop)
- [x] `object auto-orient` (`project_ops::object_auto_orient`, group-by-name)
- [x] `object add` default placement: center + Z-drop (replaces sqrt-grid)
- [ ] Manual GUI sign-off (Steps 16–20 of `docs/cli/manual-test.md`)

Spec: `docs/superpowers/specs/2026-05-29-arrange-center-drop-orient-design.md`
Notes: `docs/cli/notes/2026-05-29-drop-to-bed-hull-vs-mesh.md`
```

- [ ] **Step 3: Write the hull-vs-mesh notes one-pager**

Create `docs/cli/notes/2026-05-29-drop-to-bed-hull-vs-mesh.md`:

```markdown
# drop-to-bed: convex hull vs full mesh (2026-05-29)

`plate_drop_to_bed` and `object_auto_orient` iterate
`ModelVolume::get_convex_hull().its.vertices` to compute world-space
min-Z, not the full mesh's vertex set. This note exists to forestall
the obvious "fix" of switching to full mesh.

## Why hull, not mesh

The lowest-Z vertex of a transformed mesh is always an extreme point and
therefore on the convex hull. So min-Z over hull verts == min-Z over
mesh verts. **Mathematically identical.**

## Why it matters for the CLI

Hulls are typically 10–100 vertices. Full STL meshes can be ~100K
vertices. Headless batch composition is bambu-cli's primary use case;
dropping a plate of N instances iterates N × verts. The 1000× constant
factor is the whole reason.

## Why the GUI does it the same way

`src/slic3r/GUI/Gizmos/GizmoObjectManipulation.cpp:36-50` uses
`ModelVolume::get_convex_hull()` for the same calculation in the
gizmo's "drop to bed" button. The CLI matches.

## Why per-volume world matrix, not per-instance

The GUI uses `GLVolume::world_matrix()` which composes
`instance.transformation × volume.transformation`. The CLI does the
same (`inst_m * mv->get_transformation().get_matrix()`). Missing the
volume transform mis-drops multi-volume objects loaded from a
multi-part 3MF — the volumes have their own per-part offsets, and
treating them all as if they had the instance's offset puts non-primary
volumes at the wrong Z.

## Verifying

The unit test
`plate_drop_to_bed: rotated instance — uses world matrix not naive bbox`
(in `tests/cli/unit/test_plate_layout.cpp`) pins the rotation case.
For multi-volume coverage, see the `object_auto_orient: N instances
across two plates` test which exercises the same code path.
```

- [ ] **Step 4: Commit**

```bash
git add docs/cli/manual-test.md docs/cli/status.md \
        docs/cli/notes/2026-05-29-drop-to-bed-hull-vs-mesh.md
git -c commit.gpgsign=false commit -m "docs(cli): M11 layout ops — manual-test, status, hull-vs-mesh notes"
```

---

## Self-review checklist

Run through this list after the last commit:

- [ ] All five new project_ops functions land with both unit and roundtrip
  coverage.
- [ ] `object add` default change has both unit and (at least one)
  e2e test pinning the new behaviour.
- [ ] `plate <verb>` × 4 has e2e coverage including unknown-plate exit 6.
- [ ] `object auto-orient` has e2e coverage including unknown-name exit 6.
- [ ] No new exit codes introduced (verified against `exit_codes.hpp`).
- [ ] No new exception classes (PlacementFailure reused; `std::runtime_error`
  → exit 7 mapping reused for orient).
- [ ] `libslic3r_gui` not linked (verified by inspecting
  `src/cli/CMakeLists.txt`).
- [ ] Manual-test sign-off boxes added but unchecked (manual GUI gate is
  user's job, not the implementer's).
- [ ] Spec is referenced from status.md milestone entry.

If any item fails, fix it in a follow-up commit before requesting review.
