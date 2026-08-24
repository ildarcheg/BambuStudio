#pragma once

#include "../extern/CLI11/CLI11.hpp"
#include "../json_output.hpp"

namespace bambu_cli {

// Register project info / profile / aux leaf verbs onto the given `project`
// subcommand. Called from register_project_subcommands.
void register_project_tab_subcommands(CLI::App* project, OutputMode* mode_out);

} // namespace bambu_cli
