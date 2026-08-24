#pragma once

#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

#include <string>
#include <vector>

namespace bambu_cli {

// Single-flow state container: every command does
//   load_project(in.3mf) -> mutation -> save_project(out.3mf) -> guard -> rename.
struct ProjectState {
    Slic3r::Model                 model;
    Slic3r::DynamicPrintConfig    project_config;
    Slic3r::PlateDataPtrs         plate_data;   // raw pointers; owned (released in dtor)
    std::string                   source_path;  // path of the loaded .3mf (for thumbnail passthrough)

    ProjectState() = default;
    ~ProjectState();

    ProjectState(const ProjectState&) = delete;
    ProjectState& operator=(const ProjectState&) = delete;
    // Move-construction is safe (vector move leaves the source empty, so
    // the source dtor deletes nothing). Move-ASSIGN is deleted: the
    // defaulted one would replace the target's plate_data pointers without
    // deleting them — a silent leak. Nothing move-assigns a ProjectState;
    // keep it that way.
    ProjectState(ProjectState&&) = default;
    ProjectState& operator=(ProjectState&&) = delete;
};

} // namespace bambu_cli
