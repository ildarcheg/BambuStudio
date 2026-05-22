#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_ops.hpp"
#include "../project_state.hpp"
#include "../extern/CLI11/CLI11.hpp"
#include "op_dispatch.hpp"

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

struct PlateRemoveArgs {
    std::string in_path;
    std::string name;
    std::string out_path;   // empty -> in-place
};

struct PlateRenameArgs {
    std::string in_path;
    std::string from;
    std::string to;
    std::string out_path;   // empty -> in-place
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

        run_op_or_exit(mode, [&]() { return add_plate(state, a->name); });

        const std::string& out = a->out_path.empty() ? a->in_path : a->out_path;
        IoResult sr = save_project(state, out);
        if (!sr.ok) { emit_error(mode, sr.error_code, sr.error_message); std::exit(sr.exit_code); }

        emit_ok(mode, "ok", "plate added: " + a->name + " -> " + out);
    });

    // --- plate remove -------------------------------------------------
    auto* rem = plate->add_subcommand("remove", "remove an empty plate");
    auto rm = std::make_shared<PlateRemoveArgs>();
    rem->add_option("in", rm->in_path, "input .3mf")->required();
    rem->add_option("--name", rm->name, "plate to remove")->required();
    rem->add_option("--output", rm->out_path, "output .3mf (defaults to in-place)");
    rem->callback([rm, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;
        ProjectState state;
        IoResult lr = load_project(rm->in_path, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }

        run_op_or_exit(mode, [&]() { return remove_plate(state, rm->name); });

        const std::string& out = rm->out_path.empty() ? rm->in_path : rm->out_path;
        IoResult sr = save_project(state, out);
        if (!sr.ok) { emit_error(mode, sr.error_code, sr.error_message); std::exit(sr.exit_code); }

        emit_ok(mode, "ok", "plate removed: " + rm->name + " -> " + out);
    });

    // --- plate rename -------------------------------------------------
    auto* ren = plate->add_subcommand("rename", "rename an existing plate");
    auto rn = std::make_shared<PlateRenameArgs>();
    ren->add_option("in", rn->in_path, "input .3mf")->required();
    ren->add_option("--from", rn->from, "current plate name")->required();
    ren->add_option("--to",   rn->to,   "new plate name")->required();
    ren->add_option("--output", rn->out_path, "output .3mf (defaults to in-place)");
    ren->callback([rn, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;
        ProjectState state;
        IoResult lr = load_project(rn->in_path, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }

        run_op_or_exit(mode, [&]() { return rename_plate(state, rn->from, rn->to); });

        const std::string& out = rn->out_path.empty() ? rn->in_path : rn->out_path;
        IoResult sr = save_project(state, out);
        if (!sr.ok) { emit_error(mode, sr.error_code, sr.error_message); std::exit(sr.exit_code); }

        emit_ok(mode, "ok", "plate renamed: " + rn->from + " -> " + rn->to + " in " + out);
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
            nlohmann::json data;
            data["plate_count"] = names.size();
            data["plates"]      = nlohmann::json::array();
            for (const auto& n : names) data["plates"].push_back(n);
            emit_ok(mode, "ok", "plate list", data);
        } else {
            std::ostringstream m;
            for (size_t i = 0; i < names.size(); ++i)
                m << (i+1) << "  " << names[i] << "\n";
            emit_ok(mode, "ok", m.str());
        }
    });
}

} // namespace bambu_cli
