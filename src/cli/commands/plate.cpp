#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_ops.hpp"
#include "../project_state.hpp"
#include "../extern/CLI11/CLI11.hpp"
#include "mutation_runner.hpp"

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
        const std::string& out = a->out_path.empty() ? a->in_path : a->out_path;
        run_mutation(mode, a->in_path, out, [&](ProjectState& state) {
            add_plate(state, a->name);
            return "plate added: " + a->name + " -> " + out;
        });
    });

    // --- plate remove -------------------------------------------------
    auto* rem = plate->add_subcommand("remove", "remove an empty plate");
    auto rm = std::make_shared<PlateRemoveArgs>();
    rem->add_option("in", rm->in_path, "input .3mf")->required();
    rem->add_option("--name", rm->name, "plate to remove")->required();
    rem->add_option("--output", rm->out_path, "output .3mf (defaults to in-place)");
    rem->callback([rm, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;
        const std::string& out = rm->out_path.empty() ? rm->in_path : rm->out_path;
        run_mutation(mode, rm->in_path, out, [&](ProjectState& state) {
            remove_plate(state, rm->name);
            return "plate removed: " + rm->name + " -> " + out;
        });
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
        const std::string& out = rn->out_path.empty() ? rn->in_path : rn->out_path;
        run_mutation(mode, rn->in_path, out, [&](ProjectState& state) {
            rename_plate(state, rn->from, rn->to);
            return "plate renamed: " + rn->from + " -> " + rn->to + " in " + out;
        });
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
        emit_list_response<std::string>(
            mode, "plate list", "plate_count", "plates", names,
            [](const std::string& n) -> nlohmann::json { return n; },
            [](std::size_t i, const std::string& n) {
                std::ostringstream s;
                s << (i + 1) << "  " << n << "\n";
                return s.str();
            });
    });
}

} // namespace bambu_cli
