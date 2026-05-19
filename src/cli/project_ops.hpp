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

} // namespace bambu_cli
