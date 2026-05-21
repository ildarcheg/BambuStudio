#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_state.hpp"
#include "../extern/CLI11/CLI11.hpp"

#include "libslic3r/PrintConfig.hpp"

#include <memory>
#include <sstream>
#include <string>

namespace bambu_cli {

static size_t total_object_count(const Slic3r::Model& m) {
    return m.objects.size();
}

static size_t filament_slot_count(const Slic3r::DynamicPrintConfig& cfg) {
    const Slic3r::ConfigOption* opt = cfg.option("filament_settings_id");
    if (!opt) return 0;
    auto* vs = dynamic_cast<const Slic3r::ConfigOptionStrings*>(opt);
    return vs ? vs->values.size() : 0;
}

void register_inspect_subcommands(CLI::App& app, OutputMode* mode_out) {
    auto* inspect = app.add_subcommand("inspect", "print plate/object/filament counts");
    auto in_path = std::make_shared<std::string>();
    inspect->add_option("in", *in_path, "input .3mf path")->required();

    inspect->callback([in_path, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;

        ProjectState state;
        IoResult lr = load_project(*in_path, state);
        if (!lr.ok) {
            emit_error(mode, lr.error_code, lr.error_message);
            std::exit(lr.exit_code);
        }

        size_t plates    = state.plate_data.size();
        size_t objects   = total_object_count(state.model);
        size_t filaments = filament_slot_count(state.project_config);

        if (mode == OutputMode::Json) {
            nlohmann::json data = {
                {"plate_count",    plates},
                {"object_count",   objects},
                {"filament_count", filaments},
            };
            emit_ok(mode, "ok", "inspect ok", data);
        } else {
            std::ostringstream msg;
            msg << "plates: " << plates << "  objects: " << objects
                << "  filaments: " << filaments;
            emit_ok(mode, "ok", msg.str());
        }
    });
}

} // namespace bambu_cli
