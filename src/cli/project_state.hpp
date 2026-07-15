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
    ProjectState(ProjectState&&) = default;
    ProjectState& operator=(ProjectState&&) = default;
};

} // namespace bambu_cli
