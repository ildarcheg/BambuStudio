#pragma once

#include "project_state.hpp"

#include <string>
#include <vector>

namespace bambu_cli {

// Result of running the multi-check guard against a saved .3mf.
struct GuardResult {
    bool        ok = false;
    std::string failed_check;     // "rels", "thumbnails", "config_roundtrip",
                                  // "auxiliary_passthrough", or "cover_references_resolve"
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
//   (d) auxiliary passthrough — every Auxiliaries/* file in
//       state.source_path is present at the same archive path in the
//       saved archive with byte-identical content. (Skipped if
//       state.source_path is empty, e.g. for project_init from
//       template; that case is covered by check_thumbnails_in_archive.)
//   (e) cover references resolve — DesignerCover / ProfileCover
//       metadata reference existing files in the canonical folders.
// Returns ok=true if all pass.
GuardResult run_guard(const std::string& saved_path, const ProjectState& state);

// Standalone check (b) only — runs against an arbitrary archive path with the
// expected plate set inferred from <state>. Used by `project init` to validate
// the cloned template BEFORE save_project regenerates thumbnails (which would
// otherwise mask input corruption). Opens the archive via mz_zip_reader_init_file
// directly to tolerate Windows 8.3-shortname paths (e.g. C:\Users\ILDARC~1\...).
GuardResult check_thumbnails_in_archive(const std::string& archive_path,
                                        const ProjectState& state);

// Post-write check: every regular file under <aux_temp_dir> (recursively)
// must be present in <post_archive> at archive path
// "Auxiliaries/<relative-to-aux_temp_dir>" with byte-identical contents.
// First mismatch is written to *err_out and the function returns false.
// Empty err_out on success. If <aux_temp_dir> doesn't exist or is empty,
// returns true (nothing to verify).
//
// Compares the in-memory aux temp dir (the source-of-truth for what
// store_bbs_3mf walks) against the saved archive. Catches save-path
// bugs (wrong folder name, content corruption, missing entries) without
// false-positiving on legitimate aux mutations (aux add/remove,
// cover embed).
bool check_auxiliary_passthrough(const std::string& aux_temp_dir,
                                 const std::string& post_archive,
                                 std::string* err_out);

// Verify DesignerCover and ProfileCover metadata in <archive_path>'s
// 3D/3dmodel.model reference filenames that exist in Auxiliaries/Model
// Pictures/ and Auxiliaries/Profile Pictures/ respectively. Empty metadata
// values are valid and pass. First mismatch is written to *err_out and
// the function returns false.
bool check_cover_references_resolve(const std::string& archive_path,
                                    std::string* err_out);

} // namespace bambu_cli
