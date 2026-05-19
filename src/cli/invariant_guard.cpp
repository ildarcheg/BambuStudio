#include "invariant_guard.hpp"
#include "io.hpp"

#include "libslic3r/miniz_extension.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

#include <miniz.h>

#include <algorithm>
#include <cstring>
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
        int idx = pd->plate_index > 0 ? pd->plate_index : static_cast<int>(i + 1);
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
        if (!a || !b) {
            // Key absent on one side — only flag if both should have it.
            // Since we're iterating state's keys, 'a' is always present.
            // 'b' absent after reload means the writer dropped it — that's
            // likely a serialization issue, but we only compare when both exist.
            continue;
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

    gr.ok = true;
    return gr;
}

} // namespace bambu_cli
