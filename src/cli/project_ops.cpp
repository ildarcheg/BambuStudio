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

    // 1. Load STL into a throwaway Model, then move the mesh out.
    Slic3r::Model tmp_model;
    if (!Slic3r::load_stl(stl_path.c_str(), &tmp_model)) {
        r.exit_code = 3; r.error_code = "parse_failure";
        r.error_message = "load_stl returned false for: " + stl_path;
        return r;
    }
    if (tmp_model.objects.empty() || tmp_model.objects[0]->volumes.empty()) {
        r.exit_code = 3; r.error_code = "parse_failure";
        r.error_message = "stl loaded with no geometry: " + stl_path;
        return r;
    }
    Slic3r::TriangleMesh mesh = tmp_model.objects[0]->volumes[0]->mesh();

    // 2. Derive object name.
    std::string name = object_name_override.empty()
                       ? derive_object_name(stl_path) : object_name_override;

    Slic3r::PlateData* pd = state.plate_data[plate_idx];
    const bool manual = tf && (tf->has_translate || tf->has_rotate || tf->has_scale);
    const int  copies = std::max(1, count);
    // Track the first loaded_id we'll assign for rollback range.
    const size_t base_loaded_id = static_cast<size_t>(pd->obj_inst_map.size() + 1);
    // Track the first object index we'll add for rollback.
    const int base_obj_idx = static_cast<int>(state.model.objects.size());

    // 3–5. For each copy, create a fresh ModelObject with its own volume and instance.
    //   BBS 3MF format maps object_id (ModelObject's 3MF id) one-to-one with instances.
    //   Creating separate ModelObjects per copy ensures correct round-trip through save/load.
    for (int k = 0; k < copies; ++k) {
        // Create ModelObject for this copy.
        Slic3r::ModelObject* obj_k = state.model.add_object();
        obj_k->name       = name;
        obj_k->input_file = stl_path;
        int obj_idx_k = static_cast<int>(state.model.objects.size()) - 1;

        // Add volume + STAMP SOURCE ATTRIBUTION (G/Bug-B fix, day one).
        Slic3r::ModelVolume* vol_k = obj_k->add_volume(mesh);
        if (vol_k) {
            vol_k->source.input_file = stl_path;
            vol_k->source.object_idx = 0;
            vol_k->source.volume_idx = 0;
        }

        // Add the single instance for this copy.
        Slic3r::ModelInstance* inst_k = obj_k->add_instance();
        inst_k->set_offset(Slic3r::Vec3d::Zero());
        inst_k->set_rotation(Slic3r::Vec3d::Zero());
        inst_k->set_scaling_factor(Slic3r::Vec3d(1, 1, 1));

        if (manual) {
            // Apply T·R·S: scale first, then rotate, then translate.
            if (tf->has_scale)
                inst_k->set_scaling_factor(Slic3r::Vec3d(tf->sx, tf->sy, tf->sz));
            if (tf->has_rotate) {
                static const double DEG2RAD = 0.01745329251994329577;
                inst_k->set_rotation(Slic3r::Vec3d(tf->rx * DEG2RAD,
                                                    tf->ry * DEG2RAD,
                                                    tf->rz * DEG2RAD));
            }
            if (tf->has_translate)
                inst_k->set_offset(Slic3r::Vec3d(tf->tx, tf->ty, tf->tz));
        } else {
            // Auto-arrange: bbox-aware step-grid fallback (G5).
            //   place the object so its world-space bbox starts at
            //   (margin + col*step, margin + row*step, 0), regardless of STL origin.
            Slic3r::BoundingBoxf3 bbox = mesh.bounding_box();
            const double step   = 60.0;
            const double margin = 10.0;
            size_t existing = pd->objects_and_instances.size();
            double row = static_cast<double>(existing / 4);
            double col = static_cast<double>(existing % 4);
            Slic3r::Vec3d offset(
                (col * step + margin) - bbox.min.x(),
                (row * step + margin) - bbox.min.y(),
                -bbox.min.z()
            );
            inst_k->set_offset(offset);
        }

        // Assign loaded_id (= identify_id in 3MF). Each copy has a unique id.
        // obj_inst_map: key=3mf_object_id (here we use obj_idx_k), value={inst_idx=0, loaded_id}
        // list_objects() reads kv.second.second == inst->loaded_id for plate matching.
        size_t new_loaded_id = static_cast<size_t>(pd->obj_inst_map.size() + 1);
        inst_k->loaded_id = new_loaded_id;
        pd->obj_inst_map[obj_idx_k] = {0, static_cast<int>(new_loaded_id)};
        pd->objects_and_instances.push_back({obj_idx_k, 0});

        // Off-bed check (manual transforms only — auto-arrange is safe by construction).
        if (manual) {
            // Compute world-space AABB: apply scale then translate to mesh bbox.
            // (Rotation changes orientation but not the conservative AABB extent
            //  we compute here; for the off-bed guard we use the mesh's local bbox
            //  scaled + translated, which is conservative and correct for the
            //  common "place flat on bed" use-case.)
            Slic3r::BoundingBoxf3 bbox = mesh.bounding_box();
            double sx = tf->has_scale ? tf->sx : 1.0;
            double sy = tf->has_scale ? tf->sy : 1.0;
            // sz not used in the 2D bed AABB check (z is unconstrained).
            (void)(tf->has_scale ? tf->sz : 1.0);
            double tx = tf->has_translate ? tf->tx : 0.0;
            double ty = tf->has_translate ? tf->ty : 0.0;

            // Scaled min/max (handle negative scale by swapping)
            double bx0 = std::min(bbox.min.x() * sx, bbox.max.x() * sx) + tx;
            double bx1 = std::max(bbox.min.x() * sx, bbox.max.x() * sx) + tx;
            double by0 = std::min(bbox.min.y() * sy, bbox.max.y() * sy) + ty;
            double by1 = std::max(bbox.min.y() * sy, bbox.max.y() * sy) + ty;

            // Bed AABB: read printable_area (ConfigOptionPoints, 4 corners).
            // Priority: pd->config > state.project_config > [0..256] fallback.
            double bed_minx = 0.0, bed_miny = 0.0, bed_maxx = 256.0, bed_maxy = 256.0;
            const Slic3r::ConfigOption* pa_opt = nullptr;
            if (pd->config.has("printable_area"))
                pa_opt = pd->config.option("printable_area");
            if (!pa_opt && state.project_config.has("printable_area"))
                pa_opt = state.project_config.option("printable_area");
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

            const double margin = 0.001;
            if (bx0 < bed_minx - margin || by0 < bed_miny - margin ||
                bx1 > bed_maxx + margin || by1 > bed_maxy + margin) {
                // Roll back: delete all ModelObjects we added (base_obj_idx..current).
                // Delete in reverse order to avoid index shifts.
                int cur_obj_count = static_cast<int>(state.model.objects.size());
                for (int ri = cur_obj_count - 1; ri >= base_obj_idx; --ri)
                    state.model.delete_object(static_cast<size_t>(ri));
                // Also clean up plate data.
                for (int k2 = 0; k2 < copies && !pd->objects_and_instances.empty(); ++k2)
                    pd->objects_and_instances.pop_back();
                for (auto it = pd->obj_inst_map.begin(); it != pd->obj_inst_map.end(); ) {
                    size_t eid = static_cast<size_t>(it->second.second);
                    if (eid >= base_loaded_id && eid < base_loaded_id + static_cast<size_t>(copies))
                        it = pd->obj_inst_map.erase(it);
                    else
                        ++it;
                }
                r.exit_code = 9; r.error_code = "placement_failure";
                std::ostringstream os;
                os << "object '" << name << "' bbox ["
                   << bx0 << "," << by0 << "..." << bx1 << "," << by1
                   << "] off-bed (plate '" << plate_name << "' AABB ["
                   << bed_minx << "," << bed_miny << ".." << bed_maxx << "," << bed_maxy << "])";
                r.error_message = os.str();
                return r;
            }
        }
    }

    // 6. Filament validation + assignment.
    //    -1 means "not specified" (skip). Any other value is validated as
    //    1-based extruder slot against filament_settings_id slot count.
    //    Validate AFTER attach (so we can roll back cleanly via delete_object).
    if (filament_idx != -1) {
        const Slic3r::ConfigOption* slots_opt =
            state.project_config.option("filament_settings_id");
        size_t slot_count = 0;
        if (auto* vs = dynamic_cast<const Slic3r::ConfigOptionStrings*>(slots_opt))
            slot_count = vs->values.size();
        if (filament_idx < 1 || filament_idx > static_cast<int>(slot_count)) {
            // Roll back: delete all ModelObjects we added.
            int cur_obj_count = static_cast<int>(state.model.objects.size());
            for (int ri = cur_obj_count - 1; ri >= base_obj_idx; --ri)
                state.model.delete_object(static_cast<size_t>(ri));
            for (int k2 = 0; k2 < copies && !pd->objects_and_instances.empty(); ++k2)
                pd->objects_and_instances.pop_back();
            for (auto it = pd->obj_inst_map.begin(); it != pd->obj_inst_map.end(); ) {
                size_t eid = static_cast<size_t>(it->second.second);
                if (eid >= base_loaded_id && eid < base_loaded_id + static_cast<size_t>(copies))
                    it = pd->obj_inst_map.erase(it);
                else
                    ++it;
            }
            r.exit_code = 1; r.error_code = "usage_error";
            r.error_message = "filament " + std::to_string(filament_idx) +
                              " out of range [1," + std::to_string(slot_count) + "]";
            return r;
        }
        // Apply extruder assignment to all copies.
        for (int ki = 0; ki < copies; ++ki) {
            auto* obj_ki = state.model.objects[base_obj_idx + ki];
            if (obj_ki) obj_ki->config.set("extruder", filament_idx);
        }
    }

    if (out_ref) {
        out_ref->object_idx   = base_obj_idx;  // first copy's index
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
