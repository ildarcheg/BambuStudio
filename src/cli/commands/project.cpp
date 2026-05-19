#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_state.hpp"

#include "../extern/CLI11/CLI11.hpp"

#include <boost/filesystem.hpp>
#include <iostream>
#include <string>

namespace bambu_cli {

struct ProjectInitArgs {
    std::string out_path;
    std::string template_path;
};

// Register the `project init` subcommand on the given CLI11 app.
// Caller passes a pointer to the global OutputMode that will be set by the
// top-level --json flag callback.
void register_project_subcommands(CLI::App& app, OutputMode* mode_out) {
    auto* project = app.add_subcommand("project", "project-level operations");

    auto* init = project->add_subcommand("init", "clone a template .3mf via load/save/guard");
    auto args = std::make_shared<ProjectInitArgs>();
    init->add_option("out", args->out_path, "output .3mf path")->required();
    init->add_option("--template", args->template_path, "reference .3mf path")->required();

    init->callback([args, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;

        // 1. Atomic-copy template -> out.
        IoResult cp = atomic_copy(args->template_path, args->out_path);
        if (!cp.ok) {
            emit_error(mode, cp.error_code, cp.error_message);
            std::exit(cp.exit_code);
        }

        // 2. Load the clone.
        ProjectState state;
        IoResult lr = load_project(args->out_path, state);
        if (!lr.ok) {
            // Remove the partial clone so the output path doesn't linger.
            boost::filesystem::remove(args->out_path);
            emit_error(mode, lr.error_code, lr.error_message);
            std::exit(lr.exit_code);
        }

        // 3. Save (no mutation). save_project runs the guard before rename.
        IoResult sr = save_project(state, args->out_path);
        if (!sr.ok) {
            // On any save failure (pre-save guard or post-save guard), remove
            // the clone so the out path is clean (no partial/corrupted output).
            boost::filesystem::remove(args->out_path);
            emit_error(mode, sr.error_code, sr.error_message);
            std::exit(sr.exit_code);
        }

        emit_ok(mode, "ok", "project init: clone-and-verify succeeded -> " + args->out_path);
    });
}

} // namespace bambu_cli
