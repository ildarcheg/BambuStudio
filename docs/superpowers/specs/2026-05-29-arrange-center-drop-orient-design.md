# Arrange / Center / Drop-to-Bed / Auto-Orient — Design (2026-05-29)

## Goal

Add four per-plate layout verbs and one per-object verb to `bambu-cli`,
mirroring the GUI's "Arrange objects on the current plate" button, the
object-level "Auto Orient" button, and the gizmo-driven center / drop-to-bed
operations. Also change `object add`'s default placement (when no
`--translate`/`--rotate`/`--scale` is supplied) from sqrt-grid to
center-on-plate-with-Z-drop, so newly-added objects start in a usable
position for headless batch composition.

Primary use case: scripted pipelines that load STLs, place objects,
set config, and save `.3mf` files without ever opening the GUI. Output
must be usable without manual repositioning.

## Architecture

- Five new functions in `src/cli/project_ops.{hpp,cpp}`, alongside
  existing per-plate operations (`add_plate`, `remove_plate`, etc.).
  No new module. Mirrors Approach A from brainstorming.
- One private file-local helper (`collect_plate_instances`) shared by
  the four `plate_*` verbs.
- Five new CLI subcommand registrations split between
  `src/cli/commands/plate.cpp` (four verbs) and `src/cli/commands/object.cpp`
  (one verb).
- Behaviour change to `add_object_to_plate`'s auto-placement branch:
  delete sqrt-grid layout, replace with center-on-plate + Z-drop. Manual
  transform branch unchanged.
- Reuse `libslic3r/Arrange.hpp` (`arrangement::arrange()`,
  `ArrangePolygons`, `ArrangeParams`) and `libslic3r/Orient.hpp`
  (`orientation::orient(ModelInstance*)`). No new libslic3r deps; the
  CLI already links `libslic3r`.
- Bed shape comes from `arrangement::get_shrink_bedpts(project_config,
  params)` (declared in `libslic3r/Arrange.hpp:195`); arrange parameters
  come from `arrangement::update_arrange_params()` +
  `update_selected_items_inflation()` (`Arrange.hpp:189-191`). These are
  the same helpers the GUI slice path uses (`BambuStudio.cpp:5367-5368`)
  and are accessible from libslic3r alone. Exclude zones (`bed_exclude_area`)
  are parsed directly from `project_config` — the equivalent GUI helper
  `partplate_list.preprocess_exclude_areas` lives in `libslic3r_gui` which
  the CLI does not link, so the CLI reconstructs rectangular excludes
  from the config option directly.
- Multi-plate world translation reuses the existing
  `plate_world_origin(plate_index_1based, total_plates, bed_w, bed_h)`
  helper from `project_ops.cpp`.
- Per-plate scope for all four `plate_*` verbs. Group-by-name semantics
  for `object_auto_orient` (matching `remove_object` /
  `set_object_filament`).

## Scope

In scope:
- `plate arrange --plate <name> in.3mf [--output out.3mf]`
- `plate center --plate <name> in.3mf [--output out.3mf]`
- `plate drop-to-bed --plate <name> in.3mf [--output out.3mf]`
- `plate auto-orient --plate <name> in.3mf [--output out.3mf]`
- `object auto-orient --name <obj> in.3mf [--output out.3mf]`
- `add_object_to_plate` default-placement change (center + Z-drop)
- Unit, e2e, and roundtrip test coverage for all of the above

Out of scope (explicit):
- Full GUI parameter passthrough for arrange (min-distance, allow-rotation
  override, align-to-y-axis, bed-shrink, sequential-print toggle). Defaults
  pulled from project config; no CLI flags.
- Sequential-print mode handling beyond reading `print_sequence` from
  config and propagating as `ArrangeParams::is_seq_print`. No special
  per-plate sequential-print overrides.
- Assemble-plate / `need_arrange` semantics from BambuStudio.cpp's slice
  path.
- Multi-plate spill-over on arrange overflow (overflow exits 9).
- `place what fits, drop the rest` semantics.
- A standalone `reseat` combined verb.
- Backward-compat flag (`--legacy-grid` or similar) for the old `object
  add` sqrt-grid behaviour. Behaviour change is total; tests that pin old
  coordinates get updated.
- New exit codes. All errors map to existing codes.
- New exception types. `PlacementFailureError` is reused.
- Install rule for `bambu-cli`. Continues to ship from `build/`.

## Decisions reached during brainstorming

1. Primary use case: headless batch composition.
2. Scope: per-plate for arrange/center/drop-to-bed/auto-orient; per-object
   also for auto-orient.
3. Arrange knobs: sane defaults only, pulled from `state.project_config`.
   No CLI flags for tuning.
4. Overflow handling: fail with exit 9 (`placement_failure`); no save.
5. Axis split: `center` does XY only; `drop-to-bed` does Z only. Composable
   (run both for full reseat).
6. Multi-instance semantics: per-instance, independent. Each instance is
   centered/dropped on its own axes. Multi-instance objects stack at the
   bed center after `center`. Explicitly accepted.
7. `object add` default change: with no transform args, all N copies stack
   at the plate centroid with Z dropped. Bulk-add user runs `plate arrange`
   after if they want spread.
8. Naming: `plate <verb> --plate <name>` for plate-scoped, `object
   auto-orient --name <obj>` for object-scoped.
9. Auto-orient implicit drop: `plate auto-orient` and `object auto-orient`
   always finish with a Z-drop after rotating.

## Components

### New `project_ops` API

```cpp
// In src/cli/project_ops.hpp

// Center every instance on <plate_name> to the plate-bed centroid in XY.
// Z unchanged on each instance. Empty plate: success no-op.
// Errors:
//   exit 6 (unknown_reference) — plate not found
OpResult plate_center(ProjectState& state, const std::string& plate_name);

// Drop every instance on <plate_name> so its world-space mesh min-Z equals 0.
// XY unchanged on each instance. Computed via full instance transformation
// (matches the GUI's get_volume_min_z helper). Empty plate: success no-op.
// Errors:
//   exit 6 (unknown_reference) — plate not found
OpResult plate_drop_to_bed(ProjectState& state, const std::string& plate_name);

// Arrange every instance on <plate_name> using arrangement::arrange() with
// parameters pulled from state.project_config. Bed polygon and excludes
// constructed from project_config["printable_area"] and
// project_config["bed_exclude_area"]. Plate-local arrange + plate-stride
// world translation. Empty plate: success no-op.
// Errors:
//   exit 1 (usage_error)        — printable_area missing/malformed, or
//                                 bed_exclude_area malformed
//   exit 6 (unknown_reference)  — plate not found
//   exit 9 (placement_failure)  — one or more objects did not fit
OpResult plate_arrange(ProjectState& state, const std::string& plate_name);

// Auto-orient every object instance on <plate_name> using
// orientation::orient(ModelInstance*), then implicitly drop each
// instance to bed (Z translation only). Empty plate: success no-op.
// Errors:
//   exit 6 (unknown_reference)  — plate not found
//   exit 7 (invalid_state)      — orient engine reports failure
OpResult plate_auto_orient(ProjectState& state, const std::string& plate_name);

// Auto-orient every instance of every ModelObject whose name == <object_name>
// (group-by-name semantics — same as remove_object and set_object_filament),
// then implicitly drop each instance to bed. Per-instance operation; instance
// plate membership is irrelevant to correctness here. Empty match: error.
// Errors:
//   exit 6 (unknown_reference)  — no matching object found
//   exit 7 (invalid_state)      — orient engine reports failure
OpResult object_auto_orient(ProjectState& state, const std::string& object_name);
```

### Private helper

```cpp
// In src/cli/project_ops.cpp (file-local, not in header)

// Returns the (obj_idx, instance_idx) pairs of every instance belonging
// to <plate_name>, sourced from state.plate_data[i]->obj_inst_map.
// Throws std::out_of_range if no plate matches <plate_name>.
// Empty vector means the plate exists but has no objects (caller decides
// whether that's success no-op or error — for all four current callers,
// it's success no-op).
static std::vector<std::pair<int,int>>
collect_plate_instances(const ProjectState& state, const std::string& plate_name);
```

### `add_object_to_plate` change

The auto-placement branch in `project_ops.cpp` (currently
lines 256–328) is replaced. The current auto branch builds a sqrt-grid:
`grid_cols = ceil(sqrt(count))`, cells of `max(bbox_size + 10mm, 20mm)`,
starting at `plate_bed_min + auto_margin`.

The new auto branch:
- Computes `plate_center_x = (plate_bed_minx + plate_bed_maxx) / 2.0`
  (similarly y), reusing the already-in-scope `plate_bed_min*x/y` and
  `plate_bed_max*x/y` variables.
- For each of the N copies:
  `inst_k->set_offset(Vec3d(plate_center_x - bbox.center().x(),
                            plate_center_y - bbox.center().y(),
                            -bbox.min.z()))`
- Rotation and scaling factors set to `Vec3d::Zero()` and `Vec3d(1,1,1)`
  respectively, same as today.
- `grid_cols`, `cell_x`, `cell_y`, `auto_margin`, `default_cell`, `col`,
  `row` symbols deleted.

The `manual` branch (lines 309–313 — `inst_k->set_offset(stack_offset);
inst_k->set_rotation(stack_rot); inst_k->set_scaling_factor(stack_scale);`)
is unchanged. So is the `manual`-gated off-bed AABB check (lines 342+).

### CLI subcommand registrations

In `src/cli/commands/plate.cpp::register_plate_subcommands`:

```
plate arrange       --plate <name> in.3mf [--output out.3mf]
plate center        --plate <name> in.3mf [--output out.3mf]
plate drop-to-bed   --plate <name> in.3mf [--output out.3mf]
plate auto-orient   --plate <name> in.3mf [--output out.3mf]
```

In `src/cli/commands/object.cpp::register_object_subcommands`:

```
object auto-orient  --name <obj-name> in.3mf [--output out.3mf]
```

Each callback shape mirrors the existing `plate add` / `object remove`
callbacks: extract args, derive `out` (defaults to `in_path` for in-place),
invoke `run_mutation(mode, in, out, [&](ProjectState& s){...})`. Exception
overrides for `std::invalid_argument` and `std::runtime_error` follow the
pattern already established in `object split-to-parts` and `merge-parts`
(`MutationExceptionMap` with appropriate exit codes).

## Data flow per verb

### `plate_center(state, plate_name)`
1. `auto pairs = collect_plate_instances(state, plate_name)` — throws
   `std::out_of_range` (exit 6) if plate unknown.
2. Read `printable_area` from `state.project_config`; compute plate-local
   bed centroid `(bed_cx, bed_cy)`.
3. Compute world-space plate origin via existing
   `plate_world_origin(plate_idx_1based, total_plates, bed_w, bed_h)`.
4. `world_target = (plate_origin.x + bed_cx, plate_origin.y + bed_cy)`.
5. For each `(oi, ii)` in `pairs`: read `inst->get_transformation()`,
   compute current world-space mesh AABB centroid `(cx, cy)`, then
   `inst->set_offset(inst->get_offset() + Vec3d(world_target.x - cx,
                                                world_target.y - cy, 0))`.
6. Empty `pairs` → return success.

### `plate_drop_to_bed(state, plate_name)`
1. `auto pairs = collect_plate_instances(state, plate_name)`.
2. For each `(oi, ii)`:
   - Compute world-space min-Z of `model.objects[oi]->volumes[*]->mesh`
     transformed by `inst->get_transformation().get_matrix()`. Mirrors the
     algorithm in `src/slic3r/GUI/Gizmos/GizmoObjectManipulation.cpp:36`
     (`get_volume_min_z(GLVolume*)`), but operating on `ModelVolume::mesh`
     directly since we don't have `GLVolume` in the CLI.
   - `inst->set_offset(inst->get_offset() - Vec3d(0, 0, world_min_z))`.
3. Empty `pairs` → return success.

### `plate_arrange(state, plate_name)`
1. `auto pairs = collect_plate_instances(state, plate_name)`.
2. Empty → return success.
3. Build `ArrangePolygons items`: for each `(oi, ii)` call
   `get_instance_arrange_poly(state.model.objects[oi]->instances[ii],
                              state.project_config)`
   (already declared in `libslic3r/ModelArrange.hpp`).
4. Build excludes: read `bed_exclude_area` from `state.project_config`
   (a `ConfigOptionPoints`). May be empty. Validate: point count must be
   a multiple of 4 (Bambu's convention — consecutive groups of 4 points
   form rectangular zones); else `throw std::invalid_argument`. Each
   rectangle becomes one `ArrangePolygon` in the excludes vector,
   coordinates scaled via `Slic3r::scale_()`.
5. Initialize `ArrangeParams params{}`, then call
   `arrangement::update_arrange_params(params, state.project_config, items)`
   followed by `arrangement::update_selected_items_inflation(items,
   state.project_config, params)`. Both declared in `Arrange.hpp:189-191`
   and accessible from libslic3r — these are the same helpers the GUI
   slice path uses (`BambuStudio.cpp:5367-5368`). They populate
   `min_obj_distance`, `cleareance_radius`, `printable_height`,
   `clearance_height_to_rod/lid`, `is_seq_print`, and so on from
   `project_config`. Force `params.allow_rotations = true` afterwards
   (headless pipelines benefit from rotation; the GUI default agrees).
   Leave `bed_shrink_x` / `bed_shrink_y` at 0.
6. Build bed via `Points bedpts = arrangement::get_shrink_bedpts(
   state.project_config, params)` (declared in `Arrange.hpp:195`,
   returns the bed polygon points respecting `bed_shrink_*`). The
   `arrange(items, excludes, const Points& bed, params)` overload at
   `Arrange.hpp:211` auto-dispatches to the right bed shape (rectangular
   vs. circular). Validate: `bedpts.size() >= 3`; else
   `throw std::invalid_argument("arrange: project has no usable
   printable_area")`.
7. Call `arrangement::arrange(items, excludes, bedpts, params)`.
8. Overflow check: iterate `items`. If any `item.bed_idx != 0`, count
   them and `throw PlacementFailureError("arrange: N object(s) did not
   fit on plate '<name>'")`. State unmodified at this point — instances
   still hold pre-arrange offsets.
9. Apply: compute world-space plate origin via existing
   `plate_world_origin(plate_idx_1based, total_plates, bed_w, bed_h)`.
   For each `(item, pair)` in `(items, pairs)`:
   - `Vec3d new_offset(plate_origin.x() + unscale_(item.translation.x()),
                       plate_origin.y() + unscale_(item.translation.y()),
                       inst->get_offset().z())`
   - `inst->set_offset(new_offset)`
   - `inst->set_rotation(Vec3d(0, 0, item.rotation))`

### `plate_auto_orient(state, plate_name)`
1. `auto pairs = collect_plate_instances(state, plate_name)`.
2. Empty → return success.
3. For each `(oi, ii)`: `orientation::orient(state.model.objects[oi]
   ->instances[ii])` (declared in `libslic3r/Orient.hpp`).
4. Run `plate_drop_to_bed(state, plate_name)` directly (same function,
   shared state). Implicit drop, single `OpResult` returned overall.

### `object_auto_orient(state, object_name)`
1. Collect all `obj_idx` where `model.objects[obj_idx]->name ==
   object_name`. Empty → `throw std::out_of_range`.
2. For each matched `obj_idx`, for each of its instances `ii`:
   - `orientation::orient(obj->instances[ii])`
   - Compute world-space mesh min-Z of `obj->volumes[*]->mesh` transformed
     by `obj->instances[ii]->get_transformation().get_matrix()`.
   - `obj->instances[ii]->set_offset(obj->instances[ii]->get_offset() -
     Vec3d(0, 0, world_min_z))`.

## Error model

All five verbs run inside `run_mutation`. Standard envelope: exception →
exit code → JSON / text response → state rolled back if appropriate.

| Verb | Condition | Exception | Exit |
|---|---|---|---|
| All four `plate_*` | `--plate` not supplied | CLI11 | 1 |
| All four `plate_*` | plate name unknown | `std::out_of_range` | 6 |
| All four `plate_*` | plate empty | none | 0 (noop) |
| `plate arrange` | bed shape (`get_shrink_bedpts`) yields < 3 pts | `std::invalid_argument` | 1 (override) |
| `plate arrange` | `bed_exclude_area` point count not a multiple of 4 | `std::invalid_argument` | 1 (override) |
| `plate arrange` | overflow (any `bed_idx != 0`) | `PlacementFailureError` | 9 |
| `plate auto-orient` | orient engine failure | `std::runtime_error` | 7 (override) |
| `object auto-orient` | `--name` not supplied | CLI11 | 1 |
| `object auto-orient` | name matches no object | `std::out_of_range` | 6 |
| `object auto-orient` | orient engine failure | `std::runtime_error` | 7 (override) |

`PlacementFailureError` already exists in the CLI's exception hierarchy
(used by `add_object_to_plate`'s off-bed check). Reused, not redefined.

State rollback is implicit: `run_mutation` operates on a `ProjectState`
that's discarded if the mutator throws. The arrange overflow check
(step 8 of `plate_arrange`) deliberately runs **before** any
`inst->set_offset()` calls, so a thrown overflow leaves instance offsets
untouched — no half-arranged plate left in memory.

The post-write invariant guard (`invariant_guard.cpp`'s three checks:
rels target resolution, per-plate thumbnails, vector-config roundtrip)
runs unchanged. None of the new verbs touch thumbnails, rels, or aux
folders, so the guard should pass on every successful operation.

## `object add` behavioural change

With no `--translate`/`--rotate`/`--scale`, today's behaviour
(sqrt-grid layout starting at `plate_bed_min + auto_margin`) is replaced
by center-on-plate + Z-drop. Specifically:

- Single-copy add: instance offset = plate centroid in XY, `-bbox.min.z()`
  in Z. Object centered, sitting on the bed.
- Multi-copy add (`--count N` with N > 1): all N copies share the same
  offset (= plate centroid). They stack visually at the bed center. User
  is expected to run `plate arrange` afterwards to spread them out.

Manual transform branch (any of `--translate`/`--rotate`/`--scale`
supplied) is unchanged. Stacking mode still applies all N copies at the
manually-specified pose, and the off-bed AABB check still runs.

This is a breaking change for callers that omit transforms and assume
grid layout. Existing tests under `tests/cli/e2e/` that pin post-add
coordinates need their expected offsets updated. Identified during
implementation, not pre-counted in this spec.

Edge cases:
- Object bbox larger than bed: centered placement overhangs symmetrically;
  no error raised. Today's grid behaviour overhangs to one corner; no error
  either. Same edge case, different geometric outcome.
- Off-bed AABB check stays gated to `manual` mode. Auto mode (today's
  grid and tomorrow's centered) does not run it. Status quo.

## Testing

Three new test files plus a roundtrip extension.

### `tests/cli/unit/plate_layout_test.cpp` (new)

One `TEST_CASE` per `project_ops` function, invoked directly (in-process).
Fixture: `tests/cli/fixtures/local/temp_project_for_bambu_studio.3mf`
(canonical `test_reference.3mf`).

- `plate_center`: off-center single instance, multiple instances on plate,
  Plate-2 (non-origin world coords), empty plate, unknown plate name.
- `plate_drop_to_bed`: min-Z > 0, min-Z < 0, rotated instance (verifies
  full transformation is used, not naive bbox), empty plate, unknown plate.
- `plate_arrange`: two non-overlapping objects, two overlapping objects,
  project with `bed_exclude_area`, Plate-2 (world stride), too-many-to-fit
  (exit 9 + state-rollback assertion).
- `plate_auto_orient`: pre-rotated object → bed-flat after, already-flat
  object → still bed-flat after, empty plate.
- `object_auto_orient`: single instance, N instances across two plates,
  unknown name.

### `tests/cli/e2e/plate_layout_commands_test.cpp` (new)

Subprocess invocations of `bambu-cli plate {arrange,center,drop-to-bed,
auto-orient}`. Full envelope: CLI11 parsing → run_mutation → save → guard
→ atomic swap. One happy path + one error path per verb.

### `tests/cli/e2e/object_add_center_default_test.cpp` (new)

Pins the `object add` default-placement change. Three cases:
- Single add, no transform → centered + dropped (XY ≈ plate centroid,
  Z ≈ `-bbox.min.z()`).
- `--count 3`, no transform → all three at the same offset = plate centroid
  (stacked).
- `--translate 50,50` → manual transform still respected (regression guard
  against accidentally regressing the manual branch).

### `tests/cli/e2e/object_auto_orient_test.cpp` (new)

Subprocess invocation: happy path + unknown-name exit 6.

### `tests/cli/roundtrip/plate_layout_roundtrip_test.cpp` (new)

For each of the five new verbs: invoke → save to temp `.3mf` → reload →
assert in-memory state (offsets, rotations) matches what was written.
Same shape as existing roundtrip tests.

### Tests that need updating

E2E tests in `tests/cli/e2e/` that pin `object add` post-coordinates under
the old sqrt-grid assumption need their expected offsets updated to the new
centered values. Mechanical update — listed during implementation, not
pre-counted in spec.

### Out of test scope at this layer

- GUI parity on Bambu Studio app — covered by the existing
  `docs/cli/manual-test.md` sign-off gate, with a new line per verb added.
- Sequential-print arrange edge cases — `is_seq_print` is propagated from
  config but no CLI override exists, so the matrix is fixed by the input.
- Assemble-plate / `need_arrange` semantics — not exposed in CLI at all.

## Fixtures

No new fixtures. All cases use `test_reference.3mf` plus existing STLs
under `tests/cli/fixtures/stls/`. The arrange-overflow case uses
many copies of one existing STL rather than a new fixture.

## Documentation

- `docs/cli/manual-test.md`: add steps for the four `plate <verb>` and
  one `object auto-orient` commands. Each step ends with a "[ ] GUI
  open-and-verify" gate consistent with the existing M1–M10 pattern.
- `docs/cli/status.md`: new milestone entry for this feature set.
- `CLAUDE.md`: no update needed — feature lands on master via standard PR
  flow, and the architecture summary still holds (no new modules, no link
  surface changes, no new divergences).

## Open questions

None. All decisions reached during brainstorming.
