#pragma once

#include "exit_codes.hpp"
#include "project_state.hpp"

#include <string>
#include <vector>

namespace bambu_cli {

// Result of a project_ops mutation. Pure — no I/O.
struct OpResult {
    bool        ok           = false;
    int         exit_code    = 0;
    std::string error_code;
    std::string error_message;
};

// Add a new empty plate to <state>. <name> must be unique among existing
// plate names — otherwise exit 5 (duplicate_name).
OpResult add_plate(ProjectState& state, const std::string& name);

// Remove an existing plate from <state>. The plate must exist (else exit 6).
// If the plate contains any objects/instances, removal is refused (exit 6).
OpResult remove_plate(ProjectState& state, const std::string& name);

// Rename an existing plate from <from> to <to>. <to> must be non-empty (else
// exit 1). <to> must not already be in use (else exit 5). <from> must exist
// (else exit 6).
OpResult rename_plate(ProjectState& state,
                      const std::string& from,
                      const std::string& to);

// Get all plate names in order.
std::vector<std::string> list_plate_names(const ProjectState& state);

// Object handle returned by add_object_to_plate. Owns nothing; just an alias.
struct ObjectRef {
    int object_idx   = -1;
    int instance_idx = -1;
    std::string object_name;
};

// Optional per-call transform for add_object_to_plate (M6).
// When any of has_translate/has_rotate/has_scale is true, auto-arrange is
// skipped and T·R·S is applied (in that order: scale, then rotate, then
// translate). Off-bed AABB check runs on every instance.
// Rotation values are in degrees (converted to radians in project_ops.cpp).
struct ManualTransform {
    bool   has_translate = false;
    bool   has_rotate    = false;
    bool   has_scale     = false;
    double tx = 0, ty = 0, tz = 0;
    double rx = 0, ry = 0, rz = 0;       // degrees
    double sx = 1, sy = 1, sz = 1;       // per-axis (uniform sets all three to s)
};

// Add <count> copies of an STL as separate ModelObjects on the named plate.
// Each copy is a deep-clone (add_object(const ModelObject&)) so it gets a
// distinct internal ObjectID and 3MF object_id on save — required for the BBS
// 3MF obj_inst_map to round-trip correctly (the loader keys on 3MF object_id;
// duplicate keys collapse to one entry, breaking objects_and_instances rebuild).
//
// Stamps vol->source.input_file (G/Bug-B fix) on every volume of every copy.
// Calls clear_instances() after each deep-copy (load_stl preserves the STL's
// own instance vector; leaving it causes off-by-one in count).
//
// Auto-arrange uses a sqrt-grid layout (cell_size = max(bbox+margin, 20mm);
// cols = ceil(sqrt(N)) derived from total count, not per-iteration).
// Manual transforms use stacking mode (all N copies at the same pose).
//
// Off-bed AABB check (manual only) uses scale-only, rotation excluded —
// matching the GUI's own off-bed approximation.
// Multi-plate placement applies BBS stride (bed × 1.2) to the plate world
// origin so objects land at correct world coords on plate > 1.
//
// <filament_idx>: -1 = not specified (skip extruder assignment); 1..N = 1-based
// extruder slot. Out-of-range → exit_code 1 (usage_error); state rolled back.
// <count>: clamped to max(1, count). Off-bed → exit_code 9 (placement_failure).
OpResult add_object_to_plate(ProjectState& state,
                             const std::string& plate_name,
                             const std::string& stl_path,
                             const std::string& object_name,
                             int filament_idx                = -1,
                             const ManualTransform* tf       = nullptr,
                             int count                       = 1,
                             ObjectRef* out_ref              = nullptr);

// List objects per plate (returns flat list of {plate_name, object_name}).
struct ListedObject { std::string plate_name; std::string object_name; int extruder; };
std::vector<ListedObject> list_objects(const ProjectState& state,
                                       const std::string& only_plate = {});

// ---- M9: object remove + object set-filament ------------------------------

// Remove ALL ModelObjects whose name == <object_name> from the project.
// Detaches them from all plates (obj_inst_map + objects_and_instances), then
// deletes them from the model and renumbers remaining object-index references.
// Group-by-name semantics: if N ModelObjects share the same name (e.g., from
// `object add --count N`), all N are removed in one call.
// Error codes:
//   exit 6 (unknown_reference) — no matching object found
OpResult remove_object(ProjectState& state, const std::string& object_name);

// Stamp extruder=N on ALL ModelObjects whose name == <object_name>.
// Group-by-name semantics (same N-object logic as remove_object above).
// Also applies the Bug B retrofit guard: if any volume's source.input_file is
// empty, it is populated from obj->input_file before setting the extruder key.
//
// <part_name>: "" (default) = object-level (write to ModelObject::config for
//              every matching object).
//              non-empty    = per-volume by name: scans ALL volumes across all
//              matched objects and sets extruder on every volume whose
//              vol->name == part_name. Throws std::out_of_range (exit 6) if no
//              volume with that name is found across all matched objects.
//              Error message: "part name '<NAME>' not found across <K> matching
//              object(s)".
//
// Error codes:
//   exit 6 (unknown_reference) — no matching object found
//                                OR part_name not found in any matched object
//   exit 1 (usage_error)       — filament_idx out of range [1, slot_count]
OpResult set_object_filament(ProjectState& state,
                             const std::string& object_name,
                             int filament_idx,
                             const std::string& part_name = "");

// ---- M7: config set / unset / list ----------------------------------------

// A single key-value config entry (used by config_list).
struct ConfigEntry {
    std::string key;
    std::string value;
};

// Set a config key on the project-level config (empty object_name) or on the
// per-object config of the first matching ModelObject.
// Error codes:
//   exit 4 (bad_config)        — unknown key or set_deserialize parse failure
//   exit 6 (unknown_reference) — object_name not found
OpResult config_set(ProjectState& state,
                    const std::string& object_name,
                    const std::string& key,
                    const std::string& value);

// Remove a config key from the project-level config or per-object config.
// Error codes:
//   exit 4 (bad_config)        — unknown key in print_config_def
//   exit 6 (unknown_reference) — object_name not found, OR key not set on target
OpResult config_unset(ProjectState& state,
                      const std::string& object_name,
                      const std::string& key);

// List config entries. If only_changed is true, only keys whose value differs
// from libslic3r defaults are returned. Otherwise, all keys currently set on
// the target are returned.
// Error codes (in OpResult, but config_list returns the list directly):
//   If object_name is provided and not found, returns empty vector.
// NOTE: for project_config against libslic3r defaults, many keys may differ
//   because the printer preset sets them — that's expected.
std::vector<ConfigEntry> config_list(const ProjectState& state,
                                     const std::string& object_name,
                                     bool only_changed);

// ---- D1: object split-to-parts --------------------------------------------

// Split the FIRST ModelObject whose name == <name> into multiple volumes by
// disconnected mesh components. First-match semantics — does NOT operate on
// all objects sharing the name (unlike remove_object / set_object_filament).
//
// Design decision: first-match, not group-by-name, because it is ambiguous
// which clone to split when N copies share the same name. Per Phase D prompt
// (2026-05-22) and OrcaSlicer CLI report §10 — both defer the group case.
//
// Throws:
//   std::out_of_range    if name not found in state.model.objects  (-> exit 6)
//   std::invalid_argument if:
//     - the object has != 1 volume                                  (-> exit 7 via override)
//     - that volume is not ModelVolumeType::MODEL_PART              (-> exit 7 via override)
//     - the mesh has only 1 connected component                     (-> exit 7 via override)
//
// Returns the number of resulting volumes (always >= 2 on success).
size_t split_object_to_parts(ProjectState& state, const std::string& name);

// Plate origin formula in world coordinates, mirroring BBS PartPlateList
// stride: stride_xy = bed_extent * (1 + LOGICAL_PART_PLATE_GAP) with
// LOGICAL_PART_PLATE_GAP = 1.0/5.0 = 0.2. Plate 1 (1-based) returns (0,0,0).
// Exposed for unit/regression testing of the multi-plate stride.
Slic3r::Vec3d plate_world_origin(int plate_index_1based,
                                 double bed_width, double bed_height);

// ---- D2: object merge-parts -----------------------------------------------

// Parameters for merge_object_parts. --parts must be non-empty (validated by
// the caller before calling this function).
struct MergePartsParams {
    std::vector<std::string> parts;   // volume names to merge (non-empty)
    std::string              into;    // name for the resulting merged volume
    int                      filament = -1;  // 1-based extruder slot, -1 = auto
};

// Merge named volumes of FIRST-MATCHING object <name> into a single new volume.
// First-match semantics -- NOT group-by-name. Splitting across a clone-group is
// ambiguous (which clone's volumes do we merge?). Per Phase D prompt (2026-05-22)
// and OrcaSlicer CLI report §10 -- both defer the group case.
//
// 8-step deterministic validation (fail-fast, must run in this exact order):
//   b. First-match on <name>. Unknown -> std::out_of_range           (exit 6)
//   c. Each part by name in obj.volumes. Unknown -> std::out_of_range (exit 6)
//   d. --into must not exist. Collision -> DuplicateNameError          (exit 5)
//   e. --filament range [1, slot_count]. Out of range -> std::out_of_range (exit 6)
//   f. Each source must be MODEL_PART. Not -> std::invalid_argument    (exit 7 via override)
//   g. Each source mesh non-empty. Empty -> std::invalid_argument      (exit 7 via override)
//   h. Filament agreement (if --filament not given). Disagree -> std::invalid_argument (exit 7)
//   i. Per-volume config: only "extruder" allowed. Other key -> std::invalid_argument (exit 7)
//
// (Step a -- empty --parts -- is validated in the CLI callback BEFORE run_mutation,
//  so it exits 1 directly without hitting this function.)
//
// On success: places merged volume at the lowest-indexed source slot (deterministic
// under --parts reordering), deletes source volumes, returns success message.
std::string merge_object_parts(ProjectState& state,
                               const std::string& name,
                               const MergePartsParams& p);

} // namespace bambu_cli
