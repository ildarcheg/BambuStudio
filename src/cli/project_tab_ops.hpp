#pragma once

#include "exceptions.hpp"
#include "project_state.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Slic3r { class Model; }

namespace bambu_cli {

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
    std::optional<std::string> cover_path;  // on-disk path; validated PNG/JPEG
    std::optional<std::string> cover_name;  // basename of an existing aux entry in Model Pictures
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
    std::optional<std::string> cover_path;  // on-disk path; validated PNG/JPEG
    std::optional<std::string> cover_name;  // basename of an existing aux entry in Profile Pictures
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
    ModelPictures,      // archive subdir "Model Pictures"
    ProfilePictures,    // archive subdir "Profile Pictures"
    BillOfMaterials,    // archive subdir "Bill of Materials"
    AssemblyGuide,      // archive subdir "Assembly Guide"
    Others,             // archive subdir "Others"
};

// CLI flag spelling (hyphen form) for --folder.
std::string folder_flag(AuxFolder f);

// JSON key spelling (underscore form).
std::string folder_json_key(AuxFolder f);

// Subdirectory name within "Auxiliaries/" in the archive.
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

namespace detail {
    // Returns true if <path> begins with the PNG magic (89 50 4E 47 0D 0A 1A 0A)
    // or the JPEG SOI sequence (FF D8 FF). Returns false on read failure,
    // truncation (<3 bytes), or any other signature.
    bool is_png_or_jpeg(const std::string& path);

    // Embed <on_disk_path> as <basename(on_disk_path)> under the aux temp
    // <folder>. Validates PNG/JPEG signature. Returns the basename written.
    // Throws BadCoverImage on bad signature / unreadable source.
    // <folder> must be ModelPictures or ProfilePictures (anything else throws
    // std::invalid_argument as an internal sanity check; CLI parsing never
    // produces those values for the cover paths).
    std::string embed_image_into_folder(Slic3r::Model& model,
                                        AuxFolder folder,
                                        const std::string& on_disk_path);

    // Throws std::out_of_range if Auxiliaries/<folder>/<basename> is not
    // present in the aux temp dir. Maps to ExitCode::unknown_reference (6)
    // via run_mutation. Takes Model& (not const&) because
    // get_auxiliary_file_temp_path is non-const in libslic3r.
    void require_image_in_folder(Slic3r::Model& model,
                                 AuxFolder folder,
                                 const std::string& basename);
}

} // namespace bambu_cli
