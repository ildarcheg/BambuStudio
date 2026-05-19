// bambu-cli — composes .3mf project files for Bambu Studio.
// v1 entry point. Subcommands registered in commands/*.cpp.

#include "extern/CLI11/CLI11.hpp"
#include "json_output.hpp"

#include <iostream>

namespace bambu_cli {
void register_project_subcommands(CLI::App& app, OutputMode* mode_out);
void register_inspect_subcommands(CLI::App& app, OutputMode* mode_out);
void register_plate_subcommands(CLI::App& app, OutputMode* mode_out);
}

int main(int argc, char** argv) {
    CLI::App app{"bambu-cli — compose .3mf project files for Bambu Studio"};
    app.set_version_flag("--version", "bambu-cli 0.1.0");

    bambu_cli::OutputMode mode = bambu_cli::OutputMode::Text;

    // Use add_flag_callback so mode is set during parse, BEFORE subcommand callbacks fire.
    app.add_flag_callback("--json", [&]() { mode = bambu_cli::OutputMode::Json; },
                          "emit machine-readable JSON Shape A");

    bool verbose = false;
    app.add_flag("--verbose", verbose, "verbose diagnostic logging");

    bambu_cli::register_project_subcommands(app, &mode);
    bambu_cli::register_inspect_subcommands(app, &mode);
    bambu_cli::register_plate_subcommands(app, &mode);

    CLI11_PARSE(app, argc, argv);

    if (app.get_subcommands().empty()) {
        std::cout << app.help() << std::endl;
        return 0;
    }
    return 0;
}
