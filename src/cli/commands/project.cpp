#include "project_tab.hpp"

#include "../exit_codes.hpp"
#include "../io.hpp"
#include "../invariant_guard.hpp"
#include "../json_output.hpp"
#include "../project_state.hpp"

#include "../extern/CLI11/CLI11.hpp"

#include <boost/filesystem.hpp>
#include <iostream>
#include <string>

namespace bambu_cli {

// Forward declaration — defined in commands/project_apply.cpp.
void register_project_apply_subcommand(CLI::App* project_cmd, OutputMode* mode_out);

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

    // Register info / profile / aux leaf verbs.
    register_project_tab_subcommands(project, mode_out);

    // Register the batch-manifest apply verb.
    register_project_apply_subcommand(project, mode_out);

    init->callback([args, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;
        namespace fs = boost::filesystem;

        // Verify the template exists before touching anything else, so
        // file_not_found surfaces as exit 2 (atomic_copy used to do this).
        if (!fs::exists(args->template_path)) {
            emit_error(mode, "file_not_found", "template not found: " + args->template_path);
            std::exit(to_int(ExitCode::file_not_found));
        }

        // 1. Stage the template at <out>.init-tmp. The destination is NEVER
        //    written until save_project's atomic swap succeeds, so a failed
        //    init can never leave a half-baked or unvalidated file at the
        //    user-visible out path. This is the TOCTOU defense ported from
        //    OrcaSlicer commands/project_init.cpp:31-74: validate the bytes
        //    we actually loaded (staging copy) instead of the user-supplied
        //    path, which could be swapped between copy and validate.
        const std::string staging = args->out_path + ".init-tmp";
        {
            boost::system::error_code ec;
            fs::remove(staging, ec);  // best-effort: clean any prior leftover
        }
        try {
            fs::copy_file(args->template_path, staging,
                          fs::copy_options::overwrite_existing);
        } catch (const std::exception& e) {
            emit_error(mode, "invalid_state",
                       std::string("failed to stage template: ") + e.what());
            std::exit(to_int(ExitCode::invalid_state));
        }

        // 2. Load the staging copy.
        ProjectState state;
        IoResult lr = load_project(staging, state);
        if (!lr.ok) {
            boost::system::error_code ec;
            fs::remove(staging, ec);
            emit_error(mode, lr.error_code, lr.error_message);
            std::exit(lr.exit_code);
        }

        // 3. Validate the staging copy's thumbnail invariants BEFORE saving.
        //    save_project regenerates placeholder thumbnails, so a corrupted
        //    template (missing plate_N.png / plate_N_small.png) would otherwise
        //    be silently "fixed" by the save. Running check (b) here against
        //    the staging copy catches input-level corruption explicitly.
        //    This is scoped to `project init` only — other mutation commands
        //    (plate add, object add, config set, ...) don't pre-validate inputs.
        //    check_thumbnails_in_archive keeps its mz_zip_reader_init_file
        //    short-path tolerance for Windows 8.3 paths under TEMP.
        {
            GuardResult pre = check_thumbnails_in_archive(staging, state);
            if (!pre.ok) {
                boost::system::error_code ec;
                fs::remove(staging, ec);
                emit_error(mode, "invariant_violation",
                           "guard check '" + pre.failed_check +
                           "' failed (template): " + pre.failure_detail);
                std::exit(to_int(ExitCode::invariant_violation));
            }
        }

        // 4. Save (no mutation) to the destination. save_project runs the
        //    post-save guard before its atomic .bak swap, so the destination
        //    only ever sees fully-validated bytes.
        IoResult sr = save_project(state, args->out_path);
        {
            boost::system::error_code ec;
            fs::remove(staging, ec);  // staging cleanup runs regardless of save outcome
        }
        if (!sr.ok) {
            // save_project handles its own scratch cleanup on failure; the
            // destination is either preserved (re-init) or never created
            // (first init) by the .bak swap. Nothing to remove here.
            emit_error(mode, sr.error_code, sr.error_message);
            std::exit(sr.exit_code);
        }

        emit_ok(mode, "ok", "project init: clone-and-verify succeeded -> " + args->out_path);
    });
}

} // namespace bambu_cli
