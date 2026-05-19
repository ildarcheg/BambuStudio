#include "project_ops.hpp"

#include "libslic3r/Format/bbs_3mf.hpp"

#include <algorithm>

namespace bambu_cli {

std::vector<std::string> list_plate_names(const ProjectState& state) {
    std::vector<std::string> out;
    out.reserve(state.plate_data.size());
    for (const auto* p : state.plate_data) {
        if (p) out.push_back(p->plate_name);
        else   out.push_back("");
    }
    return out;
}

OpResult add_plate(ProjectState& state, const std::string& name) {
    OpResult r;
    if (name.empty()) {
        r.exit_code = 1; r.error_code = "usage_error";
        r.error_message = "plate name must be non-empty";
        return r;
    }
    for (const auto* p : state.plate_data) {
        if (p && p->plate_name == name) {
            r.exit_code = 5; r.error_code = "duplicate_name";
            r.error_message = "plate '" + name + "' already exists";
            return r;
        }
    }
    auto* plate = new Slic3r::PlateData();
    // plate_index is 1-based, monotonically increasing.
    int max_idx = 0;
    for (const auto* p : state.plate_data) if (p && p->plate_index > max_idx) max_idx = p->plate_index;
    plate->plate_index = max_idx + 1;
    plate->plate_name  = name;
    // objects_and_instances + obj_inst_map start empty (new plate is empty).
    // config inherits defaults at save time.
    state.plate_data.push_back(plate);
    r.ok = true;
    return r;
}

} // namespace bambu_cli
