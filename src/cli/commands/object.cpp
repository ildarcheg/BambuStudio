#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_ops.hpp"
#include "../project_state.hpp"
#include "../extern/CLI11/CLI11.hpp"

#include <memory>
#include <sstream>

namespace bambu_cli {

struct ObjectAddArgs {
    std::string in_path;
    std::string plate;
    std::string stl;
    std::string name;
    std::string out_path;
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
    add->callback([a, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;
        ProjectState state;
        IoResult lr = load_project(a->in_path, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }

        ObjectRef ref;
        OpResult op = add_object_to_plate(state, a->plate, a->stl, a->name, &ref);
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
            std::ostringstream d;
            d << "{\"object_count\":" << objs.size() << ",\"objects\":[";
            for (size_t i = 0; i < objs.size(); ++i) {
                if (i) d << ",";
                d << "{\"plate\":\""    << json_escape(objs[i].plate_name)
                  << "\",\"name\":\""   << json_escape(objs[i].object_name)
                  << "\",\"extruder\":" << objs[i].extruder << "}";
            }
            d << "]}";
            emit_ok(mode, "ok", "object list", d.str());
        } else {
            std::ostringstream m;
            for (const auto& o : objs)
                m << "[" << o.plate_name << "] " << o.object_name
                  << " (extruder=" << o.extruder << ")\n";
            emit_ok(mode, "ok", m.str());
        }
    });
}

} // namespace bambu_cli
