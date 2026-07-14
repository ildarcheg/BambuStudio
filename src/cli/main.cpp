// bambu-cli — composes .3mf project files for Bambu Studio.
// v1 entry point. Subcommands registered in commands/*.cpp.

#include "extern/CLI11/CLI11.hpp"
#include "json_output.hpp"

#include <iostream>
#include <cstdlib>
#include <boost/filesystem.hpp>
#include <libslic3r/Utils.hpp>

namespace bambu_cli {
void register_project_subcommands(CLI::App& app, OutputMode* mode_out);
void register_inspect_subcommands(CLI::App& app, OutputMode* mode_out);
void register_plate_subcommands(CLI::App& app, OutputMode* mode_out);
void register_object_subcommands(CLI::App& app, OutputMode* mode_out);
void register_config_subcommands(CLI::App& app, OutputMode* mode_out);
}

int main(int argc, char** argv) {
    // Initialise temp dir so Model::get_backup_path() doesn't write to "/"
    std::string tmp;
    if (const char* env = std::getenv("TMPDIR"))
        tmp = env;
    else
        tmp = boost::filesystem::temp_directory_path().string();
    Slic3r::set_temporary_dir(tmp);

    CLI::App app{"bambu-cli -- compose .3mf project files for Bambu Studio"};
    app.set_version_flag("--version", "bambu-cli 0.1.0");

    bambu_cli::OutputMode mode = bambu_cli::OutputMode::Text;

    // Use add_flag_callback so mode is set during parse, BEFORE subcommand callbacks fire.
    app.add_flag_callback("--json", [&]() { mode = bambu_cli::OutputMode::Json; },
                          "emit machine-readable JSON Shape A");

    // --verbose is accepted for forward-compat but is intentionally a no-op:
    // wiring it to stage callbacks requires invasive plumbing (>30 LOC across
    // 5 register functions). Hidden from --help; kept parsed so scripts don't break.
    bool verbose = false;
    app.add_flag("--verbose", verbose, "")->group("");

    bambu_cli::register_project_subcommands(app, &mode);
    bambu_cli::register_inspect_subcommands(app, &mode);
    bambu_cli::register_plate_subcommands(app, &mode);
    bambu_cli::register_object_subcommands(app, &mode);
    bambu_cli::register_config_subcommands(app, &mode);

    CLI11_PARSE(app, argc, argv);

    if (app.get_subcommands().empty()) {
        std::cout << app.help() << std::endl;
        return 0;
    }
    return 0;
}
