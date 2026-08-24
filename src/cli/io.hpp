#pragma once

#include "exit_codes.hpp"
#include "project_state.hpp"

#include <string>

namespace bambu_cli {

// Result of a load/save operation. `error_message` is empty on success.
struct IoResult {
    bool        ok           = false;
    int         exit_code    = 0;       // 0 on ok; mapped exit code on failure
    std::string error_code;             // machine code: "file_not_found", "parse_failure", "invalid_state"
    std::string error_message;          // human-readable
};

// Load <path> into <state>. Always uses LoadStrategy::LoadModel | LoadConfig (G1).
// Rebuilds PlateData::objects_and_instances from obj_inst_map (G2).
// On failure: returns IoResult with exit_code 2 (file_not_found) or 3 (parse_failure).
IoResult load_project(const std::string& path, ProjectState& state);

// Save <state> to <out_path> via atomic temp-write + runtime guard + rename.
// Always uses SaveStrategy::SplitModel (= ProductionExt | 0x1000). Writes
// 128x128 gray RGBA 0xC0 placeholder thumbnails per plate (G3) before save.
// On runtime-guard failure: removes temp file, returns exit_code 8 (invariant_violation).
// On rename failure: returns exit_code 7 (invalid_state).
IoResult save_project(const ProjectState& state, const std::string& out_path);

// Atomic copy of <src> to <dst>. dst.tmp.3mf created, flushed to disk,
// renamed. Used by `project init` for the initial clone.
IoResult atomic_copy(const std::string& src, const std::string& dst);

// Flush <path>'s data to stable storage (_commit on Windows, fsync
// elsewhere). Called on the temp file before the rename swap so a power
// loss right after a "successful" save cannot leave the destination
// pointing at data that never hit the disk. Returns false if the file
// cannot be opened or the flush fails.
bool flush_to_disk(const std::string& path);

} // namespace bambu_cli
