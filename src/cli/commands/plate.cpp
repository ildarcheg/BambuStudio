#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_ops.hpp"
#include "../project_state.hpp"
#include "../extern/CLI11/CLI11.hpp"

#include <memory>
#include <sstream>

namespace bambu_cli {

struct PlateAddArgs {
    std::string in_path;
    std::string name;
    std::string out_path;   // empty -> in-place
};

struct PlateListArgs {
    std::string in_path;
};

void register_plate_subcommands(CLI::App& app, OutputMode* mode_out) {
    auto* plate = app.add_subcommand("plate", "plate-level operations");

    // --- plate add ----------------------------------------------------
    auto* add = plate->add_subcommand("add", "append a new empty plate");
    auto a = std::make_shared<PlateAddArgs>();
    add->add_option("in", a->in_path, "input .3mf")->required();
    add->add_option("--name", a->name, "new plate name")->required();
    add->add_option("--output", a->out_path, "output .3mf (defaults to in-place)");
    add->callback([a, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;
        ProjectState state;
        IoResult lr = load_project(a->in_path, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }

        OpResult op = add_plate(state, a->name);
        if (!op.ok) { emit_error(mode, op.error_code, op.error_message); std::exit(op.exit_code); }

        const std::string& out = a->out_path.empty() ? a->in_path : a->out_path;
        IoResult sr = save_project(state, out);
        if (!sr.ok) { emit_error(mode, sr.error_code, sr.error_message); std::exit(sr.exit_code); }

        emit_ok(mode, "ok", "plate added: " + a->name + " -> " + out);
    });

    // --- plate list ---------------------------------------------------
    auto* lst = plate->add_subcommand("list", "list plate names");
    auto l = std::make_shared<PlateListArgs>();
    lst->add_option("in", l->in_path, "input .3mf")->required();
    lst->callback([l, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;
        ProjectState state;
        IoResult lr = load_project(l->in_path, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }

        std::vector<std::string> names = list_plate_names(state);
        if (mode == OutputMode::Json) {
            std::ostringstream d;
            d << "{\"plate_count\":" << names.size() << ",\"plates\":[";
            for (size_t i = 0; i < names.size(); ++i) {
                if (i) d << ",";
                d << "\"" << json_escape(names[i]) << "\"";
            }
            d << "]}";
            emit_ok(mode, "ok", "plate list", d.str());
        } else {
            std::ostringstream m;
            for (size_t i = 0; i < names.size(); ++i)
                m << (i+1) << "  " << names[i] << "\n";
            emit_ok(mode, "ok", m.str());
        }
    });
}

} // namespace bambu_cli
