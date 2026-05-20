#include "project_ops.hpp"

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
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

    // 2. Create a fresh ModelObject in our project's Model.
    std::string name = object_name_override.empty()
                       ? derive_object_name(stl_path) : object_name_override;
    Slic3r::ModelObject* obj = state.model.add_object();
    obj->name = name;
    obj->input_file = stl_path;

    // 3. Add the volume + STAMP SOURCE ATTRIBUTION (G/Bug-B fix, day one).
    Slic3r::ModelVolume* vol = obj->add_volume(mesh);
    if (vol) {
        vol->source.input_file = stl_path;
        vol->source.object_idx = 0;
        vol->source.volume_idx = 0;
    }

    // 4. Add an instance at origin. Auto-arrange follows in step 5.
    Slic3r::ModelInstance* inst = obj->add_instance();
    inst->set_offset(Slic3r::Vec3d::Zero());
    inst->set_rotation(Slic3r::Vec3d::Zero());
    inst->set_scaling_factor(Slic3r::Vec3d(1, 1, 1));

    int obj_idx  = static_cast<int>(state.model.objects.size()) - 1;
    int inst_idx = 0;

    // 5. Attach to the target plate. We push into both obj_inst_map and
    // objects_and_instances so the writer emits the plate->object link.
    Slic3r::PlateData* pd = state.plate_data[plate_idx];
    inst->loaded_id = static_cast<size_t>(pd->obj_inst_map.size() + 1);
    pd->obj_inst_map[static_cast<int>(inst->loaded_id)] = {obj_idx, inst_idx};
    pd->objects_and_instances.push_back({obj_idx, inst_idx});

    // 6. Auto-arrange this single instance within the plate (G5 grid fallback).
    //    bbox-aware step-grid: place the object such that its world-space bbox
    //    starts at (margin + col*step, margin + row*step, 0). This guarantees:
    //      - the object sits cleanly on the bed (z bbox.min == 0; resting)
    //      - the object's bbox starts in the positive quadrant (no edge spill)
    //    regardless of the STL's local origin convention (centered, corner, etc.).
    //    This is a deliberately naive grid — it does NOT account for existing
    //    objects already on the plate. Collisions are possible if the plate has
    //    objects positioned where the grid cells land. M6 introduces explicit
    //    --translate for users who need deterministic placement.
    {
        Slic3r::BoundingBoxf3 bbox = mesh.bounding_box();
        const double step   = 60.0;   // mm between grid cells
        const double margin = 10.0;   // mm from plate origin to first cell's bbox.min
        size_t nth = pd->objects_and_instances.size() - 1;   // newly-added (0-based)
        double row = static_cast<double>(nth / 4);
        double col = static_cast<double>(nth % 4);
        Slic3r::Vec3d offset(
            (col * step + margin) - bbox.min.x(),
            (row * step + margin) - bbox.min.y(),
            -bbox.min.z()
        );
        inst->set_offset(offset);
    }

    // 7. Filament validation + assignment (1-based; 0 means unset).
    //    Validate AFTER attach (so we can roll back cleanly).
    if (filament_idx != 0) {
        const Slic3r::ConfigOption* slots_opt =
            state.project_config.option("filament_settings_id");
        size_t slot_count = 0;
        if (auto* vs = dynamic_cast<const Slic3r::ConfigOptionStrings*>(slots_opt))
            slot_count = vs->values.size();
        if (filament_idx < 1 || filament_idx > static_cast<int>(slot_count)) {
            // Roll back: detach from plate, then delete the model object.
            pd->objects_and_instances.pop_back();
            pd->obj_inst_map.erase(static_cast<int>(inst->loaded_id));
            state.model.delete_object(static_cast<size_t>(obj_idx));
            r.exit_code = 1; r.error_code = "usage_error";
            r.error_message = "filament " + std::to_string(filament_idx) +
                              " out of range [1," + std::to_string(slot_count) + "]";
            return r;
        }
        obj->config.set("extruder", filament_idx);
    }

    if (out_ref) {
        out_ref->object_idx   = obj_idx;
        out_ref->instance_idx = inst_idx;
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
