#include "project_ops.hpp"

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <sstream>
#include <boost/filesystem.hpp>

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
    if (name.empty()) {
        r.exit_code = 1; r.error_code = "usage_error";
        r.error_message = "plate name must be non-empty";
        return r;
    }
    for (const auto* p : state.plate_data) {
        if (p && p->plate_name == name) {
            r.exit_code = 5; r.error_code = "duplicate_name";
            r.error_message = "plate '" + name + "' already exists";
            return r;
        }
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
static Slic3r::Vec3d plate_world_origin(int plate_index,
                                        double bed_width, double bed_height) {
    // BBS PartPlateList::LOGICAL_PART_PLATE_GAP = 1.0/5.0 = 0.2
    static const double LOGICAL_PART_PLATE_GAP = 0.2;
    double stride_x = bed_width  * (1.0 + LOGICAL_PART_PLATE_GAP);
    double stride_y = bed_height * (1.0 + LOGICAL_PART_PLATE_GAP);

    // Use enough columns for the current plate (conservative estimate).
    // The empirically-derived formula: cols = compute_colum_count(plate_index)
    // For simplicity and CLI correctness, compute cols for plate_index plates.
    float v = std::sqrt(static_cast<float>(plate_index));
    float rv = std::round(v);
    int cols = (v > rv) ? static_cast<int>(rv) + 1 : static_cast<int>(rv);
    if (cols < 1) cols = 1;

    int pi = plate_index - 1;   // 0-based
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
    if (!boost::filesystem::exists(stl_path)) {
        r.exit_code = 2; r.error_code = "file_not_found";
        r.error_message = "stl not found: " + stl_path;
        return r;
    }
    int plate_idx = find_plate_by_name(state, plate_name);
    if (plate_idx < 0) {
        r.exit_code = 6; r.error_code = "unknown_reference";
        r.error_message = "plate '" + plate_name + "' not found";
        return r;
    }

    // Count clamp: 0 or negative → 1.
    const int copies = std::max(1, count);

    // 1. Load STL into a scratch Model once; we'll deep-copy the object N times.
    //    add_object(const ModelObject&) re-issues internal ObjectIDs on each copy so
    //    each ModelObject is fully independent and gets a distinct 3MF object_id on
    //    save.  The BBS 3MF format requires a distinct object_id per plate
    //    obj_inst_map entry (map key collision otherwise drops all but one entry on
    //    reload, breaking list_objects and objects_and_instances reconstruction).
    Slic3r::Model scratch_model;
    if (!Slic3r::load_stl(stl_path.c_str(), &scratch_model)) {
        r.exit_code = 3; r.error_code = "parse_failure";
        r.error_message = "load_stl returned false for: " + stl_path;
        return r;
    }
    if (scratch_model.objects.empty() || scratch_model.objects[0]->volumes.empty()) {
        r.exit_code = 3; r.error_code = "parse_failure";
        r.error_message = "stl loaded with no geometry: " + stl_path;
        return r;
    }

    // Cache the mesh bbox BEFORE the loop (same mesh for all copies).
    Slic3r::TriangleMesh mesh_cache = scratch_model.objects[0]->volumes[0]->mesh();
    Slic3r::BoundingBoxf3 bbox = mesh_cache.bounding_box();

    // Derive object name once.
    const std::string name = object_name_override.empty()
                             ? derive_object_name(stl_path) : object_name_override;

    Slic3r::PlateData* pd = state.plate_data[plate_idx];
    const bool manual = tf && (tf->has_translate || tf->has_rotate || tf->has_scale);

    // Track rollback range.
    const size_t base_loaded_id = static_cast<size_t>(pd->obj_inst_map.size() + 1);
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
                r.exit_code = 9; r.error_code = "placement_failure";
                std::ostringstream os;
                os << "object '" << name << "' bbox ["
                   << bx0 << "," << by0 << "..." << bx1 << "," << by1
                   << "] off-bed (plate '" << plate_name << "' AABB ["
                   << plate_bed_minx << "," << plate_bed_miny
                   << ".." << plate_bed_maxx << "," << plate_bed_maxy << "])";
                r.error_message = os.str();
                return r;
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
            r.exit_code = 1; r.error_code = "usage_error";
            r.error_message = "filament " + std::to_string(filament_idx) +
                              " out of range [1," + std::to_string(slot_count) + "]";
            return r;
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

} // namespace bambu_cli
