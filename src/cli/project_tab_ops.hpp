#pragma once

#include "exceptions.hpp"
#include "project_state.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace bambu_cli {

// ============================================================
// Typed exceptions (C1 / C3)
// Derive from the Phase-A base classes so run_mutation's built-in
// dynamic_cast dispatch picks them up without a per-call override map.
// ============================================================

// Cover image is not a PNG (signature mismatch) or path is unreadable. exit 4.
class BadCoverImage : public BadConfigError {
public:
    using BadConfigError::BadConfigError;
};

// Field name not in the allowed whitelist for info clear or profile clear. exit 4.
class InvalidField : public BadConfigError {
public:
    using BadConfigError::BadConfigError;
};

// Aux source file does not exist / unreadable. exit 2.
class BadAuxFile : public FileNotFoundError {
public:
    using FileNotFoundError::FileNotFoundError;
};

// Sanitized name is invalid (path separators, dot-only, whitespace, reserved). exit 4.
class AuxNameError : public BadConfigError {
public:
    using BadConfigError::BadConfigError;
};

// Target name already exists in the folder and --force was not given. exit 5.
class AuxCollisionError : public DuplicateNameError {
public:
    using DuplicateNameError::DuplicateNameError;
};

// ============================================================
// project info
// ============================================================

struct InfoView {
    std::string title;
    std::string description;
    std::string license;
    std::string copyright;
    std::string cover;   // archive-relative path string, or empty
    std::string origin;  // read-only
};

struct InfoSetParams {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> license;
    std::optional<std::string> copyright;
    std::optional<std::string> cover_path;  // on-disk path; validated as PNG
};

// Field names accepted by info_clear.
std::set<std::string> allowed_info_fields();

// Read-only: returns InfoView populated from state.model.model_info.
InfoView info_show(const ProjectState& state);

// Mutating: apply InfoSetParams. PNG signature is validated before embedding.
// Throws BadCoverImage if cover is not a PNG or is unreadable.
// Returns a success message string for emit_ok.
std::string info_set(ProjectState& state, const InfoSetParams& p);

// Mutating: clear each named field. Unknown field throws InvalidField.
// Returns a success message.
std::string info_clear(ProjectState& state, const std::vector<std::string>& fields);

// ============================================================
// project profile
// ============================================================

struct ProfileView {
    std::string title;        // writable: ProfileTile (note Bambu typo)
    std::string description;  // writable: ProfileDescription
    std::string cover;        // writable: ProfileCover (archive-relative path)
    std::string user_id;      // read-only: ProfileUserId
    std::string user_name;    // read-only: ProfileUserName
};

struct ProfileSetParams {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> cover_path;  // on-disk path; validated as PNG
};

// Field names accepted by profile_clear.
std::set<std::string> allowed_profile_fields();

// Read-only.
ProfileView profile_show(const ProjectState& state);

// Mutating. Throws BadCoverImage on bad cover.
// NOTE: Bambu's store_bbs_3mf reads from model.profile_info directly; no
// mirroring into metadata_items["ProfileTitle"] is needed (unlike Orca).
// See docs/cli/notes/2026-05-21-bbs-profile-storage.md §4.
std::string profile_set(ProjectState& state, const ProfileSetParams& p);

// Mutating. Unknown field throws InvalidField.
std::string profile_clear(ProjectState& state, const std::vector<std::string>& fields);

// ============================================================
// project aux  (C3 — declarations only; ops implemented in C3)
// ============================================================

enum class AuxFolder {
    Pictures,
    Bom,
    AssemblyGuide,
    Others,
};

// CLI flag spelling (hyphen form) for --folder.
std::string folder_flag(AuxFolder f);

// JSON key spelling (underscore form).
std::string folder_json_key(AuxFolder f);

// Subdirectory name within "Auxiliary/" in the archive.
std::string folder_subdir(AuxFolder f);

struct AuxEntry {
    AuxFolder   folder;
    std::string name;
    size_t      size = 0;
};

struct AuxAddParams {
    std::string input_path;   // positional <input> on CLI
    AuxFolder   folder        = AuxFolder::Others;
    std::string file_path;    // --file PATH
    std::string name;         // --name N (overrides basename of file_path)
    bool        force         = false;
    std::string output_path;  // --output O
};

// Returns all aux entries grouped across all four folders.
std::vector<AuxEntry> aux_list(ProjectState& state);

// Throws BadAuxFile / AuxNameError / AuxCollisionError as appropriate.
std::string aux_add(ProjectState& state, const AuxAddParams& p);

// Unknown name -> throws std::out_of_range (-> exit 6).
std::string aux_remove(ProjectState& state, AuxFolder folder, const std::string& name);

// Export to path. If path is an existing directory, file goes to path/<name>.
// Missing parent -> throws std::invalid_argument (-> exit 7).
void aux_export(ProjectState& state, AuxFolder folder,
                const std::string& name, const std::string& to_path);

// Validate and sanitize an aux entry name. Returns the sanitized name or
// throws AuxNameError on rejection. Reserved name set (case-insensitive):
// CON PRN AUX NUL COM1-COM9 LPT1-LPT9.
std::string sanitize_aux_name(const std::string& raw);

} // namespace bambu_cli
