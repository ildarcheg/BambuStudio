#pragma once

#include "project_state.hpp"

#include <string>
#include <vector>

namespace bambu_cli {

// Result of running the three-check guard against a saved .3mf.
struct GuardResult {
    bool        ok = false;
    std::string failed_check;     // "rels", "thumbnails", or "config_roundtrip"
    std::string failure_detail;   // e.g. which entry, which key
};

// Re-open <saved_path> as a zip and run:
//   (a) relationship Target resolution — every <Relationship> Target in
//       _rels/.rels and any *.rels file resolves to an existing archive entry.
//   (b) per-plate thumbnail existence — for every plate N referenced, both
//       Metadata/plate_N.png and Metadata/plate_N_small.png exist.
//   (c) vector-typed config round-trip — re-loaded project_config and
//       per-object configs equal the in-memory <state> for every vector type
//       (coPoint*, coPoints*, coBools, coStrings).
// Returns ok=true if all three pass.
GuardResult run_guard(const std::string& saved_path, const ProjectState& state);

} // namespace bambu_cli
