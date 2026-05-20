#pragma once

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

// Add a single-volume object to the named plate by loading <stl_path> via
// libslic3r's load_stl. Stamps vol->source.input_file (G/Bug-B fix). Auto-
// arranges within the plate's printable area. If no plate matches <plate_name>,
// returns exit_code 6 (unknown_reference).
// <filament_idx>: -1 = not specified (skip extruder assignment); 1..N = 1-based
// extruder slot. 0 or negative values != -1 → exit_code 1 (usage_error).
// Out-of-range → exit_code 1 (usage_error); state is rolled back on failure.
// <tf>: optional per-call manual transform; nullptr = auto-arrange.
// <count>: number of copies (1..N); all copies receive the same T·R·S.
// Off-bed AABB (manual transforms only) → exit_code 9 (placement_failure).
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

} // namespace bambu_cli
