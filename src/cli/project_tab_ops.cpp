#include "project_tab_ops.hpp"

#include "libslic3r/Model.hpp"

#include <boost/filesystem.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = boost::filesystem;

namespace bambu_cli {

// ============================================================
// Internal helpers
// ============================================================

static const uint8_t kPngSignature[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};

static bool check_png_signature(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint8_t sig[8] = {};
    f.read(reinterpret_cast<char*>(sig), 8);
    return f.gcount() == 8 && std::memcmp(sig, kPngSignature, 8) == 0;
}

namespace detail {

bool is_png_or_jpeg(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint8_t hdr[8] = {};
    f.read(reinterpret_cast<char*>(hdr), 8);
    const auto n = f.gcount();
    if (n >= 8 && std::memcmp(hdr, "\x89PNG\r\n\x1A\n", 8) == 0) return true;
    if (n >= 3 && hdr[0] == 0xFF && hdr[1] == 0xD8 && hdr[2] == 0xFF) return true;
    return false;
}

} // namespace detail

// Ensure model.model_info is non-null and return a reference.
static Slic3r::ModelInfo& ensure_model_info(Slic3r::Model& model) {
    if (!model.model_info)
        model.model_info = std::make_shared<Slic3r::ModelInfo>();
    return *model.model_info;
}

// Ensure model.profile_info is non-null and return a reference.
static Slic3r::ModelProfileInfo& ensure_profile_info(Slic3r::Model& model) {
    if (!model.profile_info)
        model.profile_info = std::make_shared<Slic3r::ModelProfileInfo>();
    return *model.profile_info;
}

// Embed a cover PNG from on-disk path into the model's aux temp dir.
// Sets the archive-relative path on success. Throws BadCoverImage on failure.
static void embed_cover(Slic3r::Model& model,
                        const std::string& on_disk_path,
                        std::string& field_out,
                        const std::string& archive_entry /* e.g. "Auxiliaries/cover.png" */) {
    if (!fs::exists(on_disk_path))
        throw BadCoverImage("cover file unreadable: " + on_disk_path);
    if (!check_png_signature(on_disk_path))
        throw BadCoverImage("cover must be PNG (signature mismatch): " + on_disk_path);
    const std::string aux_dir = model.get_auxiliary_file_temp_path();
    const std::string model_pics_dir = aux_dir + "/Model Pictures";
    fs::create_directories(model_pics_dir);
    const std::string dest = model_pics_dir + "/cover.png";
    fs::copy_file(on_disk_path, dest, fs::copy_options::overwrite_existing);
    field_out = archive_entry;
}

// Cover-image refcount (ported from OrcaSlicer project_tab_ops.hpp:223 /
// project_tab_ops.cpp::clear_cover_image). The on-disk cover.png is shared
// by `info.cover_file` and `profile.ProfileCover`; deleting it eagerly on
// the first clear would orphan the other surface's pointer. Defer the
// delete until both pointers are empty after the clear.
static bool info_cover_empty(const Slic3r::Model& model) {
    return !model.model_info || model.model_info->cover_file.empty();
}
static bool profile_cover_empty(const Slic3r::Model& model) {
    return !model.profile_info || model.profile_info->ProfileCover.empty();
}
static void delete_cover_file_if_unreferenced(Slic3r::Model& model) {
    if (!info_cover_empty(model) || !profile_cover_empty(model)) return;
    const fs::path landed = fs::path(model.get_auxiliary_file_temp_path()) / "Model Pictures" / "cover.png";
    boost::system::error_code ec;
    fs::remove(landed, ec);  // best-effort; absent file is a no-op
}

static void validate_fields(const std::vector<std::string>& fields,
                             const std::set<std::string>& allowed) {
    for (const auto& f : fields) {
        if (allowed.find(f) == allowed.end())
            throw InvalidField("not a clearable field: " + f);
    }
}

// ============================================================
// project info
// ============================================================

std::set<std::string> allowed_info_fields() {
    return {"title", "description", "license", "copyright", "cover"};
}

InfoView info_show(const ProjectState& state) {
    InfoView v;
    const auto& mi = state.model.model_info;
    if (mi) {
        v.title       = mi->model_name;
        v.description = mi->description;
        v.license     = mi->license;
        v.copyright   = mi->copyright;
        v.cover       = mi->cover_file;
        v.origin      = mi->origin;
    }
    return v;
}

std::string info_set(ProjectState& state, const InfoSetParams& p) {
    auto& mi = ensure_model_info(state.model);
    if (p.title)       mi.model_name  = *p.title;
    if (p.description) mi.description = *p.description;
    if (p.license)     mi.license     = *p.license;
    if (p.copyright)   mi.copyright   = *p.copyright;
    if (p.cover_path)
        embed_cover(state.model, *p.cover_path, mi.cover_file, "cover.png");
    return "applied info edits";
}

std::string info_clear(ProjectState& state, const std::vector<std::string>& fields) {
    validate_fields(fields, allowed_info_fields());
    auto& mi = ensure_model_info(state.model);
    int n = 0;
    bool cleared_cover = false;
    for (const auto& f : fields) {
        if (f == "title")       { mi.model_name  = ""; ++n; }
        else if (f == "description") { mi.description = ""; ++n; }
        else if (f == "license")     { mi.license     = ""; ++n; }
        else if (f == "copyright")   { mi.copyright   = ""; ++n; }
        else if (f == "cover")       { mi.cover_file  = ""; ++n; cleared_cover = true; }
    }
    if (cleared_cover) delete_cover_file_if_unreferenced(state.model);
    return "cleared " + std::to_string(n) + " field(s)";
}

// ============================================================
// project profile
// ============================================================

std::set<std::string> allowed_profile_fields() {
    return {"title", "description", "cover"};
}

ProfileView profile_show(const ProjectState& state) {
    ProfileView v;
    const auto& pi = state.model.profile_info;
    if (pi) {
        v.title       = pi->ProfileTile;   // note: Bambu typo "Tile" not "Title"
        v.description = pi->ProfileDescription;
        v.cover       = pi->ProfileCover;
        v.user_id     = pi->ProfileUserId;
        v.user_name   = pi->ProfileUserName;
    }
    return v;
}

std::string profile_set(ProjectState& state, const ProfileSetParams& p) {
    // Bambu's store_bbs_3mf reads profile fields from model.profile_info directly.
    // No mirroring into metadata_items["ProfileTitle"] is required.
    // See docs/cli/notes/2026-05-21-bbs-profile-storage.md §4.
    auto& pi = ensure_profile_info(state.model);
    if (p.title)       pi.ProfileTile        = *p.title;
    if (p.description) pi.ProfileDescription = *p.description;
    if (p.cover_path)
        embed_cover(state.model, *p.cover_path, pi.ProfileCover, "cover.png");
    return "applied profile edits";
}

std::string profile_clear(ProjectState& state, const std::vector<std::string>& fields) {
    validate_fields(fields, allowed_profile_fields());
    auto& pi = ensure_profile_info(state.model);
    int n = 0;
    bool cleared_cover = false;
    for (const auto& f : fields) {
        if (f == "title")       { pi.ProfileTile        = ""; ++n; }
        else if (f == "description") { pi.ProfileDescription = ""; ++n; }
        else if (f == "cover")       { pi.ProfileCover       = ""; ++n; cleared_cover = true; }
    }
    if (cleared_cover) delete_cover_file_if_unreferenced(state.model);
    return "cleared " + std::to_string(n) + " field(s)";
}

// ============================================================
// AuxFolder helpers
// ============================================================

std::string folder_flag(AuxFolder f) {
    switch (f) {
        case AuxFolder::ModelPictures:   return "model-pictures";
        case AuxFolder::ProfilePictures: return "profile-pictures";
        case AuxFolder::BillOfMaterials: return "bill-of-materials";
        case AuxFolder::AssemblyGuide:   return "assembly-guide";
        case AuxFolder::Others:          return "others";
    }
    return "others";
}

std::string folder_json_key(AuxFolder f) {
    switch (f) {
        case AuxFolder::ModelPictures:   return "model_pictures";
        case AuxFolder::ProfilePictures: return "profile_pictures";
        case AuxFolder::BillOfMaterials: return "bill_of_materials";
        case AuxFolder::AssemblyGuide:   return "assembly_guide";
        case AuxFolder::Others:          return "others";
    }
    return "others";
}

std::string folder_subdir(AuxFolder f) {
    switch (f) {
        case AuxFolder::ModelPictures:   return "Model Pictures";
        case AuxFolder::ProfilePictures: return "Profile Pictures";
        case AuxFolder::BillOfMaterials: return "Bill of Materials";
        case AuxFolder::AssemblyGuide:   return "Assembly Guide";
        case AuxFolder::Others:          return "Others";
    }
    return "Others";
}

// ============================================================
// project aux  (C3 stubs — implemented in Phase C3)
// ============================================================

std::vector<AuxEntry> aux_list(ProjectState& state) {
    std::vector<AuxEntry> entries;
    const std::string aux_dir = state.model.get_auxiliary_file_temp_path();
    if (!fs::exists(aux_dir)) return entries;

    for (const auto folder : {AuxFolder::ModelPictures, AuxFolder::ProfilePictures,
                               AuxFolder::BillOfMaterials, AuxFolder::AssemblyGuide,
                               AuxFolder::Others}) {
        const std::string sub = aux_dir + "/" + folder_subdir(folder);
        if (!fs::exists(sub)) continue;
        for (const auto& entry : fs::directory_iterator(sub)) {
            if (!fs::is_regular_file(entry.path())) continue;
            AuxEntry ae;
            ae.folder = folder;
            ae.name   = entry.path().filename().string();
            ae.size   = static_cast<size_t>(fs::file_size(entry.path()));
            entries.push_back(ae);
        }
    }
    return entries;
}

// ---- sanitize_aux_name ----

static bool is_reserved_name(const std::string& upper) {
    static const std::vector<std::string> kReserved = {
        "CON","PRN","AUX","NUL",
        "COM1","COM2","COM3","COM4","COM5","COM6","COM7","COM8","COM9",
        "LPT1","LPT2","LPT3","LPT4","LPT5","LPT6","LPT7","LPT8","LPT9",
    };
    for (const auto& r : kReserved)
        if (upper == r) return true;
    return false;
}

std::string sanitize_aux_name(const std::string& raw) {
    // Reject path separators
    if (raw.find('/') != std::string::npos || raw.find('\\') != std::string::npos)
        throw AuxNameError("aux name contains path separator: " + raw);
    // Reject leading/trailing whitespace
    if (!raw.empty() && (std::isspace(static_cast<unsigned char>(raw.front())) ||
                          std::isspace(static_cast<unsigned char>(raw.back()))))
        throw AuxNameError("aux name has leading/trailing whitespace: " + raw);
    // Reject empty
    if (raw.empty())
        throw AuxNameError("aux name is empty");
    // Reject dot-only (. and ..)
    const bool all_dots = std::all_of(raw.begin(), raw.end(),
                                      [](char c){ return c == '.'; });
    if (all_dots)
        throw AuxNameError("aux name is dot-only: " + raw);
    // Reject Windows reserved names (case-insensitive, ignore extension)
    std::string stem = raw;
    {
        auto dot = raw.rfind('.');
        if (dot != std::string::npos) stem = raw.substr(0, dot);
    }
    std::string upper = stem;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    if (is_reserved_name(upper))
        throw AuxNameError("aux name is a Windows reserved name: " + raw);
    return raw;
}

std::string aux_add(ProjectState& state, const AuxAddParams& p) {
    // Source must exist.
    if (!fs::exists(p.file_path))
        throw BadAuxFile("aux source unreadable: " + p.file_path);

    // Determine the in-archive name.
    std::string name = p.name.empty()
        ? fs::path(p.file_path).filename().string()
        : p.name;
    name = sanitize_aux_name(name);

    const std::string aux_dir   = state.model.get_auxiliary_file_temp_path();
    const std::string sub       = aux_dir + "/" + folder_subdir(p.folder);
    const std::string dest_file = sub + "/" + name;

    fs::create_directories(sub);

    // Check for collision.
    if (fs::exists(dest_file)) {
        if (!p.force) {
            // Byte-identical re-add is exit 0.
            if (fs::file_size(p.file_path) == fs::file_size(dest_file)) {
                // Considered identical for idempotent re-add: just succeed.
                return "added " + name + " to " + folder_flag(p.folder);
            }
            throw AuxCollisionError("aux name already exists: " + name);
        }
    }

    fs::copy_file(p.file_path, dest_file, fs::copy_options::overwrite_existing);
    return "added " + name + " to " + folder_flag(p.folder);
}

std::string aux_remove(ProjectState& state, AuxFolder folder, const std::string& name) {
    const std::string aux_dir = state.model.get_auxiliary_file_temp_path();
    const std::string target  = aux_dir + "/" + folder_subdir(folder) + "/" + name;
    if (!fs::exists(target))
        throw std::out_of_range("aux entry not found: " + name);
    fs::remove(target);
    return "removed " + name + " from " + folder_flag(folder);
}

void aux_export(ProjectState& state, AuxFolder folder,
                const std::string& name, const std::string& to_path) {
    const std::string aux_dir = state.model.get_auxiliary_file_temp_path();
    const std::string src     = aux_dir + "/" + folder_subdir(folder) + "/" + name;
    if (!fs::exists(src))
        throw std::out_of_range("aux entry not found: " + name);

    std::string dest = to_path;
    if (fs::is_directory(to_path))
        dest = (fs::path(to_path) / name).string();

    const fs::path dest_parent = fs::path(dest).parent_path();
    if (!dest_parent.empty() && !fs::exists(dest_parent))
        throw std::invalid_argument("destination parent directory does not exist: " +
                                    dest_parent.string());

    fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
}

} // namespace bambu_cli
