#include "project_ops.hpp"

#include "exceptions.hpp"

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Orient.hpp"
#include "libslic3r/Arrange.hpp"
#include "libslic3r/ModelArrange.hpp"

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

// Compute BBS plate-world origin for a 1-based plate_index in a layout of
// total_plates plates. The GUI lays plates on a sqrt-grid with stride =
// bed_dim * 1.2 (GUI_PLATE_GAP_RATIO = 0.2 in PartPlate.cpp). Column count
// MUST come from the total plate count (PartPlate.cpp:4776 calls
// compute_colum_count(m_plate_count)) -- deriving cols from
// plate_index_1based gives wrong x/y for any plate past the first row of
// a layout with > index plates (e.g. plate 3 in a 5-plate layout sits at
// col 2 row 0 with cols=ceil(sqrt(5))=3, not col 0 row 1 from cols=2).
Slic3r::Vec3d plate_world_origin(int plate_index_1based, int total_plates,
                                 double bed_width, double bed_height) {
    // BBS PartPlateList::LOGICAL_PART_PLATE_GAP = 1.0/5.0 = 0.2
    static const double LOGICAL_PART_PLATE_GAP = 0.2;
    double stride_x = bed_width  * (1.0 + LOGICAL_PART_PLATE_GAP);
    double stride_y = bed_height * (1.0 + LOGICAL_PART_PLATE_GAP);

    // cols = ceil(sqrt(total_plates)) -- mirrors PartPlate.cpp
    // compute_colum_count and OrcaSlicer src/cli/placement.cpp::
    // plate_origin_offset.
    int n = std::max(1, total_plates);
    int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
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
    // The GUI stride formula uses a 1-based plate number. Cols must come
    // from the total plate count, not from the per-call index -- see the
    // doc comment on plate_world_origin.
    int plate_number = pd->plate_index + 1;
    int total_plates = static_cast<int>(state.plate_data.size());
    Slic3r::Vec3d plate_origin = plate_world_origin(plate_number, total_plates,
                                                    bed_width, bed_height);

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
                             const std::string& part_name) {
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

    // Per-volume mode: collect all volumes with matching name across all
    // matched objects, then validate at least one was found.
    if (!part_name.empty()) {
        using ObjVol = std::pair<Slic3r::ModelObject*, Slic3r::ModelVolume*>;
        std::vector<ObjVol> targets;
        for (int idx : matches) {
            auto* obj = state.model.objects[idx];
            if (!obj) continue;
            for (auto* vol : obj->volumes) {
                if (vol && vol->name == part_name)
                    targets.push_back({obj, vol});
            }
        }
        if (targets.empty())
            throw std::out_of_range("part name '" + part_name + "' not found across " +
                                    std::to_string(matches.size()) + " matching object(s)");

        for (auto& [obj, vol] : targets) {
            if (vol->source.input_file.empty()) {
                vol->source.input_file = obj->input_file;
                vol->source.object_idx = 0;
                // Stamp the actual volume index within the parent object.
                for (size_t vi = 0; vi < obj->volumes.size(); ++vi) {
                    if (obj->volumes[vi] == vol) {
                        vol->source.volume_idx = static_cast<int>(vi);
                        break;
                    }
                }
            }
            vol->config.set("extruder", filament_idx);
        }
        r.ok = true;
        return r;
    }

    // Object-level mode: apply to every matching ModelObject.
    for (int idx : matches) {
        auto* obj = state.model.objects[idx];
        if (!obj) continue;

        // Bug B retrofit guard.
        for (auto* vol : obj->volumes) {
            if (vol && vol->source.input_file.empty()) {
                vol->source.input_file = obj->input_file;
                vol->source.object_idx = 0;
                vol->source.volume_idx = 0;
            }
        }
        obj->config.set("extruder", filament_idx);
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

// ---- D2: object merge-parts -----------------------------------------------

std::string merge_object_parts(ProjectState& state,
                               const std::string& name,
                               const MergePartsParams& p)
{
    // Step a: --parts must not be empty.
    if (p.parts.empty())
        throw std::invalid_argument("merge-parts: --parts must not be empty");

    // Step b: First-match on --name.
    Slic3r::ModelObject* obj = nullptr;
    for (auto* o : state.model.objects) {
        if (o->name == name) { obj = o; break; }
    }
    if (!obj)
        throw std::out_of_range("merge-parts: object not found: " + name);

    // Step c: Lookup each part by name in obj.volumes. Track indices.
    std::vector<size_t> src_indices;
    src_indices.reserve(p.parts.size());
    for (const auto& part_name : p.parts) {
        size_t found = static_cast<size_t>(-1);
        for (size_t i = 0; i < obj->volumes.size(); ++i) {
            if (obj->volumes[i]->name == part_name) { found = i; break; }
        }
        if (found == static_cast<size_t>(-1))
            throw std::out_of_range("merge-parts: part not found: " + part_name);
        src_indices.push_back(found);
    }

    // Step d: --into must not already exist in obj.volumes.
    for (auto* v : obj->volumes) {
        if (v->name == p.into)
            throw DuplicateNameError("merge-parts: volume '" + p.into + "' already exists");
    }

    // Step e: Filament range check (if provided).
    const size_t slot_count = get_filament_count(state.project_config);
    if (p.filament != -1) {
        if (p.filament < 1 || static_cast<size_t>(p.filament) > slot_count)
            throw std::out_of_range(
                "merge-parts: filament " + std::to_string(p.filament) +
                " out of range [1, " + std::to_string(slot_count) + "]");
    }

    // Step f: Each source must be MODEL_PART.
    for (size_t idx : src_indices) {
        if (obj->volumes[idx]->type() != Slic3r::ModelVolumeType::MODEL_PART)
            throw std::invalid_argument(
                "merge-parts: part '" + obj->volumes[idx]->name + "' is not MODEL_PART");
    }

    // Step g: Each source mesh must be non-empty.
    for (size_t idx : src_indices) {
        if (obj->volumes[idx]->mesh().empty())
            throw std::invalid_argument(
                "merge-parts: part '" + obj->volumes[idx]->name + "' has empty mesh");
    }

    // Step h: Filament agreement (if --filament not specified).
    int agreed_extruder = p.filament;
    if (p.filament == -1) {
        int first_ext = -1;
        for (size_t idx : src_indices) {
            int e = obj->volumes[idx]->extruder_id();
            if (first_ext == -1) { first_ext = e; }
            else if (e != first_ext)
                throw std::invalid_argument(
                    "merge-parts: parts have different filament assignments; use --filament to resolve");
        }
        agreed_extruder = first_ext;
    }

    // Step i: Per-volume config: only "extruder" key allowed.
    for (size_t idx : src_indices) {
        for (const std::string& key : obj->volumes[idx]->config.keys()) {
            if (key != "extruder")
                throw std::invalid_argument(
                    "merge-parts: part '" + obj->volumes[idx]->name +
                    "' has per-volume config key '" + key + "' (only extruder allowed)");
        }
    }

    // --- All validation passed -- execution ---

    // Find lowest-indexed source for placement and source attribution.
    const size_t min_src_idx =
        *std::min_element(src_indices.begin(), src_indices.end());
    const Slic3r::ModelVolume* lowest_vol = obj->volumes[min_src_idx];
    const std::string  saved_input_file  = lowest_vol->source.input_file;
    const int          saved_object_idx  = lowest_vol->source.object_idx;
    const int          saved_volume_idx  = lowest_vol->source.volume_idx;

    // Build merged TriangleMesh: bake each source's transform, then accumulate.
    Slic3r::TriangleMesh merged;
    for (size_t idx : src_indices) {
        Slic3r::TriangleMesh copy = obj->volumes[idx]->mesh();
        copy.transform(obj->volumes[idx]->get_matrix());
        merged.merge(copy);
    }

    // Capture N before adding merged volume.
    const size_t N_before = obj->volumes.size();

    // Add merged volume at end, bypassing bbox-center shift.
    Slic3r::ModelVolume* new_vol = obj->add_volume(merged, false);
    new_vol->name = p.into;
    new_vol->config.set("extruder", agreed_extruder);
    new_vol->source.input_file = saved_input_file;
    new_vol->source.object_idx = saved_object_idx;
    new_vol->source.volume_idx = saved_volume_idx;

    // Delete source volumes in reverse-index order.
    std::vector<size_t> sorted_desc(src_indices);
    std::sort(sorted_desc.begin(), sorted_desc.end(), std::greater<size_t>());
    for (size_t idx : sorted_desc) {
        obj->delete_volume(idx);
    }
    // new_vol has shifted left by S positions (all sources had idx < N_before).
    // new_vol is now at index (N_before - src_indices.size()).

    // Single-volume serialization shim.
    if (obj->volumes.size() == 1) {
        obj->config.set("extruder", agreed_extruder);
    }

    // Move merged volume to lowest-source-index slot for determinism.
    const size_t S = src_indices.size();
    const size_t current_pos = N_before - S;
    const size_t target_pos  = min_src_idx;
    if (current_pos != target_pos) {
        std::rotate(
            obj->volumes.begin() + static_cast<std::ptrdiff_t>(target_pos),
            obj->volumes.begin() + static_cast<std::ptrdiff_t>(current_pos),
            obj->volumes.begin() + static_cast<std::ptrdiff_t>(current_pos) + 1
        );
    }

    return "merge-parts: " + std::to_string(S) + " parts -> '" +
           p.into + "' in " + name + ".";
}

// ---------------------------------------------------------------------------
// Layout operations (2026-05-29)
// ---------------------------------------------------------------------------

// Shared by plate_center, plate_drop_to_bed, plate_arrange, plate_auto_orient.
// Sources the (obj_idx, instance_idx) pairs from PlateData::objects_and_instances,
// which io.cpp:55-73 rebuilds at load time from loaded_id_to_loc. The
// adjacent obj_inst_map is NOT used here — its post-load key/value
// semantics (instance_idx, loaded_id) make it the wrong source for
// (obj_idx, instance_idx) iteration.
//
// Throws std::out_of_range (exit 6) if no plate matches <plate_name>.
// Empty vector means the plate exists but has no objects.
static std::vector<std::pair<int,int>>
collect_plate_instances(const ProjectState& state,
                        const std::string& plate_name) {
    for (size_t i = 0; i < state.plate_data.size(); ++i) {
        const auto* pd = state.plate_data[i];
        if (!pd) continue;
        if (pd->plate_name != plate_name) continue;
        std::vector<std::pair<int,int>> out;
        out.reserve(pd->objects_and_instances.size());
        for (const auto& p : pd->objects_and_instances)
            out.emplace_back(p.first, p.second);
        return out;
    }
    throw std::out_of_range("plate '" + plate_name + "' not found");
}

// Compute (plate_index_1based, total_plates, bed_width, bed_height) for
// the named plate, plus the bed-local centroid (cx, cy). Used by
// plate_center (and reusable for plate_arrange). Throws std::out_of_range
// if the plate isn't found, std::invalid_argument if printable_area is
// degenerate.
struct PlateBedInfo {
    int           index_1based;
    int           total_plates;
    double        bed_width;
    double        bed_height;
    double        local_cx;   // bed-local centroid X
    double        local_cy;
    Slic3r::Vec3d world_origin;
};

static PlateBedInfo plate_bed_info(const ProjectState& state,
                                   const std::string& plate_name) {
    int idx_1 = 0;
    for (size_t i = 0; i < state.plate_data.size(); ++i) {
        if (state.plate_data[i] &&
            state.plate_data[i]->plate_name == plate_name) {
            idx_1 = static_cast<int>(i) + 1;
            break;
        }
    }
    if (idx_1 == 0)
        throw std::out_of_range("plate '" + plate_name + "' not found");

    const auto* pa_opt =
        state.project_config.option<Slic3r::ConfigOptionPoints>("printable_area");
    if (!pa_opt || pa_opt->values.size() < 3)
        throw std::invalid_argument("printable_area missing or < 3 points");

    const auto& pa = pa_opt->values;
    double cx = 0, cy = 0;
    double min_x = pa.front().x(), max_x = min_x;
    double min_y = pa.front().y(), max_y = min_y;
    for (const auto& p : pa) {
        cx += p.x(); cy += p.y();
        min_x = std::min(min_x, p.x()); max_x = std::max(max_x, p.x());
        min_y = std::min(min_y, p.y()); max_y = std::max(max_y, p.y());
    }
    cx /= static_cast<double>(pa.size());
    cy /= static_cast<double>(pa.size());

    PlateBedInfo info;
    info.index_1based  = idx_1;
    info.total_plates  = static_cast<int>(state.plate_data.size());
    info.bed_width     = max_x - min_x;
    info.bed_height    = max_y - min_y;
    info.local_cx      = cx;
    info.local_cy      = cy;
    info.world_origin  = plate_world_origin(info.index_1based, info.total_plates,
                                            info.bed_width, info.bed_height);
    return info;
}

OpResult plate_center(ProjectState& state, const std::string& plate_name) {
    auto pairs = collect_plate_instances(state, plate_name);
    if (pairs.empty()) {
        OpResult r; r.ok = true; return r;
    }
    auto info = plate_bed_info(state, plate_name);
    const double target_x = info.world_origin.x() + info.local_cx;
    const double target_y = info.world_origin.y() + info.local_cy;

    for (const auto& [oi, ii] : pairs) {
        auto* inst = state.model.objects[oi]->instances[ii];
        // World-space XY centroid of the instance's mesh AABB.
        Slic3r::BoundingBoxf3 bb =
            state.model.objects[oi]->instance_bounding_box(ii, false);
        const double cx_now = 0.5 * (bb.min.x() + bb.max.x());
        const double cy_now = 0.5 * (bb.min.y() + bb.max.y());
        const auto off = inst->get_offset();
        inst->set_offset(Slic3r::Vec3d(
            off.x() + (target_x - cx_now),
            off.y() + (target_y - cy_now),
            off.z()));
    }
    OpResult r; r.ok = true; return r;
}

// World-space min-Z across all volumes of a single instance, using each
// volume's convex hull (typically 10-100 verts) rather than the full mesh
// (up to ~100K verts). Mathematically equivalent — the lowest-Z vertex
// is always extreme and therefore on the hull — but materially faster
// for batch composition with many instances.
//
// Composes instance × volume transforms (matches GUI's
// GLVolume::world_matrix at slic3r/GUI/Gizmos/GizmoObjectManipulation.cpp:38).
// Missing the volume transform mis-drops multi-volume objects loaded
// from a multi-part 3MF.
static double instance_world_min_z(const Slic3r::ModelObject& obj,
                                   const Slic3r::ModelInstance& inst) {
    const Slic3r::Transform3d inst_m = inst.get_transformation().get_matrix();
    double min_z = std::numeric_limits<double>::max();
    for (const auto* mv : obj.volumes) {
        if (!mv) continue;
        const Slic3r::Transform3d world_m =
            inst_m * mv->get_transformation().get_matrix();
        const Slic3r::TriangleMesh& hull = mv->get_convex_hull();
        for (const auto& v : hull.its.vertices) {
            const Slic3r::Vec3d w = world_m * v.cast<double>();
            if (w.z() < min_z) min_z = w.z();
        }
    }
    return (min_z == std::numeric_limits<double>::max()) ? 0.0 : min_z;
}

OpResult plate_drop_to_bed(ProjectState& state, const std::string& plate_name) {
    auto pairs = collect_plate_instances(state, plate_name);
    if (pairs.empty()) {
        OpResult r; r.ok = true; return r;
    }
    for (const auto& [oi, ii] : pairs) {
        const auto& obj = *state.model.objects[oi];
        auto* inst = state.model.objects[oi]->instances[ii];
        const double mz = instance_world_min_z(obj, *inst);
        const auto off = inst->get_offset();
        inst->set_offset(Slic3r::Vec3d(off.x(), off.y(), off.z() - mz));
    }
    OpResult r; r.ok = true; return r;
}

OpResult plate_auto_orient(ProjectState& state, const std::string& plate_name) {
    auto pairs = collect_plate_instances(state, plate_name);
    if (pairs.empty()) {
        OpResult r; r.ok = true; return r;
    }
    for (const auto& [oi, ii] : pairs) {
        auto* inst = state.model.objects[oi]->instances[ii];
        Slic3r::orientation::orient(inst);
    }
    // Implicit drop after orient — rotation typically leaves the object
    // off the bed in Z. Per spec, auto-orient always finishes with drop.
    return plate_drop_to_bed(state, plate_name);
}

OpResult object_auto_orient(ProjectState& state,
                            const std::string& object_name) {
    std::vector<int> matched_obj_idx;
    for (size_t i = 0; i < state.model.objects.size(); ++i) {
        if (state.model.objects[i] &&
            state.model.objects[i]->name == object_name)
            matched_obj_idx.push_back(static_cast<int>(i));
    }
    if (matched_obj_idx.empty())
        throw std::out_of_range("object '" + object_name + "' not found");

    for (int oi : matched_obj_idx) {
        auto& obj = *state.model.objects[oi];
        for (size_t ii = 0; ii < obj.instances.size(); ++ii) {
            auto* inst = obj.instances[ii];
            Slic3r::orientation::orient(inst);
            const double mz = instance_world_min_z(obj, *inst);
            const auto off = inst->get_offset();
            inst->set_offset(Slic3r::Vec3d(off.x(), off.y(), off.z() - mz));
        }
    }
    OpResult r; r.ok = true; return r;
}

OpResult plate_arrange(ProjectState& state, const std::string& plate_name) {
    auto pairs = collect_plate_instances(state, plate_name);
    if (pairs.empty()) {
        OpResult r; r.ok = true; return r;
    }
    const auto info = plate_bed_info(state, plate_name);   // throws on degenerate

    // Build ArrangePolygons from instances. get_instance_arrange_poly
    // emits translation in world coords (Model.cpp:4240).
    Slic3r::arrangement::ArrangePolygons items;
    items.reserve(pairs.size());
    for (const auto& [oi, ii] : pairs) {
        auto* inst = state.model.objects[oi]->instances[ii];
        Slic3r::arrangement::ArrangePolygon ap =
            Slic3r::get_instance_arrange_poly(inst, state.project_config);
        // Normalize world-coord translation to plate-local. For plate 1
        // this subtracts zero. For plate >= 2 it removes the BBS stride
        // so the arrange engine sees bed-local input (otherwise the item
        // is way outside the bed polygon at bed_idx 0).
        ap.translation -= Slic3r::Vec2crd(
            Slic3r::scaled<coord_t>(info.world_origin.x()),
            Slic3r::scaled<coord_t>(info.world_origin.y()));
        items.emplace_back(std::move(ap));
    }

    // Build excludes from bed_exclude_area (consecutive groups of 4
    // rectangular points). Stricter than GUI: malformed counts throw,
    // per spec divergence note.
    Slic3r::arrangement::ArrangePolygons excludes;
    if (const auto* exc_opt = state.project_config.option<
            Slic3r::ConfigOptionPoints>("bed_exclude_area")) {
        const auto& pts = exc_opt->values;
        if (pts.size() % 4 != 0)
            throw std::invalid_argument(
                "arrange: bed_exclude_area malformed (point count not multiple of 4)");
        for (size_t i = 0; i + 3 < pts.size(); i += 4) {
            Slic3r::arrangement::ArrangePolygon e;
            for (size_t k = 0; k < 4; ++k)
                e.poly.contour.append(Slic3r::Point(
                    Slic3r::scaled<coord_t>(pts[i + k].x()),
                    Slic3r::scaled<coord_t>(pts[i + k].y())));
            e.bed_idx       = 0;   // explicit; matches PartPlate.cpp:5815
            e.is_virt_object = true;
            excludes.emplace_back(std::move(e));
        }
    }

    // Populate ArrangeParams from project config via libslic3r helpers.
    Slic3r::arrangement::ArrangeParams params;
    Slic3r::arrangement::update_arrange_params(params, state.project_config,
                                               items);
    Slic3r::arrangement::update_selected_items_inflation(items,
        state.project_config, params);
    // Deliberate CLI divergence from GUI slice path (BambuStudio.cpp:5351):
    // headless batch composition benefits unconditionally from rotation.
    params.allow_rotations = true;

    // Bed points respecting bed_shrink_* (params just got populated).
    Slic3r::Points bedpts =
        Slic3r::arrangement::get_shrink_bedpts(state.project_config, params);
    if (bedpts.size() < 3)
        throw std::invalid_argument(
            "arrange: project has no usable printable_area");

    // Run the engine.
    Slic3r::arrangement::arrange(items, excludes, bedpts, params);

    // Overflow check BEFORE applying offsets — state rollback on throw.
    int overflow = 0;
    for (const auto& ap : items) if (ap.bed_idx != 0) ++overflow;
    if (overflow > 0)
        throw PlacementFailure("arrange: " + std::to_string(overflow) +
                               " object(s) did not fit on plate '" +
                               plate_name + "'");

    // Apply: translate bed-local result back to world via plate_origin.
    for (size_t k = 0; k < pairs.size(); ++k) {
        const auto& [oi, ii] = pairs[k];
        auto* inst = state.model.objects[oi]->instances[ii];
        const auto cur = inst->get_offset();
        inst->set_offset(Slic3r::Vec3d(
            info.world_origin.x() + Slic3r::unscaled<double>(items[k].translation.x()),
            info.world_origin.y() + Slic3r::unscaled<double>(items[k].translation.y()),
            cur.z()));
        inst->set_rotation(Slic3r::Vec3d(0, 0, items[k].rotation));
    }
    OpResult r; r.ok = true; return r;
}

} // namespace bambu_cli
