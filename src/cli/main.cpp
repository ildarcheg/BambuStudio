// bambu-cli — composes .3mf project files for Bambu Studio.
// v1 entry point. Subcommands wired in subsequent milestones.

#include "extern/CLI11/CLI11.hpp"

#include <iostream>

int main(int argc, char** argv) {
    CLI::App app{"bambu-cli — compose .3mf project files for Bambu Studio"};
    app.set_version_flag("--version", "bambu-cli 0.1.0");

    // Global flags (not yet plumbed; honored from M1 onward).
    bool json_output = false;
    bool verbose = false;
    app.add_flag("--json", json_output, "emit machine-readable JSON Shape A");
    app.add_flag("--verbose", verbose, "verbose diagnostic logging");

    // Subcommands land in subsequent milestones.

    CLI11_PARSE(app, argc, argv);
    if (app.get_subcommands().empty()) {
        std::cout << app.help() << std::endl;
        return 0;
    }
    return 0;
}
