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

// Add a single-volume object to the named plate by loading <stl_path> via
// libslic3r's load_stl. Stamps vol->source.input_file (G/Bug-B fix). Auto-
// arranges within the plate's printable area. If no plate matches <plate_name>,
// returns exit_code 6 (unknown_reference).
// <filament_idx> is 1-based extruder slot (0 = unset). Out-of-range ->
// exit_code 1 (usage_error); state is rolled back on failure.
OpResult add_object_to_plate(ProjectState& state,
                             const std::string& plate_name,
                             const std::string& stl_path,
                             const std::string& object_name,
                             int filament_idx = 0,
                             ObjectRef* out_ref = nullptr);

// List objects per plate (returns flat list of {plate_name, object_name}).
struct ListedObject { std::string plate_name; std::string object_name; int extruder; };
std::vector<ListedObject> list_objects(const ProjectState& state,
                                       const std::string& only_plate = {});

} // namespace bambu_cli
