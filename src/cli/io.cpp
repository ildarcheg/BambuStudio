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

// --- G2 rebuild: PlateData::objects_and_instances from model instances ----
// After bbs_3mf loading, obj_inst_map[key] = (instance_id, identify_id/loaded_id).
// The bbs_3mf reader stamps each ModelInstance::loaded_id = identify_id (bbs_3mf.cpp:2402).
// We rebuild objects_and_instances (which the writer needs as (obj_idx, inst_idx)) by
// matching each model instance's loaded_id against the identify_ids stored in obj_inst_map.
static void rebuild_objects_and_instances(Slic3r::PlateDataPtrs& plates,
                                          const Slic3r::Model& model) {
    // Build: loaded_id -> (obj_idx, inst_idx) from model
    std::map<size_t, std::pair<int,int>> loaded_id_to_loc;
    for (int oi = 0; oi < static_cast<int>(model.objects.size()); ++oi) {
        const auto* obj = model.objects[oi];
        if (!obj) continue;
        for (int ii = 0; ii < static_cast<int>(obj->instances.size()); ++ii) {
            const auto* inst = obj->instances[ii];
            if (!inst || inst->loaded_id == 0) continue;
            loaded_id_to_loc[inst->loaded_id] = {oi, ii};
        }
    }

    for (auto* plate : plates) {
        if (!plate) continue;
        plate->objects_and_instances.clear();
        for (const auto& kv : plate->obj_inst_map) {
            // After bbs_3mf load: kv.second = (instance_id, identify_id/loaded_id)
            size_t loaded_id = static_cast<size_t>(kv.second.second);
            if (loaded_id == 0) continue;
            auto it = loaded_id_to_loc.find(loaded_id);
            if (it != loaded_id_to_loc.end()) {
                plate->objects_and_instances.push_back(it->second);
            }
        }
    }
}

IoResult load_project(const std::string& path, ProjectState& state) {
    IoResult r;
    if (!fs::exists(path)) {
        r.exit_code = to_int(ExitCode::file_not_found); r.error_code = "file_not_found";
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
        r.exit_code = to_int(ExitCode::parse_failure); r.error_code = "parse_failure";
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
        r.exit_code = to_int(ExitCode::invalid_state); r.error_code = "invalid_state";
        r.error_message = "store_bbs_3mf returned false for: " + tmp_path;
        return r;
    }

    // 4. Runtime invariant guard.
    GuardResult gr = run_guard(tmp_path, state);
    if (!gr.ok) {
        fs::remove(tmp_path);
        r.exit_code = to_int(ExitCode::invariant_violation); r.error_code = "invariant_violation";
        r.error_message = "guard check '" + gr.failed_check + "' failed: " + gr.failure_detail;
        return r;
    }

    // 5. Safer atomic .bak-swap: rename dst -> .bak, rename tmp -> dst,
    //    remove .bak. The destination is never absent during the swap; if
    //    the middle rename fails we restore the original from .bak.
    //    Ported from OrcaSlicer/src/cli/io.cpp:467-499 (their "M11 cleanup").
    //    Eliminates the prior window between remove(out_path) and
    //    rename(tmp -> out_path) where a crash would leave no file at the
    //    destination.
    const std::string bak = out_path + ".bak";
    try {
        if (fs::exists(out_path)) {
            fs::remove(bak);   // clean any stale leftover
            fs::rename(out_path, bak);
        }
        try {
            fs::rename(tmp_path, out_path);
        } catch (...) {
            // Best-effort restore: put the original back from .bak.
            if (fs::exists(bak)) {
                boost::system::error_code rec;
                fs::rename(bak, out_path, rec);
            }
            throw;
        }
        if (fs::exists(bak)) {
            boost::system::error_code rm_ec;
            fs::remove(bak, rm_ec);   // best-effort; stale .bak is harmless
        }
    } catch (const std::exception& e) {
        fs::remove(tmp_path);
        r.exit_code = to_int(ExitCode::invalid_state); r.error_code = "invalid_state";
        r.error_message = std::string("rename failed: ") + e.what();
        return r;
    }

    r.ok = true;
    return r;
}

IoResult atomic_copy(const std::string& src, const std::string& dst) {
    IoResult r;
    if (!fs::exists(src)) {
        r.exit_code = to_int(ExitCode::file_not_found); r.error_code = "file_not_found";
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
        r.exit_code = to_int(ExitCode::invalid_state); r.error_code = "invalid_state";
        r.error_message = std::string("atomic_copy failed: ") + e.what();
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace bambu_cli
