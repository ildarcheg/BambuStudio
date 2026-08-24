#include "io.hpp"
#include "invariant_guard.hpp"

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PNGReadWrite.hpp"
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

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

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

// Shared .bak swap (save_project step 5 and atomic_copy): rename out ->
// .bak, rename tmp -> out, drop .bak. If the middle rename fails the
// original is restored from .bak (best effort) and <err> carries the
// reason. The tmp file is NOT cleaned up here — callers own scratch
// cleanup. Ported from OrcaSlicer/src/cli/io.cpp:467-499 ("M11 cleanup").
static bool bak_swap(const std::string& tmp_path, const std::string& out_path,
                     std::string& err) {
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
        return true;
    } catch (const std::exception& e) {
        err = std::string("rename failed: ") + e.what();
        return false;
    }
}

bool flush_to_disk(const std::string& path) {
#ifdef _WIN32
    int fd = -1;
    if (_sopen_s(&fd, path.c_str(), _O_RDWR | _O_BINARY, _SH_DENYNO,
                 _S_IREAD | _S_IWRITE) != 0 || fd < 0)
        return false;
    const bool ok = (_commit(fd) == 0);
    _close(fd);
    return ok;
#else
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    const bool ok = (::fsync(fd) == 0);
    ::close(fd);
    return ok;
#endif
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

        // Normalize to the CLI-canonical single-domain shape:
        // key == loaded_id (globally unique), value = (instance_id,
        // loaded_id). The loader keys by 3mf object_id while
        // add_object_to_plate keys by loaded_id — two small-int domains in
        // one map invite collisions, and no consumer reads the loader's
        // keys (every reader uses value.second). Entries with loaded_id 0
        // are unusable and dropped. The store path never reads this map
        // (it consumes objects_and_instances, bbs_3mf.cpp:8202).
        std::map<int, std::pair<int, int>> normalized;
        for (const auto& kv : plate->obj_inst_map) {
            if (kv.second.second > 0)
                normalized[kv.second.second] = kv.second;
        }
        plate->obj_inst_map = std::move(normalized);
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

// --- Per-plate thumbnail for the store path (design note 2026-07-15) ----
// The bbs_3mf loader extracts each source thumbnail's raw PNG bytes into
// PlateData::plate_thumbnail.pixels (width/height stay 0). Decode those
// bytes back to RGBA and hand them to store_bbs_3mf as real ThumbnailData,
// so the exporter writes plate_<N>.png AND derives plate_<N>_small.png
// through its own canonical path (the same code the GUI uses) -- no
// post-write archive rewriting. Rows are stored bottom-up because the
// exporter encodes with the vertical-flip flag (GL framebuffer
// convention, bbs_3mf.cpp:6720). Plates without a source thumbnail (or
// with undecodable bytes) get the 128x128 gray placeholder.
static void fill_placeholder_thumbnail(Slic3r::ThumbnailData& td) {
    td.set(128, 128);
    std::memset(td.pixels.data(), 0xC0, td.pixels.size());   // gray RGBA
}

// Decode a PNG byte blob into bottom-up RGBA ThumbnailData. Returns false
// (td untouched) for undecodable or unsupported formats.
static bool decode_png_to_thumbnail(const void* data, size_t size,
                                    Slic3r::ThumbnailData& td) {
    if (!data || size == 0) return false;
    Slic3r::png::ImageColorscale img;
    Slic3r::png::ReadBuf rb{data, size};
    if (!Slic3r::png::decode_colored_png(rb, img) ||
        img.cols == 0 || img.rows == 0 ||
        (img.bytes_per_pixel != 3 && img.bytes_per_pixel != 4) ||
        img.buf.size() < img.cols * img.rows * img.bytes_per_pixel)
        return false;
    td.set(static_cast<unsigned int>(img.cols),
           static_cast<unsigned int>(img.rows));
    const size_t ch = static_cast<size_t>(img.bytes_per_pixel);
    for (size_t row = 0; row < img.rows; ++row) {
        // bottom-up: destination row counts from the end
        unsigned char* dst =
            td.pixels.data() + (img.rows - 1 - row) * img.cols * 4;
        const unsigned char* src = img.buf.data() + row * img.cols * ch;
        for (size_t col = 0; col < img.cols; ++col) {
            dst[col * 4 + 0] = src[col * ch + 0];
            dst[col * 4 + 1] = src[col * ch + 1];
            dst[col * 4 + 2] = src[col * ch + 2];
            dst[col * 4 + 3] = (ch == 4) ? src[col * ch + 3] : 255;
        }
    }
    return true;
}

// Read one entry from a zip archive into <out>. Returns false when the
// archive can't be opened or the entry is absent. Read-only.
static bool read_archive_entry(const std::string& archive_path,
                               const std::string& entry_name,
                               std::vector<unsigned char>& out) {
    boost::system::error_code ex_ec;
    if (archive_path.empty() || !fs::exists(archive_path, ex_ec)) return false;
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, archive_path.c_str(), 0)) return false;
    bool ok = false;
    int idx = mz_zip_reader_locate_file(&zip, entry_name.c_str(), nullptr, 0);
    if (idx >= 0) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(idx), &st)) {
            out.resize(static_cast<size_t>(st.m_uncomp_size));
            ok = !!mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(idx),
                                                out.data(), out.size(), 0);
        }
    }
    mz_zip_reader_end(&zip);
    return ok;
}

static void build_plate_thumbnail(const Slic3r::PlateData* pd,
                                  const std::string& source_path,
                                  Slic3r::ThumbnailData& td) {
    if (!pd) { fill_placeholder_thumbnail(td); return; }

    // 1. PNG bytes the loader extracted (sliced projects only — the
    //    extraction runs while merging slice_info plate entries; an
    //    unsliced project leaves pixels empty, see the design note).
    if (!pd->plate_thumbnail.pixels.empty() &&
        decode_png_to_thumbnail(pd->plate_thumbnail.pixels.data(),
                                pd->plate_thumbnail.pixels.size(), td))
        return;

    // 2. Read-only fallback: the plate's thumbnail entry in the source
    //    archive, keyed by the loader's plate numbering (plate_index + 1).
    std::vector<unsigned char> png;
    if (read_archive_entry(source_path,
                           "Metadata/plate_" +
                               std::to_string(pd->plate_index + 1) + ".png",
                           png) &&
        decode_png_to_thumbnail(png.data(), png.size(), td))
        return;

    // 3. Newly added plate / undecodable bytes: gray placeholder.
    fill_placeholder_thumbnail(td);
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

    // 2. Per-plate thumbnails: decoded from the source PNG bytes the loader
    //    left in plate_thumbnail.pixels (placeholder when absent). Passed
    //    only via store_params.thumbnail_data -- PlateData is not touched,
    //    so the source bytes survive for subsequent saves and save_project
    //    honors its const contract. (The writer's rels emission targets
    //    plate_1.png unconditionally, bbs_3mf.cpp:6830, and both entries
    //    are guaranteed by the in-memory thumbnail path.)
    std::vector<Slic3r::ThumbnailData> thumbs(state.plate_data.size());
    std::vector<Slic3r::ThumbnailData*> thumb_ptrs;
    thumb_ptrs.reserve(state.plate_data.size());
    for (size_t i = 0; i < state.plate_data.size(); ++i) {
        build_plate_thumbnail(state.plate_data[i], state.source_path, thumbs[i]);
        thumb_ptrs.push_back(&thumbs[i]);
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

    // 4. Runtime invariant guard.
    GuardResult gr = run_guard(tmp_path, state);
    if (!gr.ok) {
        remove_quiet(tmp_path);
        r.exit_code = to_int(ExitCode::invariant_violation); r.error_code = "invariant_violation";
        r.error_message = "guard check '" + gr.failed_check + "' failed: " + gr.failure_detail;
        return r;
    }

    // 4b. Flush the fully-validated temp file to stable storage BEFORE the
    //     swap: once the original is demoted to .bak, the new bytes must
    //     already be on disk or a power loss loses both.
    if (!flush_to_disk(tmp_path)) {
        remove_quiet(tmp_path);
        r.exit_code = to_int(ExitCode::invalid_state); r.error_code = "invalid_state";
        r.error_message = "flush to disk failed for: " + tmp_path;
        return r;
    }

    // 5. Safer atomic .bak-swap (shared bak_swap helper). There is still a
    //    brief window between the two renames with no file at the
    //    destination; a crash there leaves .bak + .tmp.3mf on disk for
    //    manual recovery - strictly better than remove-then-rename, which
    //    had the same window and no .bak.
    std::string swap_err;
    if (!bak_swap(tmp_path, out_path, swap_err)) {
        remove_quiet(tmp_path);
        r.exit_code = to_int(ExitCode::invalid_state); r.error_code = "invalid_state";
        r.error_message = swap_err;
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
        if (!flush_to_disk(tmp))
            throw std::runtime_error("flush to disk failed for: " + tmp);
    } catch (const std::exception& e) {
        remove_quiet(tmp);
        r.exit_code = to_int(ExitCode::invalid_state); r.error_code = "invalid_state";
        r.error_message = std::string("atomic_copy failed: ") + e.what();
        return r;
    }
    // Same .bak swap as save_project: never destroy the destination before
    // the replacement is in place.
    std::string swap_err;
    if (!bak_swap(tmp, dst, swap_err)) {
        remove_quiet(tmp);
        r.exit_code = to_int(ExitCode::invalid_state); r.error_code = "invalid_state";
        r.error_message = "atomic_copy failed: " + swap_err;
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace bambu_cli
