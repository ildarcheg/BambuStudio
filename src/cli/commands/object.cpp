#include "../exit_codes.hpp"
#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_ops.hpp"
#include "../project_state.hpp"
#include "../extern/CLI11/CLI11.hpp"
#include "mutation_runner.hpp"

#include <memory>
#include <sstream>
#include <typeindex>
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
        const std::string& out = a->out_path.empty() ? a->in_path : a->out_path;

        // CLI-arg parsing (independent of project state). Bad input throws
        // std::invalid_argument which run_mutation's default map routes to
        // usage_error (exit 1). Run this inside the mutator so the run_mutation
        // envelope handles the error path too.
        ObjectRef ref;
        run_mutation(mode, a->in_path, out, [&](ProjectState& state) {
            ManualTransform tf;
            if (!a->translate.empty()) {
                tf.has_translate = true;
                if (!parse_triple(a->translate, tf.tx, tf.ty, tf.tz, 0.0))
                    throw std::invalid_argument("bad --translate: " + a->translate);
            }
            if (!a->rotate.empty()) {
                tf.has_rotate = true;
                if (!parse_triple(a->rotate, tf.rx, tf.ry, tf.rz, 0.0))
                    throw std::invalid_argument("bad --rotate: " + a->rotate);
            }
            if (!a->scale.empty()) {
                tf.has_scale = true;
                if (!parse_triple(a->scale, tf.sx, tf.sy, tf.sz, 1.0))
                    throw std::invalid_argument("bad --scale: " + a->scale);
            }
            const ManualTransform* tf_ptr =
                (tf.has_translate || tf.has_rotate || tf.has_scale) ? &tf : nullptr;

            add_object_to_plate(state, a->plate, a->stl, a->name,
                                a->filament, tf_ptr, a->count, &ref);
            return "object added: " + ref.object_name + " -> " + out;
        });
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
        emit_list_response<ListedObject>(
            mode, "object list", "object_count", "objects", objs,
            [](const ListedObject& o) -> nlohmann::json {
                return {
                    {"plate",    o.plate_name},
                    {"name",     o.object_name},
                    {"extruder", o.extruder},
                };
            },
            [](std::size_t, const ListedObject& o) {
                std::ostringstream s;
                s << "[" << o.plate_name << "] " << o.object_name
                  << " (extruder=" << o.extruder << ")\n";
                return s.str();
            });
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
        const std::string& out = ora->out.empty() ? ora->in : ora->out;
        run_mutation(mode, ora->in, out, [&](ProjectState& state) {
            remove_object(state, ora->name);
            return "object removed: " + ora->name;
        });
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
        const std::string& out = sa->out.empty() ? sa->in : sa->out;
        run_mutation(mode, sa->in, out, [&](ProjectState& state) {
            set_object_filament(state, sa->name, sa->filament, sa->part);
            if (sa->part >= 0)
                return "set-filament: " + sa->name + " part " + std::to_string(sa->part) +
                       " -> " + std::to_string(sa->filament);
            return "set-filament: " + sa->name + " -> " + std::to_string(sa->filament);
        });
    });

    // --- object split-to-parts -------------------------------------------
    // First-match semantics: --name selects the FIRST matching ModelObject.
    // Not group-by-name — splitting across a clone-group is ambiguous (which
    // clone do we split?). Per Phase D prompt (2026-05-22) and Orca report §10.
    struct SplitArgs { std::string in, name, out; };
    auto* spt = object->add_subcommand("split-to-parts",
                                       "split a single-volume object into multiple parts by mesh components");
    auto spa = std::make_shared<SplitArgs>();
    spt->add_option("in",       spa->in,   "input .3mf")->required();
    spt->add_option("--name",   spa->name, "object name (first match)")->required();
    spt->add_option("--output", spa->out,  "output .3mf (defaults to in-place)");
    spt->callback([spa, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json)
                          ? OutputMode::Json : OutputMode::Text;
        const std::string& out = spa->out.empty() ? spa->in : spa->out;
        // std::invalid_argument is remapped to exit 7 (invalid_state) for
        // this verb — invalid mesh state, not a usage error.
        MutationExceptionMap overrides = {
            {std::type_index(typeid(std::invalid_argument)), {7, "invalid_state"}}
        };
        run_mutation(mode, spa->in, out, [&](ProjectState& state) {
            size_t parts = split_object_to_parts(state, spa->name);
            return "split-to-parts: " + spa->name + " -> " +
                   std::to_string(parts) + " parts.";
        }, overrides);
    });

    // --- object merge-parts -----------------------------------------------
    // First-match semantics: --name selects the FIRST matching ModelObject.
    // Not group-by-name — merging across a clone-group is ambiguous (which
    // clone's volumes do we merge?). Per Phase D prompt (2026-05-22) and Orca §10.
    struct MergeArgs { std::string in, name, parts_str, into, out; int filament = -1; };
    auto* mpt = object->add_subcommand("merge-parts",
                                       "merge named volumes of an object into a single new volume");
    auto mpa = std::make_shared<MergeArgs>();
    mpt->add_option("in",          mpa->in,        "input .3mf")->required();
    mpt->add_option("--name",      mpa->name,      "object name (first match)")->required();
    mpt->add_option("--parts",     mpa->parts_str, "comma-separated volume names to merge")->required();
    mpt->add_option("--into",      mpa->into,      "name for the merged result volume")->required();
    mpt->add_option("--filament",  mpa->filament,  "1-based filament slot for merged volume");
    mpt->add_option("--output",    mpa->out,       "output .3mf (defaults to in-place)");
    mpt->callback([mpa, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json)
                          ? OutputMode::Json : OutputMode::Text;
        const std::string& out = mpa->out.empty() ? mpa->in : mpa->out;

        // Parse --parts CSV (step a: empty list -> exit 1 before entering run_mutation)
        std::vector<std::string> parts;
        {
            std::istringstream ss(mpa->parts_str);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                if (!tok.empty()) parts.push_back(tok);
            }
        }
        if (parts.empty()) {
            emit_error(mode, "usage_error", "merge-parts: --parts must not be empty");
            std::exit(to_int(ExitCode::usage_error));
        }

        // Override: std::invalid_argument -> exit 7 (invalid_state) for mesh-state checks.
        MutationExceptionMap overrides = {
            {std::type_index(typeid(std::invalid_argument)), {7, "invalid_state"}}
        };
        run_mutation(mode, mpa->in, out, [&](ProjectState& state) {
            MergePartsParams p;
            p.parts    = parts;
            p.into     = mpa->into;
            p.filament = mpa->filament;
            return merge_object_parts(state, mpa->name, p);
        }, overrides);
    });
}

} // namespace bambu_cli
