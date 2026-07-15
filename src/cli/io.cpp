#include "io.hpp"
#include "invariant_guard.hpp"
#include "png_placeholder.hpp"

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
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = boost::filesystem;

namespace bambu_cli {

// Best-effort scratch cleanup: never throws (error_code overload). Used on
// error paths where a failing remove must not mask the original error —
// io-boundary calls run outside every try block in the command envelope,
// so a throwing overload here would escape straight to std::terminate.
static void remove_quiet(const std::string& path) {
    boost::system::error_code ec;
    fs::remove(path, ec);
}

// Clear a stale scratch path without throwing. Non-recursive on purpose:
// if a foreign directory tree squats on the scratch path it is not ours
// to delete — report failure and let the caller surface an IoResult.
// Returns true when the path is gone afterwards.
static bool clear_stale_scratch(const std::string& path,
                                std::string& detail) {
    boost::system::error_code rm_ec, ex_ec;
    fs::remove(path, rm_ec);
    if (fs::exists(path, ex_ec)) {
        detail = "cannot clear stale temp path: " + path +
                 (rm_ec ? " (" + rm_ec.message() + ")" : "");
        return false;
    }
    return true;
}

ProjectState::~ProjectState() {
    for (auto* p : plate_data) delete p;
    plate_data.clear();
}

// --- LoadStrategy combinator (enum class — operators are defined in bbs_3mf.hpp)
// LoadAuxiliary is included so auxiliary files (cover images, aux add/remove/list
// entries) are extracted to the model's aux temp dir on every load. Without this,
// aux files would only exist in the current process's temp dir and be lost between
// CLI invocations. Existing operations (plate/object/config) are unaffected:
// the aux temp dir is empty for projects without aux files.
static Slic3r::LoadStrategy load_model_and_config() {
    return Slic3r::LoadStrategy::LoadModel | Slic3r::LoadStrategy::LoadConfig
           | Slic3r::LoadStrategy::LoadAuxiliary;
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
    boost::system::error_code ex_ec;
    if (!fs::exists(path, ex_ec)) {
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
    state.source_path = path;
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

// --- Thumbnail passthrough (B.2) -------------------------------------------
// After store_bbs_3mf writes the output archive, replace each plate thumbnail
// entry:
//   - If source archive has the matching entry: zero-copy (mz_zip_writer_add_from_zip_reader).
//   - Otherwise: inject make_placeholder_png_128() as a well-formed PNG.
//
// The source entry for plate at position i is looked up by plate_index+1
// (which equals the plater_id as loaded from the source .3mf). The output
// entry name is position-based (i+1) to match how store_bbs_3mf names them.
// For plates whose plate_index was compacted after a remove, the passthrough
// may fall back to synthesis — acceptable for Phase B scope.
static bool rewrite_thumbnails(const std::string& archive_path,
                                const std::string& source_path,
                                const Slic3r::PlateDataPtrs& plate_data) {
    // Build map: output entry name -> source candidate entry name
    std::map<std::string, std::string> passthrough;
    for (size_t i = 0; i < plate_data.size(); ++i) {
        const auto* pd = plate_data[i];
        if (!pd) continue;
        std::string out_key   = "Metadata/plate_" + std::to_string(i + 1);
        std::string src_key   = "Metadata/plate_" + std::to_string(pd->plate_index + 1);
        passthrough[out_key + ".png"]       = src_key + ".png";
        passthrough[out_key + "_small.png"] = src_key + "_small.png";
    }

    // Open source archive (optional)
    mz_zip_archive src_zip;
    std::memset(&src_zip, 0, sizeof(src_zip));
    bool has_source = !source_path.empty() && fs::exists(source_path)
                   && mz_zip_reader_init_file(&src_zip, source_path.c_str(), 0);

    // Build name->index map for source
    std::map<std::string, mz_uint> src_idx;
    if (has_source) {
        mz_uint n = mz_zip_reader_get_num_files(&src_zip);
        for (mz_uint i = 0; i < n; ++i) {
            char name[512];
            mz_zip_reader_get_filename(&src_zip, i, name, sizeof(name));
            src_idx[name] = i;
        }
    }

    // Open the store_bbs_3mf output as reader
    mz_zip_archive out_zip;
    std::memset(&out_zip, 0, sizeof(out_zip));
    if (!mz_zip_reader_init_file(&out_zip, archive_path.c_str(), 0)) {
        if (has_source) mz_zip_reader_end(&src_zip);
        return false;
    }

    // Create rewritten archive
    const std::string new_path = archive_path + ".pass_tmp";
    mz_zip_archive new_zip;
    std::memset(&new_zip, 0, sizeof(new_zip));
    if (!mz_zip_writer_init_file(&new_zip, new_path.c_str(), 0)) {
        mz_zip_reader_end(&out_zip);
        if (has_source) mz_zip_reader_end(&src_zip);
        return false;
    }

    // Pre-generate placeholder PNG (used for any synthesized thumbnail)
    const auto placeholder_png = make_placeholder_png_128();

    bool ok = true;
    mz_uint n = mz_zip_reader_get_num_files(&out_zip);
    for (mz_uint i = 0; i < n && ok; ++i) {
        char name[512];
        mz_zip_reader_get_filename(&out_zip, i, name, sizeof(name));
        const std::string entry(name);

        auto pt_it = passthrough.find(entry);
        if (pt_it != passthrough.end()) {
            // Plate thumbnail: passthrough from source or synthesize
            auto src_it = src_idx.find(pt_it->second);
            if (src_it != src_idx.end()) {
                ok = !!mz_zip_writer_add_from_zip_reader(&new_zip, &src_zip, src_it->second);
            } else {
                ok = !!mz_zip_writer_add_mem(&new_zip, entry.c_str(),
                                             placeholder_png.data(),
                                             placeholder_png.size(),
                                             MZ_NO_COMPRESSION);
            }
        } else {
            // Non-thumbnail entry: zero-copy from output archive
            ok = !!mz_zip_writer_add_from_zip_reader(&new_zip, &out_zip, i);
        }
    }

    if (ok) ok = !!mz_zip_writer_finalize_archive(&new_zip);
    mz_zip_writer_end(&new_zip);
    mz_zip_reader_end(&out_zip);
    if (has_source) mz_zip_reader_end(&src_zip);

    if (!ok) {
        remove_quiet(new_path);
        return false;
    }

    boost::system::error_code ec;
    fs::remove(archive_path, ec);
    fs::rename(new_path, archive_path, ec);
    if (ec) {
        remove_quiet(new_path);
        return false;
    }
    return true;
}

IoResult save_project(const ProjectState& state, const std::string& out_path) {
    IoResult r;

    // 1. Atomic temp path. Clean any stale leftover via the non-throwing
    //    helper: a locked file or a directory squatting on the scratch
    //    path must surface as a reported IoResult, never as an escaping
    //    boost::filesystem exception.
    const std::string tmp_path = out_path + ".tmp.3mf";
    {
        std::string detail;
        if (!clear_stale_scratch(tmp_path, detail)) {
            r.exit_code = to_int(ExitCode::invalid_state);
            r.error_code = "invalid_state";
            r.error_message = detail;
            return r;
        }
    }

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
        remove_quiet(tmp_path);
        r.exit_code = to_int(ExitCode::invalid_state); r.error_code = "invalid_state";
        r.error_message = "store_bbs_3mf returned false for: " + tmp_path;
        return r;
    }

    // 3b. Thumbnail passthrough: replace store_bbs_3mf's placeholder-encoded
    // thumbnails with the original PNG blobs from the source archive (if present),
    // or with synthesized valid PNGs for newly-added plates.
    if (!rewrite_thumbnails(tmp_path, state.source_path, state.plate_data)) {
        remove_quiet(tmp_path);
        r.exit_code = to_int(ExitCode::invalid_state); r.error_code = "invalid_state";
        r.error_message = "thumbnail rewrite failed for: " + tmp_path;
        return r;
    }

    // 4. Runtime invariant guard.
    GuardResult gr = run_guard(tmp_path, state);
    if (!gr.ok) {
        remove_quiet(tmp_path);
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
            boost::system::error_code stale_ec;
            fs::remove(bak, stale_ec);   // best-effort; locked .bak must not abort the save
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
        remove_quiet(tmp_path);
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
    {
        std::string detail;
        if (!clear_stale_scratch(tmp, detail)) {
            r.exit_code = to_int(ExitCode::invalid_state);
            r.error_code = "invalid_state";
            r.error_message = detail;
            return r;
        }
    }
    try {
        fs::copy_file(src, tmp, fs::copy_options::overwrite_existing);
        if (fs::exists(dst)) fs::remove(dst);
        fs::rename(tmp, dst);
    } catch (const std::exception& e) {
        remove_quiet(tmp);
        r.exit_code = to_int(ExitCode::invalid_state); r.error_code = "invalid_state";
        r.error_message = std::string("atomic_copy failed: ") + e.what();
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace bambu_cli
