#include "../exit_codes.hpp"
#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_ops.hpp"
#include "../project_state.hpp"
#include "../extern/CLI11/CLI11.hpp"

#include <memory>
#include <sstream>
#include <vector>

namespace bambu_cli {

// Parse "x,y[,z]" or uniform "s" into three doubles.
// z_default is used when only 2 values are provided (translate/rotate use 0,
// scale uses 1).
// Returns false if the string is malformed.
static bool parse_triple(const std::string& s, double& x, double& y, double& z,
                         double z_default) {
    if (s.empty()) return false;
    std::vector<double> v;
    std::string cur;
    for (char c : s) {
        if (c == ',') { v.push_back(std::stod(cur)); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) v.push_back(std::stod(cur));
    if (v.size() == 1) { x = y = z = v[0]; return true; }   // uniform (e.g. scale)
    if (v.size() == 2) { x = v[0]; y = v[1]; z = z_default; return true; }
    if (v.size() == 3) { x = v[0]; y = v[1]; z = v[2]; return true; }
    return false;
}

struct ObjectAddArgs {
    std::string in_path;
    std::string plate;
    std::string stl;
    std::string name;
    std::string out_path;
    int         filament  = -1;  // -1 = not specified; 1..N = 1-based extruder slot
    int         count     = 1;
    std::string translate; // "x,y[,z]" in mm
    std::string rotate;    // "rx,ry[,rz]" in degrees
    std::string scale;     // uniform "s" OR per-axis "x,y[,z]"
};

struct ObjectListArgs {
    std::string in_path;
    std::string only_plate;
};

void register_object_subcommands(CLI::App& app, OutputMode* mode_out) {
    auto* object = app.add_subcommand("object", "object-level operations");

    // --- object add ---------------------------------------------------
    auto* add = object->add_subcommand("add", "add an STL as an object on a named plate");
    auto a = std::make_shared<ObjectAddArgs>();
    add->add_option("in",        a->in_path,  "input .3mf")->required();
    add->add_option("--plate",   a->plate,    "target plate name")->required();
    add->add_option("--stl",     a->stl,      "STL file to add")->required();
    add->add_option("--name",    a->name,     "explicit object name (default: stem of --stl)");
    add->add_option("--output",  a->out_path, "output .3mf (defaults to in-place)");
    add->add_option("--filament",  a->filament,  "1-based extruder/filament slot");
    add->add_option("--count",     a->count,     "number of copies (default 1)");
    add->add_option("--translate", a->translate, "x,y[,z] in plate-local mm");
    add->add_option("--rotate",    a->rotate,    "rx,ry[,rz] in degrees");
    add->add_option("--scale",     a->scale,     "uniform s OR per-axis x,y[,z]");
    add->callback([a, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;
        ProjectState state;
        IoResult lr = load_project(a->in_path, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }

        // Build ManualTransform from flags (if any supplied).
        ManualTransform tf;
        if (!a->translate.empty()) {
            tf.has_translate = true;
            if (!parse_triple(a->translate, tf.tx, tf.ty, tf.tz, 0.0)) {
                emit_error(mode, "usage_error", "bad --translate: " + a->translate);
                std::exit(to_int(ExitCode::usage_error));
            }
        }
        if (!a->rotate.empty()) {
            tf.has_rotate = true;
            if (!parse_triple(a->rotate, tf.rx, tf.ry, tf.rz, 0.0)) {
                emit_error(mode, "usage_error", "bad --rotate: " + a->rotate);
                std::exit(to_int(ExitCode::usage_error));
            }
        }
        if (!a->scale.empty()) {
            tf.has_scale = true;
            if (!parse_triple(a->scale, tf.sx, tf.sy, tf.sz, 1.0)) {
                emit_error(mode, "usage_error", "bad --scale: " + a->scale);
                std::exit(to_int(ExitCode::usage_error));
            }
        }
        const ManualTransform* tf_ptr = (tf.has_translate || tf.has_rotate || tf.has_scale)
                                        ? &tf : nullptr;

        ObjectRef ref;
        OpResult op = add_object_to_plate(state, a->plate, a->stl, a->name,
                                          a->filament, tf_ptr, a->count, &ref);
        if (!op.ok) { emit_error(mode, op.error_code, op.error_message); std::exit(op.exit_code); }

        const std::string& out = a->out_path.empty() ? a->in_path : a->out_path;
        IoResult sr = save_project(state, out);
        if (!sr.ok) { emit_error(mode, sr.error_code, sr.error_message); std::exit(sr.exit_code); }

        emit_ok(mode, "ok", "object added: " + ref.object_name + " -> " + out);
    });

    // --- object list --------------------------------------------------
    auto* lst = object->add_subcommand("list", "list objects (optionally per plate)");
    auto l = std::make_shared<ObjectListArgs>();
    lst->add_option("in",        l->in_path,    "input .3mf")->required();
    lst->add_option("--plate",   l->only_plate, "filter to one plate");
    lst->callback([l, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;
        ProjectState state;
        IoResult lr = load_project(l->in_path, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }

        std::vector<ListedObject> objs = list_objects(state, l->only_plate);
        if (mode == OutputMode::Json) {
            nlohmann::json data;
            data["object_count"] = objs.size();
            data["objects"]      = nlohmann::json::array();
            for (const auto& o : objs) {
                data["objects"].push_back({
                    {"plate",    o.plate_name},
                    {"name",     o.object_name},
                    {"extruder", o.extruder},
                });
            }
            emit_ok(mode, "ok", "object list", data);
        } else {
            std::ostringstream m;
            for (const auto& o : objs)
                m << "[" << o.plate_name << "] " << o.object_name
                  << " (extruder=" << o.extruder << ")\n";
            emit_ok(mode, "ok", m.str());
        }
    });

    // --- object remove ------------------------------------------------
    // Removes ALL ModelObjects whose name matches (group-by-name semantics).
    struct ORmArgs { std::string in, name, out; };
    auto* orm = object->add_subcommand("remove", "remove all objects with the given name");
    auto ora = std::make_shared<ORmArgs>();
    orm->add_option("in",       ora->in,   "input .3mf")->required();
    orm->add_option("--name",   ora->name, "object name (all copies removed)")->required();
    orm->add_option("--output", ora->out,  "output .3mf (defaults to in-place)");
    orm->callback([ora, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;
        ProjectState state;
        IoResult lr = load_project(ora->in, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }
        OpResult op = remove_object(state, ora->name);
        if (!op.ok) { emit_error(mode, op.error_code, op.error_message); std::exit(op.exit_code); }
        const std::string& out = ora->out.empty() ? ora->in : ora->out;
        IoResult sr = save_project(state, out);
        if (!sr.ok) { emit_error(mode, sr.error_code, sr.error_message); std::exit(sr.exit_code); }
        emit_ok(mode, "ok", "object removed: " + ora->name);
    });

    // --- object set-filament ------------------------------------------
    // Stamps extruder=N on ALL ModelObjects with the given name (group-by-name).
    // Applies Bug B retrofit guard before setting the extruder key.
    struct SFArgs { std::string in, name, out; int filament = 0; int part = -1; };
    auto* sf = object->add_subcommand("set-filament", "retrofit filament slot on all objects with the given name");
    auto sa = std::make_shared<SFArgs>();
    sf->add_option("in",         sa->in,       "input .3mf")->required();
    sf->add_option("--name",     sa->name,     "object name (all copies updated)")->required();
    sf->add_option("--filament", sa->filament, "1-based filament slot")->required();
    sf->add_option("--part",     sa->part,     "0-based volume/part index (omit for object-level)");
    sf->add_option("--output",   sa->out,      "output .3mf (defaults to in-place)");
    sf->callback([sa, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;
        ProjectState state;
        IoResult lr = load_project(sa->in, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }
        OpResult op = set_object_filament(state, sa->name, sa->filament, sa->part);
        if (!op.ok) { emit_error(mode, op.error_code, op.error_message); std::exit(op.exit_code); }
        const std::string& out = sa->out.empty() ? sa->in : sa->out;
        IoResult sr = save_project(state, out);
        if (!sr.ok) { emit_error(mode, sr.error_code, sr.error_message); std::exit(sr.exit_code); }
        if (sa->part >= 0) {
            emit_ok(mode, "ok", "set-filament: " + sa->name + " part " + std::to_string(sa->part) +
                                " -> " + std::to_string(sa->filament));
        } else {
            emit_ok(mode, "ok", "set-filament: " + sa->name + " -> " + std::to_string(sa->filament));
        }
    });
}

} // namespace bambu_cli
