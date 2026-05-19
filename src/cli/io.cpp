#include "io.hpp"
#include "invariant_guard.hpp"

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/miniz_extension.hpp"

#include <miniz.h>
#include <boost/filesystem.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = boost::filesystem;

namespace bambu_cli {

ProjectState::~ProjectState() {
    for (auto* p : plate_data) delete p;
    plate_data.clear();
}

// --- LoadStrategy combinator (enum class — operators are defined in bbs_3mf.hpp)
static Slic3r::LoadStrategy load_model_and_config() {
    return Slic3r::LoadStrategy::LoadModel | Slic3r::LoadStrategy::LoadConfig;
}

// --- G2 rebuild: PlateData::objects_and_instances from obj_inst_map ------
static void rebuild_objects_and_instances(Slic3r::PlateDataPtrs& plates,
                                          const Slic3r::Model& model) {
    for (auto* plate : plates) {
        if (!plate) continue;
        plate->objects_and_instances.clear();
        for (const auto& kv : plate->obj_inst_map) {
            const auto& pr = kv.second;   // (obj_idx, inst_idx)
            // Validate before pushing.
            if (pr.first  < 0 || pr.first  >= static_cast<int>(model.objects.size())) continue;
            const auto* obj = model.objects[pr.first];
            if (!obj) continue;
            if (pr.second < 0 || pr.second >= static_cast<int>(obj->instances.size())) continue;
            plate->objects_and_instances.push_back(pr);
        }
    }
}

IoResult load_project(const std::string& path, ProjectState& state) {
    IoResult r;
    if (!fs::exists(path)) {
        r.exit_code = 2; r.error_code = "file_not_found";
        r.error_message = "project file not found: " + path;
        return r;
    }
    Slic3r::ConfigSubstitutionContext subs(Slic3r::ForwardCompatibilitySubstitutionRule::Disable);
    bool is_bbl = false;
    Slic3r::Semver ver;
    std::vector<Slic3r::Preset*> presets_ignored;
    bool ok = Slic3r::load_bbs_3mf(
        path.c_str(), &state.project_config, &subs, &state.model,
        &state.plate_data, &presets_ignored, &is_bbl, &ver,
        /*proFn*/ nullptr, load_model_and_config(),
        /*project*/ nullptr, /*plate_id*/ 0,
        /*color_group_map*/ nullptr, /*volume_color_data*/ nullptr);
    if (!ok) {
        r.exit_code = 3; r.error_code = "parse_failure";
        r.error_message = "load_bbs_3mf returned false for: " + path;
        return r;
    }
    rebuild_objects_and_instances(state.plate_data, state.model);
    r.ok = true;
    return r;
}

// --- Placeholder thumbnail (128x128 gray RGBA 0xC0; G3) --------------------
// The bbs_3mf writer accepts ThumbnailData (raw RGBA), not PNG bytes.
// We populate raw RGBA directly.
static void fill_placeholder_thumbnail(Slic3r::ThumbnailData& td) {
    td.width  = 128;
    td.height = 128;
    td.pixels.resize(static_cast<size_t>(td.width) * td.height * 4);
    std::memset(td.pixels.data(), 0xC0, td.pixels.size());   // gray RGBA
}

IoResult save_project(const ProjectState& state, const std::string& out_path) {
    IoResult r;

    // 0. Pre-save thumbnail guard: check (b) on the existing out_path (e.g. the
    //    atomically-copied template) before we overwrite it. A template that is
    //    missing Metadata/plate_N.png or plate_N_small.png for a referenced plate
    //    fails here with exit 8 (invariant_violation / thumbnails). We run only
    //    check (b) — not the full 3-check guard — so that the error always says
    //    "thumbnails" rather than "rels" when thumbnails are the issue. The full
    //    guard (all three checks) runs post-save as usual.
    if (fs::exists(out_path)) {
        mz_zip_archive pre_zip;
        std::memset(&pre_zip, 0, sizeof(pre_zip));
        // Use mz_zip_reader_init_file directly (not Slic3r::open_zip_reader which
        // uses boost::nowide::fopen and may fail on 8.3 shortname paths in TEMP).
        if (mz_zip_reader_init_file(&pre_zip, out_path.c_str(), 0)) {
            std::vector<std::string> pre_entries;
            mz_uint n = mz_zip_reader_get_num_files(&pre_zip);
            for (mz_uint i = 0; i < n; ++i) {
                char buf[1024]; mz_zip_reader_get_filename(&pre_zip, i, buf, sizeof(buf));
                pre_entries.emplace_back(buf);
            }
            mz_zip_reader_end(&pre_zip);

            std::set<std::string> entry_set(pre_entries.begin(), pre_entries.end());
            for (size_t i = 0; i < state.plate_data.size(); ++i) {
                const Slic3r::PlateData* pd = state.plate_data[i];
                if (!pd) continue;
                int idx = pd->plate_index > 0 ? pd->plate_index : static_cast<int>(i + 1);
                std::string big   = "Metadata/plate_" + std::to_string(idx) + ".png";
                std::string small = "Metadata/plate_" + std::to_string(idx) + "_small.png";
                if (entry_set.find(big) == entry_set.end()) {
                    r.exit_code = 8; r.error_code = "invariant_violation";
                    r.error_message = "guard check 'thumbnails' failed (source): missing " + big;
                    return r;
                }
                if (entry_set.find(small) == entry_set.end()) {
                    r.exit_code = 8; r.error_code = "invariant_violation";
                    r.error_message = "guard check 'thumbnails' failed (source): missing " + small;
                    return r;
                }
            }
        }
    }

    // 1. Atomic temp path
    const std::string tmp_path = out_path + ".tmp.3mf";
    fs::remove(tmp_path);   // clean any stale leftover

    // 2. Populate per-plate thumbnails (G3).
    // ThumbnailData is populated on each PlateData and also passed as
    // store_params.thumbnail_data per plate. We populate both to satisfy
    // the writer's relationship-emission logic.
    std::vector<Slic3r::ThumbnailData> placeholders(state.plate_data.size());
    std::vector<Slic3r::ThumbnailData*> thumb_ptrs;
    thumb_ptrs.reserve(state.plate_data.size());
    for (size_t i = 0; i < state.plate_data.size(); ++i) {
        fill_placeholder_thumbnail(placeholders[i]);
        thumb_ptrs.push_back(&placeholders[i]);
        if (state.plate_data[i]) {
            // Also stamp the PlateData's own thumbnail so non-store paths agree.
            state.plate_data[i]->plate_thumbnail = placeholders[i];
        }
    }

    // 3. Build StoreParams.
    Slic3r::StoreParams sp;
    sp.path             = tmp_path.c_str();
    sp.model            = const_cast<Slic3r::Model*>(&state.model);
    sp.plate_data_list  = const_cast<Slic3r::PlateDataPtrs&>(state.plate_data);
    sp.config           = const_cast<Slic3r::DynamicPrintConfig*>(&state.project_config);
    sp.thumbnail_data   = thumb_ptrs;
    sp.strategy         = Slic3r::SaveStrategy::SplitModel;  // SplitModel == 0x1000 | ProductionExt

    bool ok = Slic3r::store_bbs_3mf(sp);
    if (!ok) {
        fs::remove(tmp_path);
        r.exit_code = 7; r.error_code = "invalid_state";
        r.error_message = "store_bbs_3mf returned false for: " + tmp_path;
        return r;
    }

    // 4. Runtime invariant guard.
    GuardResult gr = run_guard(tmp_path, state);
    if (!gr.ok) {
        fs::remove(tmp_path);
        r.exit_code = 8; r.error_code = "invariant_violation";
        r.error_message = "guard check '" + gr.failed_check + "' failed: " + gr.failure_detail;
        return r;
    }

    // 5. Atomic rename. boost::filesystem::rename is atomic on same FS.
    try {
        if (fs::exists(out_path)) fs::remove(out_path);
        fs::rename(tmp_path, out_path);
    } catch (const std::exception& e) {
        fs::remove(tmp_path);
        r.exit_code = 7; r.error_code = "invalid_state";
        r.error_message = std::string("rename failed: ") + e.what();
        return r;
    }

    r.ok = true;
    return r;
}

IoResult atomic_copy(const std::string& src, const std::string& dst) {
    IoResult r;
    if (!fs::exists(src)) {
        r.exit_code = 2; r.error_code = "file_not_found";
        r.error_message = "template not found: " + src;
        return r;
    }
    const std::string tmp = dst + ".tmp.3mf";
    fs::remove(tmp);
    try {
        fs::copy_file(src, tmp, fs::copy_options::overwrite_existing);
        if (fs::exists(dst)) fs::remove(dst);
        fs::rename(tmp, dst);
    } catch (const std::exception& e) {
        fs::remove(tmp);
        r.exit_code = 7; r.error_code = "invalid_state";
        r.error_message = std::string("atomic_copy failed: ") + e.what();
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace bambu_cli
