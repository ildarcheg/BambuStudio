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

// Standalone check (b) only — runs against an arbitrary archive path with the
// expected plate set inferred from <state>. Used by `project init` to validate
// the cloned template BEFORE save_project regenerates thumbnails (which would
// otherwise mask input corruption). Opens the archive via mz_zip_reader_init_file
// directly to tolerate Windows 8.3-shortname paths (e.g. C:\Users\ILDARC~1\...).
GuardResult check_thumbnails_in_archive(const std::string& archive_path,
                                        const ProjectState& state);

// Post-write check: every regular file under "Auxiliaries/" in <pre_path>
// must be present at the same archive path in <post_path> with
// byte-identical contents. First mismatch is written to *err_out and the
// function returns false. Empty err_out on success.
//
// Pre = the source archive that was loaded; post = the tmp archive that
// store_bbs_3mf just produced. Used to detect accidental aux folder
// renames, missing files, or content corruption.
bool check_auxiliary_passthrough(const std::string& pre_path,
                                 const std::string& post_path,
                                 std::string* err_out);

} // namespace bambu_cli
