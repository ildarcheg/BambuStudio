#include "invariant_guard.hpp"
#include "io.hpp"

#include "libslic3r/miniz_extension.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

#include <boost/filesystem.hpp>
#include <miniz.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <regex>

namespace bambu_cli {

// ---- zip helpers -----------------------------------------------------------
static bool zip_open(const std::string& path, mz_zip_archive& zip) {
    std::memset(&zip, 0, sizeof(zip));
    return Slic3r::open_zip_reader(&zip, path);
}
static void zip_close(mz_zip_archive& zip) { Slic3r::close_zip_reader(&zip); }
static std::vector<std::string> zip_entries(mz_zip_archive& zip) {
    std::vector<std::string> out;
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        char buf[1024]; mz_zip_reader_get_filename(&zip, i, buf, sizeof(buf));
        out.emplace_back(buf);
    }
    return out;
}
static std::string zip_read_string(mz_zip_archive& zip, const std::string& name) {
    int idx = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
    if (idx < 0) return {};
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, idx, &st)) return {};
    std::string s(static_cast<size_t>(st.m_uncomp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(&zip, idx, &s[0], s.size(), 0)) return {};
    return s;
}

// ---- check (a): every <Relationship> Target resolves to an entry ----------
static bool check_rels(mz_zip_archive& zip, const std::vector<std::string>& entries,
                       GuardResult& gr) {
    std::set<std::string> entry_set(entries.begin(), entries.end());

    // Collect every .rels file in the archive.
    std::vector<std::string> rels_files;
    for (const auto& e : entries) {
        if (e.size() >= 5 && e.compare(e.size()-5, 5, ".rels") == 0) {
            rels_files.push_back(e);
        }
    }
    // Must include the root.
    if (std::find(rels_files.begin(), rels_files.end(), "_rels/.rels") == rels_files.end()) {
        gr.failed_check = "rels";
        gr.failure_detail = "missing _rels/.rels";
        return false;
    }

    // Parse Target="..." attributes naively via regex (3MF rels files are flat XML).
    // Note: use named raw-string delimiter to avoid conflict with )" inside the pattern.
    static const std::regex re(R"re(Target\s*=\s*"([^"]+)")re");
    for (const auto& rf : rels_files) {
        std::string body = zip_read_string(zip, rf);
        if (body.empty()) {
            // Not necessarily an error — could be an empty rels file. Skip.
            continue;
        }
        auto begin = std::sregex_iterator(body.begin(), body.end(), re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string tgt = (*it)[1].str();
            // Targets may be relative paths starting with '/'; archive entries do not.
            if (!tgt.empty() && tgt[0] == '/') tgt.erase(0, 1);
            if (entry_set.find(tgt) == entry_set.end()) {
                gr.failed_check = "rels";
                gr.failure_detail = "Target '" + tgt + "' in " + rf + " not in archive";
                return false;
            }
        }
    }
    return true;
}

// ---- check (b): per-plate thumbnails present ------------------------------
static bool check_thumbnails(const std::vector<std::string>& entries,
                             const ProjectState& state, GuardResult& gr) {
    std::set<std::string> entry_set(entries.begin(), entries.end());
    for (size_t i = 0; i < state.plate_data.size(); ++i) {
        const Slic3r::PlateData* pd = state.plate_data[i];
        if (!pd) continue;
        // The writer names thumbnail entries by *position*, 1-based: the
        // plate at vector position i gets plate_<i+1>.png regardless of
        // pd->plate_index (bbs_3mf.cpp:6309). Look up by position, or a
        // missing last-plate thumbnail goes undetected.
        int idx = static_cast<int>(i + 1);
        std::string big   = "Metadata/plate_" + std::to_string(idx) + ".png";
        std::string small = "Metadata/plate_" + std::to_string(idx) + "_small.png";
        if (entry_set.find(big) == entry_set.end()) {
            gr.failed_check = "thumbnails";
            gr.failure_detail = "missing " + big;
            return false;
        }
        if (entry_set.find(small) == entry_set.end()) {
            gr.failed_check = "thumbnails";
            gr.failure_detail = "missing " + small;
            return false;
        }
    }
    return true;
}

// ---- check (c): vector-typed config round-trip ----------------------------
static bool is_vector_type(Slic3r::ConfigOptionType t) {
    using namespace Slic3r;
    switch (t) {
        case coFloats: case coFloatsOrPercents: case coInts: case coPercents:
        case coBools: case coPoints: case coStrings: case coEnums:
            return true;
        default:
            return false;
    }
}

static bool check_config_roundtrip(const std::string& saved_path,
                                   const ProjectState& state, GuardResult& gr) {
    // Re-load the file's project_config.
    ProjectState reloaded;
    auto lr = load_project(saved_path, reloaded);
    if (!lr.ok) {
        gr.failed_check = "config_roundtrip";
        gr.failure_detail = "re-load failed: " + lr.error_message;
        return false;
    }

    // Compare every vector-typed key in state.project_config.
    for (const std::string& key : state.project_config.keys()) {
        const Slic3r::ConfigOptionDef* def = Slic3r::print_config_def.get(key);
        if (!def) continue;
        if (!is_vector_type(def->type)) continue;
        const Slic3r::ConfigOption* a = state.project_config.option(key);
        const Slic3r::ConfigOption* b = reloaded.project_config.option(key);
        if (!a) continue;   // defensive; iterating state's keys, 'a' should exist
        if (!b) {
            // Key absent after reload: the writer dropped it. Detecting
            // exactly this is half of what a roundtrip check is for —
            // value drift is the other half (below).
            gr.failed_check = "config_roundtrip";
            gr.failure_detail = "key '" + key +
                                "' dropped by writer (present before save, "
                                "absent after reload)";
            return false;
        }
        if (a->serialize() != b->serialize()) {
            gr.failed_check = "config_roundtrip";
            gr.failure_detail = "key '" + key + "' mismatch after save (in='" +
                                a->serialize() + "', out='" + b->serialize() + "')";
            return false;
        }
    }
    return true;
}

GuardResult check_thumbnails_in_archive(const std::string& archive_path,
                                        const ProjectState& state) {
    GuardResult gr;
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    // Use mz_zip_reader_init_file directly (not Slic3r::open_zip_reader which
    // uses boost::nowide::fopen and may fail on 8.3 shortname paths in TEMP).
    if (!mz_zip_reader_init_file(&zip, archive_path.c_str(), 0)) {
        gr.failed_check = "thumbnails";
        gr.failure_detail = "could not open archive: " + archive_path;
        return gr;
    }
    std::vector<std::string> entries;
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        char buf[1024]; mz_zip_reader_get_filename(&zip, i, buf, sizeof(buf));
        entries.emplace_back(buf);
    }
    mz_zip_reader_end(&zip);

    if (check_thumbnails(entries, state, gr)) gr.ok = true;
    return gr;
}

GuardResult run_guard(const std::string& saved_path, const ProjectState& state) {
    GuardResult gr;
    mz_zip_archive zip;
    if (!zip_open(saved_path, zip)) {
        gr.failed_check = "rels";
        gr.failure_detail = "could not open saved zip: " + saved_path;
        return gr;
    }
    auto entries = zip_entries(zip);
    bool ok_rels = check_rels(zip, entries, gr);
    bool ok_thumbs = ok_rels && check_thumbnails(entries, state, gr);
    zip_close(zip);
    if (!ok_rels || !ok_thumbs) return gr;

    if (!check_config_roundtrip(saved_path, state, gr)) return gr;

    // (d) auxiliary passthrough — compares the in-memory aux temp dir
    // (what store_bbs_3mf walks) against the saved archive. Catches save-path
    // bugs without false-positiving on intentional aux mutations.
    {
        // get_auxiliary_file_temp_path is non-const in libslic3r; const_cast is
        // the established pattern in this file's callers (see io.cpp where
        // store_bbs_3mf gets a const_cast Model*).
        const std::string aux_dir =
            const_cast<Slic3r::Model&>(state.model).get_auxiliary_file_temp_path();
        std::string ax_err;
        if (!check_auxiliary_passthrough(aux_dir, saved_path, &ax_err)) {
            gr.failed_check   = "auxiliary_passthrough";
            gr.failure_detail = ax_err;
            return gr;
        }
    }

    // (e) cover references resolve.
    {
        std::string cov_err;
        if (!check_cover_references_resolve(saved_path, &cov_err)) {
            gr.failed_check   = "cover_references_resolve";
            gr.failure_detail = cov_err;
            return gr;
        }
    }

    gr.ok = true;
    return gr;
}

// ---- check_auxiliary_passthrough -------------------------------------------

bool check_auxiliary_passthrough(const std::string& aux_temp_dir,
                                 const std::string& post_archive,
                                 std::string* err_out) {
    namespace fs = boost::filesystem;

    auto fail = [err_out](std::string msg) {
        if (err_out) *err_out = std::move(msg);
        return false;
    };
    if (err_out) err_out->clear();

    // Empty / nonexistent aux temp dir = nothing to verify.
    if (aux_temp_dir.empty() || !fs::exists(aux_temp_dir) ||
        !fs::is_directory(aux_temp_dir))
        return true;

    mz_zip_archive post{};
    if (!zip_open(post_archive, post))
        return fail("cannot open post archive: " + post_archive);
    struct ScopedClose {
        mz_zip_archive* z;
        ~ScopedClose() { zip_close(*z); }
    } close_post{&post};

    const fs::path root(aux_temp_dir);
    boost::system::error_code walk_ec;
    for (fs::recursive_directory_iterator it(root, walk_ec), end;
         it != end; it.increment(walk_ec)) {
        if (walk_ec) return fail("walk error: " + walk_ec.message());
        const fs::path& p = it->path();
        if (!fs::is_regular_file(p)) continue;

        // Compute relative path; normalize separators to '/'.
        const fs::path rel = fs::relative(p, root);
        std::string rel_str = rel.generic_string();   // forward slashes
        const std::string archive_path = "Auxiliaries/" + rel_str;

        const int idx = mz_zip_reader_locate_file(&post, archive_path.c_str(),
                                                  nullptr, 0);
        if (idx < 0) return fail("missing in post: " + archive_path);

        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&post, static_cast<mz_uint>(idx), &st))
            return fail("stat failed for post: " + archive_path);

        // Read both sides into memory and compare.
        std::ifstream in(p.string(), std::ios::binary);
        if (!in) return fail("cannot open temp file: " + p.string());
        std::string disk_bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());

        std::string post_bytes(static_cast<size_t>(st.m_uncomp_size), '\0');
        if (st.m_uncomp_size > 0 &&
            !mz_zip_reader_extract_to_mem(&post, static_cast<mz_uint>(idx),
                                          &post_bytes[0],
                                          static_cast<size_t>(st.m_uncomp_size), 0))
            return fail("read failed for post: " + archive_path);

        if (disk_bytes != post_bytes)
            return fail("content drift: " + archive_path);
    }
    return true;
}

// ---- check_cover_references_resolve ----------------------------------------

// extract_metadata is a by-construction substring matcher, NOT a general
// XML parser. It assumes Bambu's exact output shape for <metadata> elements:
// (a) attribute order is `name="KEY"` first; (b) the value follows immediately
// after the closing `">` of the name attribute; (c) the closing tag is the
// literal "</metadata>" on the same logical text run. Reordered attributes,
// XML comments inside the element, or namespace prefixes would all defeat
// this. Acceptable here because we only run it on archives that
// store_bbs_3mf itself just produced (see src/libslic3r/Format/bbs_3mf.cpp).
static std::string extract_metadata(const std::string& xml, const std::string& key) {
    const std::string needle = "name=\"" + key + "\">";
    const auto p = xml.find(needle);
    if (p == std::string::npos) return "";
    const auto start = p + needle.size();
    const auto end   = xml.find("</metadata>", start);
    if (end == std::string::npos) return "";
    return xml.substr(start, end - start);
}

bool check_cover_references_resolve(const std::string& archive_path,
                                    std::string* err_out) {
    auto fail = [err_out](std::string msg) {
        if (err_out) *err_out = std::move(msg);
        return false;
    };
    if (err_out) err_out->clear();

    mz_zip_archive zip{};
    if (!zip_open(archive_path, zip))
        return fail("cannot open archive: " + archive_path);
    struct ScopedClose {
        mz_zip_archive* z;
        ~ScopedClose() { zip_close(*z); }
    } close_zip{&zip};

    const int idx_model = mz_zip_reader_locate_file(&zip, "3D/3dmodel.model", nullptr, 0);
    if (idx_model < 0) return fail("missing 3D/3dmodel.model");

    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(idx_model), &st))
        return fail("stat failed for 3D/3dmodel.model");
    std::string model_xml(static_cast<size_t>(st.m_uncomp_size), '\0');
    if (st.m_uncomp_size > 0 &&
        !mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(idx_model),
                                      &model_xml[0],
                                      static_cast<size_t>(st.m_uncomp_size), 0))
        return fail("read failed for 3D/3dmodel.model");

    const std::string designer = extract_metadata(model_xml, "DesignerCover");
    const std::string profile  = extract_metadata(model_xml, "ProfileCover");

    auto check_one = [&](const std::string& meta_key,
                         const std::string& subdir,
                         const std::string& basename) -> bool {
        if (basename.empty()) return true;
        const std::string target = "Auxiliaries/" + subdir + "/" + basename;
        const int idx = mz_zip_reader_locate_file(&zip, target.c_str(), nullptr, 0);
        if (idx < 0)
            return fail(meta_key + " references missing entry: " + basename
                        + " (expected at " + target + ")");
        return true;
    };

    if (!check_one("DesignerCover", "Model Pictures",   designer)) return false;
    if (!check_one("ProfileCover",  "Profile Pictures", profile))  return false;
    return true;
}

} // namespace bambu_cli
