#include "project_ops.hpp"

#include "exceptions.hpp"

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/Config.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <sstream>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/join.hpp>

namespace bambu_cli {

std::vector<std::string> list_plate_names(const ProjectState& state) {
    std::vector<std::string> out;
    out.reserve(state.plate_data.size());
    for (const auto* p : state.plate_data) {
        if (p) out.push_back(p->plate_name);
        else   out.push_back("");
    }
    return out;
}

OpResult add_plate(ProjectState& state, const std::string& name) {
    OpResult r;
    if (name.empty())
        throw std::invalid_argument("plate name must be non-empty");
    for (const auto* p : state.plate_data) {
        if (p && p->plate_name == name)
            throw DuplicateNameError("plate '" + name + "' already exists");
    }
    auto* plate = new Slic3r::PlateData();
    // plate_index is 1-based, monotonically increasing.
    int max_idx = 0;
    for (const auto* p : state.plate_data) if (p && p->plate_index > max_idx) max_idx = p->plate_index;
    plate->plate_index = max_idx + 1;
    plate->plate_name  = name;
    // objects_and_instances + obj_inst_map start empty (new plate is empty).
    // config inherits defaults at save time.
    state.plate_data.push_back(plate);
    r.ok = true;
    return r;
}

static int find_plate_by_name(const ProjectState& state, const std::string& name) {
    for (size_t i = 0; i < state.plate_data.size(); ++i) {
        if (state.plate_data[i] && state.plate_data[i]->plate_name == name)
            return static_cast<int>(i);
    }
    return -1;
}

OpResult remove_plate(ProjectState& state, const std::string& name) {
    OpResult r;
    int idx = find_plate_by_name(state, name);
    if (idx < 0)
        throw std::out_of_range("plate '" + name + "' not found");
    Slic3r::PlateData* pd = state.plate_data[static_cast<size_t>(idx)];
    if (!pd->objects_and_instances.empty())
        throw std::out_of_range("plate '" + name + "' is not empty (" +
                                std::to_string(pd->objects_and_instances.size()) +
                                " instance(s)); remove objects first");
    delete pd;
    state.plate_data.erase(state.plate_data.begin() + idx);
    // Re-assign plate_index values to ensure contiguous 0-based sequence.
    // The BBS 3MF loader requires contiguous 1-based plater_ids on reload:
    // it checks  if (plate_index_in_file > m_plater_data.size())  and rejects
    // gaps.  Since we may have removed a plate from the middle, compact now.
    for (int i = 0; i < static_cast<int>(state.plate_data.size()); ++i) {
        if (state.plate_data[static_cast<size_t>(i)])
            state.plate_data[static_cast<size_t>(i)]->plate_index = i;
    }
    r.ok = true;
    return r;
}

OpResult rename_plate(ProjectState& state,
                      const std::string& from,
                      const std::string& to) {
    OpResult r;
    if (to.empty())
        throw std::invalid_argument("new plate name must be non-empty");
    for (const auto* p : state.plate_data) {
        if (p && p->plate_name == to)
            throw DuplicateNameError("plate '" + to + "' already exists");
    }
    int idx = find_plate_by_name(state, from);
    if (idx < 0)
        throw std::out_of_range("plate '" + from + "' not found");
    state.plate_data[static_cast<size_t>(idx)]->plate_name = to;
    r.ok = true;
    return r;
}

static std::string derive_object_name(const std::string& stl_path) {
    boost::filesystem::path p(stl_path);
    return p.stem().string();
}

// Helper: compute bed AABB for plate 1 (base bed) from project/plate config.
// Returns defaults [0..256] x [0..256] if no printable_area config found.
static void get_bed_aabb(const Slic3r::PlateData* pd,
                         const Slic3r::DynamicPrintConfig& project_config,
                         double& bed_minx, double& bed_miny,
                         double& bed_maxx, double& bed_maxy) {
    bed_minx = 0.0; bed_miny = 0.0; bed_maxx = 256.0; bed_maxy = 256.0;
    const Slic3r::ConfigOption* pa_opt = nullptr;
    if (pd && pd->config.has("printable_area"))
        pa_opt = pd->config.option("printable_area");
    if (!pa_opt && project_config.has("printable_area"))
        pa_opt = project_config.option("printable_area");
    if (auto* pts = dynamic_cast<const Slic3r::ConfigOptionPoints*>(pa_opt)) {
        if (!pts->values.empty()) {
            bed_minx = bed_maxx = pts->values.front().x();
            bed_miny = bed_maxy = pts->values.front().y();
            for (const auto& p : pts->values) {
                bed_minx = std::min(bed_minx, p.x());
                bed_miny = std::min(bed_miny, p.y());
                bed_maxx = std::max(bed_maxx, p.x());
                bed_maxy = std::max(bed_maxy, p.y());
            }
        }
    }
}

// Compute BBS plate-world origin for a 1-based plate_index.
// The GUI layout places plates on a stride_x × stride_y grid; stride = bed_dim * 1.2.
// compute_colum_count logic (from PartPlate.hpp):
//   cols = round(sqrt(total)); if sqrt > round → cols+1 else cols.
// For our CLI purposes we only need stride (plate 1 → origin (0,0);
// plate 2 → (stride_x, 0), etc.). Since we don't know total_plate_count
// we use a fixed large-enough column count (e.g., ceil(sqrt(plate_index+1))).
Slic3r::Vec3d plate_world_origin(int plate_index_1based,
                                 double bed_width, double bed_height) {
    // BBS PartPlateList::LOGICAL_PART_PLATE_GAP = 1.0/5.0 = 0.2
    static const double LOGICAL_PART_PLATE_GAP = 0.2;
    double stride_x = bed_width  * (1.0 + LOGICAL_PART_PLATE_GAP);
    double stride_y = bed_height * (1.0 + LOGICAL_PART_PLATE_GAP);

    // Use enough columns for the current plate (conservative estimate).
    // The empirically-derived formula: cols = compute_colum_count(plate_index_1based)
    // For simplicity and CLI correctness, compute cols for plate_index_1based plates.
    float v = std::sqrt(static_cast<float>(plate_index_1based));
    float rv = std::round(v);
    int cols = (v > rv) ? static_cast<int>(rv) + 1 : static_cast<int>(rv);
    if (cols < 1) cols = 1;

    int pi = plate_index_1based - 1;   // 0-based
    int plate_col = pi % cols;
    int plate_row = pi / cols;
    return Slic3r::Vec3d(
        plate_col * stride_x,
        -plate_row * stride_y,   // GUI rows stack top-down; bed Y grows upward
        0.0
    );
}

OpResult add_object_to_plate(ProjectState& state,
                             const std::string& plate_name,
                             const std::string& stl_path,
                             const std::string& object_name_override,
                             int filament_idx,
                             const ManualTransform* tf,
                             int count,
                             ObjectRef* out_ref) {
    OpResult r;
    if (!boost::filesystem::exists(stl_path))
        throw FileNotFoundError("stl not found: " + stl_path);
    int plate_idx = find_plate_by_name(state, plate_name);
    if (plate_idx < 0)
        throw std::out_of_range("plate '" + plate_name + "' not found");

    // Count clamp: 0 or negative → 1.
    const int copies = std::max(1, count);

    // 1. Load STL into a scratch Model once; we'll deep-copy the object N times.
    //    add_object(const ModelObject&) re-issues internal ObjectIDs on each copy so
    //    each ModelObject is fully independent and gets a distinct 3MF object_id on
    //    save.  The BBS 3MF format requires a distinct object_id per plate
    //    obj_inst_map entry (map key collision otherwise drops all but one entry on
    //    reload, breaking list_objects and objects_and_instances reconstruction).
    Slic3r::Model scratch_model;
    if (!Slic3r::load_stl(stl_path.c_str(), &scratch_model))
        throw std::runtime_error("load_stl returned false for: " + stl_path);
    if (scratch_model.objects.empty() || scratch_model.objects[0]->volumes.empty())
        throw std::runtime_error("stl loaded with no geometry: " + stl_path);

    // Cache the mesh bbox BEFORE the loop (same mesh for all copies).
    Slic3r::TriangleMesh mesh_cache = scratch_model.objects[0]->volumes[0]->mesh();
    Slic3r::BoundingBoxf3 bbox = mesh_cache.bounding_box();

    // Derive object name once.
    const std::string name = object_name_override.empty()
                             ? derive_object_name(stl_path) : object_name_override;

    Slic3r::PlateData* pd = state.plate_data[plate_idx];
    const bool manual = tf && (tf->has_translate || tf->has_rotate || tf->has_scale);

    // Track rollback range.
    // loaded_id is the 3MF identify_id and MUST be globally unique within
    // the project. Earlier implementations sized this from pd->obj_inst_map.size()
    // per-plate, which collided across freshly-empty plates — list_objects
    // then returned cross-plate matches for any filter. Compute the global
    // max across all plates' obj_inst_map entries AND all ModelInstance
    // loaded_ids (post-load 3MF state) and add 1.
    size_t max_existing_loaded_id = 0;
    for (const auto* p : state.plate_data) {
        if (!p) continue;
        for (const auto& kv : p->obj_inst_map) {
            size_t lid = static_cast<size_t>(kv.second.second);
            if (lid > max_existing_loaded_id) max_existing_loaded_id = lid;
        }
    }
    for (const auto* mo : state.model.objects) {
        if (!mo) continue;
        for (const auto* inst : mo->instances) {
            if (!inst) continue;
            size_t lid = static_cast<size_t>(inst->loaded_id);
            if (lid > max_existing_loaded_id) max_existing_loaded_id = lid;
        }
    }
    const size_t base_loaded_id = max_existing_loaded_id + 1;
    const int    base_obj_idx   = static_cast<int>(state.model.objects.size());

    // Compute bed AABB and plate-world origin.
    double bed_minx, bed_miny, bed_maxx, bed_maxy;
    get_bed_aabb(pd, state.project_config, bed_minx, bed_miny, bed_maxx, bed_maxy);

    const double bed_width  = bed_maxx - bed_minx;
    const double bed_height = bed_maxy - bed_miny;
    // BBS 3MF stores plater_id as 1-based, but loaded plate_index is 0-based
    // (bbs_3mf.cpp sets plate_index = plater_id - 1 on load).
    // add_plate() sets plate_index = max_existing_index + 1, so fresh plates
    // are also consistently 0-based: plate 1 → index=0, plate 2 → index=1, etc.
    // The GUI stride formula uses a 1-based plate number.
    int plate_number = pd->plate_index + 1;
    Slic3r::Vec3d plate_origin = plate_world_origin(plate_number, bed_width, bed_height);

    double plate_bed_minx = bed_minx + plate_origin.x();
    double plate_bed_miny = bed_miny + plate_origin.y();
    double plate_bed_maxx = bed_maxx + plate_origin.x();
    double plate_bed_maxy = bed_maxy + plate_origin.y();

    // Pre-compute sqrt-grid parameters (derived from total copies, not loop index).
    // CRITICAL: grid_cols must come from N (batch size), not from i+1.
    const double auto_margin  = 10.0;
    const double default_cell = 20.0;
    const double cell_x = std::max(bbox.size().x() + auto_margin, default_cell);
    const double cell_y = std::max(bbox.size().y() + auto_margin, default_cell);
    const int grid_cols = static_cast<int>(
        std::ceil(std::sqrt(static_cast<double>(copies))));

    // Stacking transform (manual mode).
    static const double DEG2RAD = 0.01745329251994329577;
    Slic3r::Vec3d stack_offset = Slic3r::Vec3d::Zero();
    Slic3r::Vec3d stack_rot    = Slic3r::Vec3d::Zero();
    Slic3r::Vec3d stack_scale  = Slic3r::Vec3d(1, 1, 1);
    if (manual) {
        Slic3r::Vec3d local_offset = tf->has_translate
            ? Slic3r::Vec3d(tf->tx, tf->ty, tf->tz) : Slic3r::Vec3d::Zero();
        // plate_bed_min == 0 for plate 1, so stack_offset == local_offset on plate 1.
        stack_offset = Slic3r::Vec3d(plate_bed_minx, plate_bed_miny, 0.0) + local_offset;
        stack_rot   = tf->has_rotate
            ? Slic3r::Vec3d(tf->rx * DEG2RAD, tf->ry * DEG2RAD, tf->rz * DEG2RAD)
            : Slic3r::Vec3d::Zero();
        stack_scale = tf->has_scale
            ? Slic3r::Vec3d(tf->sx, tf->sy, tf->sz) : Slic3r::Vec3d(1, 1, 1);
    }

    // 2–5. For each copy: deep-copy the scratch object, stamp attribution, clear
    //       pre-existing instances, add 1 instance with the computed transform.
    //       Each copy gets a distinct ModelObject (and thus a distinct 3MF object_id
    //       on save), which is required for correct BBS 3MF obj_inst_map round-trip.
    for (int k = 0; k < copies; ++k) {
        // Deep-copy: re-issues ObjectIDs so this copy is fully independent.
        Slic3r::ModelObject* obj_k = state.model.add_object(*scratch_model.objects[0]);
        int obj_idx_k = static_cast<int>(state.model.objects.size()) - 1;
        obj_k->name       = name;
        obj_k->input_file = stl_path;

        // Stamp source attribution on every volume (Bug B / Bug C fix).
        for (size_t vi = 0; vi < obj_k->volumes.size(); ++vi) {
            Slic3r::ModelVolume* vol = obj_k->volumes[vi];
            if (!vol) continue;
            vol->source.input_file = stl_path;
            vol->source.object_idx = 0;
            vol->source.volume_idx = static_cast<int>(vi);
        }

        // Clear pre-existing instances (load_stl preserves the STL's instance
        // vector; leaving them causes off-by-one in count).
        obj_k->clear_instances();

        // Add the single instance for this copy.
        Slic3r::ModelInstance* inst_k = obj_k->add_instance();

        if (manual) {
            // Stacking mode: all copies at the same pose.
            inst_k->set_offset(stack_offset);
            inst_k->set_rotation(stack_rot);
            inst_k->set_scaling_factor(stack_scale);
        } else {
            // Auto-grid mode: sqrt-grid layout, cols derived from total copies.
            int col = k % grid_cols;
            int row = k / grid_cols;
            Slic3r::Vec3d offset(
                plate_bed_minx + auto_margin + col * cell_x
                    + bbox.size().x() * 0.5 - bbox.center().x(),
                plate_bed_miny + auto_margin + row * cell_y
                    + bbox.size().y() * 0.5 - bbox.center().y(),
                -bbox.min.z()   // place object base on bed
            );
            inst_k->set_offset(offset);
            inst_k->set_rotation(Slic3r::Vec3d::Zero());
            inst_k->set_scaling_factor(Slic3r::Vec3d(1, 1, 1));
        }

        // Update plate bookkeeping. Each copy gets a unique loaded_id
        // (= identify_id in 3MF) so obj_inst_map → objects_and_instances
        // reconstruction works correctly after save/reload.
        size_t new_loaded_id = base_loaded_id + static_cast<size_t>(k);
        inst_k->loaded_id = new_loaded_id;
        pd->obj_inst_map[obj_idx_k] = {0, static_cast<int>(new_loaded_id)};
        pd->objects_and_instances.push_back({obj_idx_k, 0});

        // Off-bed check (manual mode only; scale-only AABB, rotation excluded).
        // Why rotation is excluded: rotating a bbox and re-AABB-ing inflates the
        // box artificially; the GUI's own off-bed indicator uses the same
        // scale-yes-rotation-no approximation.
        if (manual) {
            double sx = tf->has_scale ? tf->sx : 1.0;
            double sy = tf->has_scale ? tf->sy : 1.0;
            double tx = stack_offset.x();
            double ty = stack_offset.y();

            // Scaled min/max (handle negative scale by swapping).
            double bx0 = std::min(bbox.min.x() * sx, bbox.max.x() * sx) + tx;
            double bx1 = std::max(bbox.min.x() * sx, bbox.max.x() * sx) + tx;
            double by0 = std::min(bbox.min.y() * sy, bbox.max.y() * sy) + ty;
            double by1 = std::max(bbox.min.y() * sy, bbox.max.y() * sy) + ty;

            const double off_margin = 0.001;
            if (bx0 < plate_bed_minx - off_margin || by0 < plate_bed_miny - off_margin ||
                bx1 > plate_bed_maxx + off_margin || by1 > plate_bed_maxy + off_margin) {
                // Roll back: delete all ModelObjects added so far in this batch.
                int cur = static_cast<int>(state.model.objects.size());
                for (int ri = cur - 1; ri >= base_obj_idx; --ri)
                    state.model.delete_object(static_cast<size_t>(ri));
                // Clean up plate data.
                int added_so_far = k + 1;
                for (int k2 = 0; k2 < added_so_far && !pd->objects_and_instances.empty(); ++k2)
                    pd->objects_and_instances.pop_back();
                for (auto it = pd->obj_inst_map.begin(); it != pd->obj_inst_map.end(); ) {
                    size_t eid = static_cast<size_t>(it->second.second);
                    if (eid >= base_loaded_id &&
                        eid < base_loaded_id + static_cast<size_t>(copies))
                        it = pd->obj_inst_map.erase(it);
                    else ++it;
                }
                std::ostringstream os;
                os << "object '" << name << "' bbox ["
                   << bx0 << "," << by0 << "..." << bx1 << "," << by1
                   << "] off-bed (plate '" << plate_name << "' AABB ["
                   << plate_bed_minx << "," << plate_bed_miny
                   << ".." << plate_bed_maxx << "," << plate_bed_maxy << "])";
                throw PlacementFailure(os.str());
            }
        }
    }

    // 6. Filament validation + assignment.
    //    -1 means "not specified" (skip). Validated as 1-based extruder slot.
    if (filament_idx != -1) {
        const Slic3r::ConfigOption* slots_opt =
            state.project_config.option("filament_settings_id");
        size_t slot_count = 0;
        if (auto* vs = dynamic_cast<const Slic3r::ConfigOptionStrings*>(slots_opt))
            slot_count = vs->values.size();
        if (filament_idx < 1 || filament_idx > static_cast<int>(slot_count)) {
            // Roll back all copies.
            int cur = static_cast<int>(state.model.objects.size());
            for (int ri = cur - 1; ri >= base_obj_idx; --ri)
                state.model.delete_object(static_cast<size_t>(ri));
            for (int k2 = 0; k2 < copies && !pd->objects_and_instances.empty(); ++k2)
                pd->objects_and_instances.pop_back();
            for (auto it = pd->obj_inst_map.begin(); it != pd->obj_inst_map.end(); ) {
                size_t eid = static_cast<size_t>(it->second.second);
                if (eid >= base_loaded_id &&
                    eid < base_loaded_id + static_cast<size_t>(copies))
                    it = pd->obj_inst_map.erase(it);
                else ++it;
            }
            throw std::invalid_argument("filament " + std::to_string(filament_idx) +
                                        " out of range [1," + std::to_string(slot_count) + "]");
        }
        // Apply extruder to every copy (per-ModelObject config).
        for (int ki = 0; ki < copies; ++ki) {
            auto* obj_ki = state.model.objects[base_obj_idx + ki];
            if (obj_ki) obj_ki->config.set("extruder", filament_idx);
        }
    }

    if (out_ref) {
        out_ref->object_idx   = base_obj_idx;   // first copy's model index
        out_ref->instance_idx = 0;
        out_ref->object_name  = name;
    }
    r.ok = true;
    return r;
}

std::vector<ListedObject> list_objects(const ProjectState& state, const std::string& only_plate) {
    std::vector<ListedObject> out;

    // After bbs_3mf loading, obj_inst_map[key] = (inst_idx_in_object, identify_id/loaded_id).
    // The bbs_3mf reader stamps inst->loaded_id = identify_id during load (bbs_3mf.cpp:2402).
    // For freshly-added objects (same process), we set inst->loaded_id explicitly in
    // add_object_to_plate and key obj_inst_map by that same loaded_id.
    //
    // Either way: for each obj_inst_map entry on a plate,
    //   kv.second.second == inst->loaded_id  (after load or after add).
    // So we build a loaded_id -> plate map and then enumerate model objects/instances.

    // Step 1: build loaded_id -> plate mapping for relevant plates.
    std::map<size_t, const Slic3r::PlateData*> id_to_plate;
    for (const auto* pd : state.plate_data) {
        if (!pd) continue;
        if (!only_plate.empty() && pd->plate_name != only_plate) continue;
        for (const auto& kv : pd->obj_inst_map) {
            size_t lid = static_cast<size_t>(kv.second.second);
            if (lid > 0) id_to_plate[lid] = pd;
        }
    }

    // Step 2: enumerate all model instances and match by loaded_id.
    for (int oi = 0; oi < static_cast<int>(state.model.objects.size()); ++oi) {
        const auto* obj = state.model.objects[oi];
        if (!obj) continue;
        for (int ii = 0; ii < static_cast<int>(obj->instances.size()); ++ii) {
            const auto* inst = obj->instances[ii];
            if (!inst) continue;
            auto it = id_to_plate.find(inst->loaded_id);
            if (it == id_to_plate.end()) continue;
            const auto* pd = it->second;
            ListedObject lo;
            lo.plate_name  = pd->plate_name;
            lo.object_name = obj->name;
            const Slic3r::ConfigOption* eopt = obj->config.option("extruder");
            lo.extruder = eopt ? static_cast<const Slic3r::ConfigOptionInt*>(eopt)->value : 0;
            out.push_back(std::move(lo));
        }
    }
    return out;
}

// ---- M9: object remove + object set-filament ------------------------------

OpResult remove_object(ProjectState& state, const std::string& object_name) {
    OpResult r;

    // Collect ALL indices whose name matches (group-by-name semantics from M6
    // N-objects-per-copy model).  Store in ascending order.
    std::vector<int> to_remove;
    for (int i = 0; i < static_cast<int>(state.model.objects.size()); ++i) {
        const auto* obj = state.model.objects[i];
        if (obj && obj->name == object_name)
            to_remove.push_back(i);
    }
    if (to_remove.empty())
        throw std::out_of_range("object '" + object_name + "' not found");

    // Build a lookup set for O(1) membership test (used for objects_and_instances cleanup).
    std::vector<bool> is_removed(state.model.objects.size(), false);
    for (int idx : to_remove) is_removed[static_cast<size_t>(idx)] = true;

    // Collect the loaded_id values of all instances belonging to the removed objects.
    // This is the correct cross-format key for obj_inst_map cleanup:
    //   - After load_project: obj_inst_map key=3mf_object_id, value={instance_id, loaded_id}
    //   - After add_object_to_plate: obj_inst_map key=obj_idx, value={0, loaded_id}
    // In both cases, value.second == loaded_id == ModelInstance::loaded_id.
    // Removing by loaded_id set handles both formats correctly.
    std::set<size_t> removed_loaded_ids;
    for (int idx : to_remove) {
        const auto* obj = state.model.objects[idx];
        if (!obj) continue;
        for (const auto* inst : obj->instances) {
            if (inst && inst->loaded_id != 0)
                removed_loaded_ids.insert(inst->loaded_id);
        }
    }

    // Step 1: detach all instances belonging to the removed objects from every plate.
    for (auto* pd : state.plate_data) {
        if (!pd) continue;
        // objects_and_instances: keyed by (obj_idx, inst_idx) — remove by obj_idx membership.
        auto& oi = pd->objects_and_instances;
        oi.erase(std::remove_if(oi.begin(), oi.end(),
            [&](const std::pair<int,int>& p) {
                int fi = p.first;
                return fi >= 0 && fi < static_cast<int>(is_removed.size()) && is_removed[static_cast<size_t>(fi)];
            }), oi.end());
        // obj_inst_map: remove entries whose value.second (loaded_id) is in the removed set.
        // Works correctly for both post-load (key=3mf_obj_id, value={inst_id, loaded_id})
        // and post-add (key=obj_idx, value={0, loaded_id}) formats.
        for (auto it = pd->obj_inst_map.begin(); it != pd->obj_inst_map.end(); ) {
            size_t lid = static_cast<size_t>(it->second.second);
            if (removed_loaded_ids.count(lid) > 0)
                it = pd->obj_inst_map.erase(it);
            else
                ++it;
        }
    }

    // Step 2: delete ModelObjects in DESCENDING index order so earlier indices
    // remain valid as we delete (each delete shifts higher-indexed objects down).
    for (int i = static_cast<int>(to_remove.size()) - 1; i >= 0; --i) {
        state.model.delete_object(static_cast<size_t>(to_remove[static_cast<size_t>(i)]));
    }

    // Step 3: renumber remaining object-index references in all plates.
    // For each remaining index ref R in a plate, count how many of the removed
    // indices were BELOW R — that is the amount by which R must be decremented.
    for (auto* pd : state.plate_data) {
        if (!pd) continue;
        auto adjust = [&](int old_idx) -> int {
            // Count how many removed indices are strictly less than old_idx.
            int decrement = 0;
            for (int rm : to_remove) {
                if (rm < old_idx) ++decrement;
            }
            return old_idx - decrement;
        };
        for (auto& p : pd->objects_and_instances)
            p.first = adjust(p.first);
        for (auto& kv : pd->obj_inst_map)
            kv.second.first = adjust(kv.second.first);
    }

    r.ok = true;
    return r;
}

OpResult set_object_filament(ProjectState& state,
                             const std::string& object_name,
                             int filament_idx,
                             int part_idx) {
    OpResult r;

    // Collect ALL matching indices (group-by-name semantics).
    std::vector<int> matches;
    for (int i = 0; i < static_cast<int>(state.model.objects.size()); ++i) {
        const auto* obj = state.model.objects[i];
        if (obj && obj->name == object_name)
            matches.push_back(i);
    }
    if (matches.empty())
        throw std::out_of_range("object '" + object_name + "' not found");

    // Validate filament index BEFORE any mutation.
    const Slic3r::ConfigOption* slots_opt =
        state.project_config.option("filament_settings_id");
    size_t slot_count = 0;
    if (auto* vs = dynamic_cast<const Slic3r::ConfigOptionStrings*>(slots_opt))
        slot_count = vs->values.size();
    if (filament_idx < 1 || filament_idx > static_cast<int>(slot_count))
        throw std::invalid_argument("filament " + std::to_string(filament_idx) +
                                    " out of range [1," + std::to_string(slot_count) + "]");

    // If per-volume mode requested, pre-validate that EVERY matching object
    // has at least (part_idx + 1) volumes. We validate before any mutation
    // so partial failure does not leave half-updated state.
    if (part_idx >= 0) {
        for (int idx : matches) {
            const auto* obj = state.model.objects[idx];
            if (!obj) continue;
            if (part_idx >= static_cast<int>(obj->volumes.size()))
                throw std::invalid_argument("part index " + std::to_string(part_idx) +
                                            " out of range for object '" + object_name +
                                            "' (has " + std::to_string(obj->volumes.size()) +
                                            " volume(s))");
        }
    }

    // Apply to every matching ModelObject.
    for (int idx : matches) {
        auto* obj = state.model.objects[idx];
        if (!obj) continue;

        // Bug B retrofit guard (object-level mode only; per-volume mode
        // operates on a specific volume that is assumed already stamped).
        if (part_idx < 0) {
            for (auto* vol : obj->volumes) {
                if (vol && vol->source.input_file.empty()) {
                    vol->source.input_file = obj->input_file;
                    vol->source.object_idx = 0;
                    vol->source.volume_idx = 0;
                }
            }
            obj->config.set("extruder", filament_idx);
        } else {
            // Per-volume mode: stamp source on the target volume only if
            // empty, then write extruder to its config.
            auto* vol = obj->volumes[part_idx];
            if (vol) {
                if (vol->source.input_file.empty()) {
                    vol->source.input_file = obj->input_file;
                    vol->source.object_idx = 0;
                    vol->source.volume_idx = part_idx;
                }
                vol->config.set("extruder", filament_idx);
            }
        }
    }

    r.ok = true;
    return r;
}

// ---- M7: config set / unset / list ----------------------------------------

// Helper: get filament_count from project_config.
// filament_count = length of filament_settings_id values.
// Falls back to 1 if the option is absent or empty.
static size_t get_filament_count(const Slic3r::DynamicPrintConfig& cfg) {
    const Slic3r::ConfigOption* opt = cfg.option("filament_settings_id");
    if (auto* vs = dynamic_cast<const Slic3r::ConfigOptionStrings*>(opt))
        if (!vs->values.empty())
            return vs->values.size();
    return 1;
}

// Decide which slot a config key belongs to in different_settings_to_system.
// Slot layout matches the OrcaSlicer comment block at project_ops.cpp:420-427:
//   slot 0        -> process-tab key (Preset::print_options)
//   slot 1        -> printer-tab key (Preset::printer_options)
//   slots 2..fc+1 -> per-filament dirty keys (Preset::filament_options, broadcast via -2 sentinel)
//   slot 0        -> unknown (default: process tab — GUI ignores irrelevant entries)
// Ported from OrcaSlicer/src/cli/project_ops.cpp:434-451.
static int classify_key_slot(const std::string& key) {
    using namespace Slic3r;
    {
        const auto& opts = Preset::print_options();
        if (std::find(opts.begin(), opts.end(), key) != opts.end()) return 0;
    }
    {
        const auto& opts = Preset::printer_options();
        if (std::find(opts.begin(), opts.end(), key) != opts.end()) return 1;
    }
    {
        const auto& opts = Preset::filament_options();
        if (std::find(opts.begin(), opts.end(), key) != opts.end()) return -2; // sentinel: broadcast to all filament slots
    }
    return 0;
}

// Add <key> to the named slot of different_settings_to_system. Keys are
// stored as a flat semicolon-separated string within each slot.
static void add_key_to_slot(Slic3r::ConfigOptionStrings* diff, int slot, const std::string& key) {
    std::vector<std::string> keys;
    if (!diff->values[slot].empty())
        Slic3r::unescape_strings_cstyle(diff->values[slot], keys);
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
        keys.push_back(key);
        diff->values[slot] = boost::algorithm::join(keys, ";");
    }
}

static void remove_key_from_slot(Slic3r::ConfigOptionStrings* diff, int slot, const std::string& key) {
    std::vector<std::string> keys;
    if (!diff->values[slot].empty())
        Slic3r::unescape_strings_cstyle(diff->values[slot], keys);
    auto it = std::find(keys.begin(), keys.end(), key);
    if (it == keys.end()) return;
    keys.erase(it);
    diff->values[slot] = boost::algorithm::join(keys, ";");
}

// Mark a project-level config key as overriding the system preset. Routes
// to slot 0 (process) / slot 1 (printer) / slots 2..2+fc-1 (filament,
// broadcast across all filament slots) based on classify_key_slot.
// Replaces the prior slot-0-only add_to_different_settings_to_system.
static void add_to_different_settings_to_system(Slic3r::DynamicPrintConfig& cfg,
                                                const std::string& key) {
    size_t fc = get_filament_count(cfg);
    size_t target_size = fc + 2;

    auto* opt = cfg.option<Slic3r::ConfigOptionStrings>("different_settings_to_system",
                                                         /*create_if_missing=*/true);
    if (!opt) return;
    if (opt->values.size() < target_size)
        opt->values.resize(target_size, std::string());

    int slot = classify_key_slot(key);
    if (slot == -2) {
        // Filament key: broadcast to every filament slot (2..2+fc-1).
        for (size_t i = 0; i < fc; ++i)
            add_key_to_slot(opt, static_cast<int>(2 + i), key);
    } else {
        // Process key (slot 0), printer key (slot 1), or unknown (default 0).
        add_key_to_slot(opt, slot, key);
    }
}

// Symmetric to add_to_different_settings_to_system. Routes unset based on
// the same classifier so the unset cleans the slot the set actually wrote to.
static void remove_from_different_settings_to_system(Slic3r::DynamicPrintConfig& cfg,
                                                     const std::string& key) {
    auto* opt = cfg.option<Slic3r::ConfigOptionStrings>("different_settings_to_system",
                                                         /*create_if_missing=*/false);
    if (!opt || opt->values.empty()) return;

    size_t fc = get_filament_count(cfg);
    if (opt->values.size() < fc + 2) return;

    int slot = classify_key_slot(key);
    if (slot == -2) {
        for (size_t i = 0; i < fc; ++i)
            remove_key_from_slot(opt, static_cast<int>(2 + i), key);
    } else {
        remove_key_from_slot(opt, slot, key);
    }
}

int find_object_by_name(const ProjectState& state, const std::string& name) {
    for (int i = 0; i < static_cast<int>(state.model.objects.size()); ++i) {
        const auto* obj = state.model.objects[i];
        if (obj && obj->name == name)
            return i;
    }
    return -1;
}


OpResult config_set(ProjectState& state,
                    const std::string& object_name,
                    const std::string& key,
                    const std::string& value) {
    OpResult r;

    // Validate the key against print_config_def.
    if (!Slic3r::print_config_def.has(key))
        throw BadConfigError("unknown config key: '" + key + "'");

    // different_settings_to_system is a system-managed key that tracks which
    // keys were user-modified. It must not be set directly by the user.
    if (key == "different_settings_to_system")
        throw BadConfigError("'different_settings_to_system' is a system-managed key "
                             "and cannot be set directly");

    Slic3r::ConfigSubstitutionContext subst_ctx{
        Slic3r::ForwardCompatibilitySubstitutionRule::Disable };

    if (object_name.empty()) {
        // Project-level: target is state.project_config (DynamicPrintConfig directly).
        try {
            state.project_config.set_deserialize(key, value, subst_ctx);
        } catch (const std::exception& ex) {
            throw BadConfigError("invalid value for '" + key + "': " + ex.what());
        }
        // Register this key in different_settings_to_system (position 0, process tab)
        // so that BambuStudio recognizes it as a user override and honors it when
        // slicing. Per-object overrides go through model_settings.config and do NOT
        // need this tracking. Filament-tab and printer-tab routing is a follow-up.
        add_to_different_settings_to_system(state.project_config, key);
    } else {
        // Group-by-name: apply the set to EVERY ModelObject whose name matches
        // (multiple matches happen when `--count N` produced N clones).
        // Sibling parity: OrcaSlicer commit c2ddf51d87 semantics.
        std::vector<int> matches;
        for (int i = 0; i < static_cast<int>(state.model.objects.size()); ++i) {
            const auto* obj = state.model.objects[i];
            if (obj && obj->name == object_name)
                matches.push_back(i);
        }
        if (matches.empty())
            throw std::out_of_range("object '" + object_name + "' not found");
        for (int idx : matches) {
            // ModelObject::config is ModelConfigObject which inherits ModelConfig.
            // ModelConfig::set_deserialize delegates to m_data.set_deserialize + touch().
            try {
                state.model.objects[idx]->config.set_deserialize(key, value, subst_ctx);
            } catch (const std::exception& ex) {
                throw BadConfigError("invalid value for '" + key + "': " + ex.what());
            }
        }
    }

    r.ok = true;
    return r;
}

OpResult config_unset(ProjectState& state,
                      const std::string& object_name,
                      const std::string& key) {
    OpResult r;

    // Validate the key against print_config_def.
    if (!Slic3r::print_config_def.has(key))
        throw BadConfigError("unknown config key: '" + key + "'");

    if (object_name.empty()) {
        if (!state.project_config.has(key))
            throw std::out_of_range("key '" + key + "' not set on target");
        state.project_config.erase(key);
        // Remove from different_settings_to_system tracking so BS no longer
        // treats this key as a user override.
        remove_from_different_settings_to_system(state.project_config, key);
    } else {
        // Group-by-name: erase the key from EVERY matching ModelObject that
        // has it set. Throw unknown_reference if no match exists, or if a
        // match exists but no match has the key. Sibling parity: OrcaSlicer
        // commit c2ddf51d87 semantics.
        std::vector<int> matches;
        for (int i = 0; i < static_cast<int>(state.model.objects.size()); ++i) {
            const auto* obj = state.model.objects[i];
            if (obj && obj->name == object_name)
                matches.push_back(i);
        }
        if (matches.empty())
            throw std::out_of_range("object '" + object_name + "' not found");
        int erased = 0;
        for (int idx : matches) {
            if (state.model.objects[idx]->config.has(key)) {
                state.model.objects[idx]->config.erase(key);
                ++erased;
            }
        }
        if (erased == 0)
            throw std::out_of_range("key '" + key + "' not set on target");
    }

    r.ok = true;
    return r;
}

std::vector<ConfigEntry> config_list(const ProjectState& state,
                                     const std::string& object_name,
                                     bool only_changed) {
    std::vector<ConfigEntry> out;

    if (object_name.empty()) {
        // Project-level: single DynamicPrintConfig.
        const Slic3r::DynamicPrintConfig& cfg = state.project_config;
        std::vector<std::string> keys_to_emit;
        if (only_changed) {
            std::vector<std::string> all_keys = cfg.keys();
            if (!all_keys.empty()) {
                std::unique_ptr<Slic3r::DynamicPrintConfig> defaults(
                    Slic3r::DynamicPrintConfig::new_from_defaults_keys(all_keys));
                if (defaults) {
                    keys_to_emit = cfg.diff(*defaults);
                } else {
                    keys_to_emit = std::move(all_keys);
                }
            }
        } else {
            keys_to_emit = cfg.keys();
        }
        for (const auto& k : keys_to_emit) {
            ConfigEntry e; e.key = k; e.value = cfg.opt_serialize(k);
            out.push_back(std::move(e));
        }
        return out;
    }

    // Per-object: group-by-name.
    // Walk ALL ModelObjects whose name matches, build the UNION of their
    // explicitly-set keys (or per-match diff against defaults when
    // only_changed), and take each key's value from the first match that
    // has it. Sibling parity: OrcaSlicer commit c2ddf51d87 semantics.
    std::vector<const Slic3r::DynamicPrintConfig*> match_cfgs;
    for (int i = 0; i < static_cast<int>(state.model.objects.size()); ++i) {
        const auto* obj = state.model.objects[i];
        if (obj && obj->name == object_name)
            match_cfgs.push_back(&obj->config.get());
    }
    if (match_cfgs.empty()) return out;   // no match -> empty list

    // Build the union of keys to emit, sorted by std::set for deterministic
    // output regardless of which match contributed which key first.
    std::set<std::string> union_keys;
    for (const auto* cfg : match_cfgs) {
        std::vector<std::string> match_keys = cfg->keys();
        if (only_changed && !match_keys.empty()) {
            std::unique_ptr<Slic3r::DynamicPrintConfig> defaults(
                Slic3r::DynamicPrintConfig::new_from_defaults_keys(match_keys));
            if (defaults) {
                for (const auto& k : cfg->diff(*defaults))
                    union_keys.insert(k);
            } else {
                for (const auto& k : match_keys)
                    union_keys.insert(k);
            }
        } else {
            for (const auto& k : match_keys)
                union_keys.insert(k);
        }
    }

    // For each key, emit the value from the first match that has it.
    for (const auto& k : union_keys) {
        for (const auto* cfg : match_cfgs) {
            if (cfg->has(k)) {
                ConfigEntry e; e.key = k; e.value = cfg->opt_serialize(k);
                out.push_back(std::move(e));
                break;
            }
        }
    }
    return out;
}

// ---- D1: object split-to-parts --------------------------------------------

size_t split_object_to_parts(ProjectState& state, const std::string& name)
{
    // First-match on --name. See design note in project_ops.hpp.
    int idx = -1;
    for (int i = 0; i < static_cast<int>(state.model.objects.size()); ++i) {
        if (state.model.objects[i]->name == name) { idx = i; break; }
    }
    if (idx < 0)
        throw std::out_of_range("split-to-parts: object not found: " + name);

    Slic3r::ModelObject* obj = state.model.objects[idx];

    // Validation: exactly one volume.
    if (obj->volumes.size() != 1)
        throw std::invalid_argument(
            "split-to-parts requires exactly 1 volume, got " +
            std::to_string(obj->volumes.size()));

    Slic3r::ModelVolume* vol = obj->volumes[0];

    // Validation: must be a solid model part, not a modifier/support/etc.
    if (vol->type() != Slic3r::ModelVolumeType::MODEL_PART)
        throw std::invalid_argument(
            "split-to-parts: volume type must be MODEL_PART");

    // Align volume name with object name before split so resulting volumes
    // get sensible names (split() appends _1, _2, ...).
    vol->name = obj->name;

    // Capture source attribution before split — ModelVolume::split() resets
    // source on the first resulting volume (this->source = ModelVolume::Source()).
    const std::string saved_input_file = vol->source.input_file;

    // Split. filament_count is the number of loaded filament slots.
    // Scale determinant defaults to 1.f (no scaling applied during split).
    const unsigned int filament_count =
        static_cast<unsigned int>(get_filament_count(state.project_config));
    size_t parts = vol->split(filament_count);

    if (parts <= 1)
        throw std::invalid_argument(
            "split-to-parts: mesh has only 1 connected component");

    // Re-stamp source.input_file on every resulting volume that lost it
    // (defense-in-depth: split() resets source on the first volume).
    if (!saved_input_file.empty()) {
        for (auto* v : obj->volumes) {
            if (v->source.input_file.empty())
                v->source.input_file = saved_input_file;
        }
    }

    return parts;
}

} // namespace bambu_cli
