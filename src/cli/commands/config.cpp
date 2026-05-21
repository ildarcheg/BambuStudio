#include "../exit_codes.hpp"
#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_ops.hpp"
#include "../project_state.hpp"
#include "../extern/CLI11/CLI11.hpp"

#include <memory>
#include <sstream>

namespace bambu_cli {

struct ConfigSetArgs {
    std::string in_path;
    std::string object_name;   // empty = project-level
    std::string key;
    std::string value;
    std::string out_path;      // empty = in-place
};

struct ConfigUnsetArgs {
    std::string in_path;
    std::string object_name;   // empty = project-level
    std::string key;
    std::string out_path;      // empty = in-place
};

struct ConfigListArgs {
    std::string in_path;
    std::string object_name;   // empty = project-level
    bool        changed_only = false;
};

void register_config_subcommands(CLI::App& app, OutputMode* mode_out) {
    auto* cfg = app.add_subcommand("config", "config set/unset/list operations");

    // --- config set -------------------------------------------------------
    auto* set_cmd = cfg->add_subcommand("set", "set a config key on the project or an object");
    auto sa = std::make_shared<ConfigSetArgs>();
    set_cmd->add_option("in",       sa->in_path,     "input .3mf")->required();
    set_cmd->add_option("--object", sa->object_name, "object name (omit for project-level)");
    set_cmd->add_option("--key",    sa->key,         "config key")->required();
    set_cmd->add_option("--value",  sa->value,       "config value")->required();
    set_cmd->add_option("--output", sa->out_path,    "output .3mf (defaults to in-place)");
    set_cmd->callback([sa, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json)
                          ? OutputMode::Json : OutputMode::Text;
        ProjectState state;
        IoResult lr = load_project(sa->in_path, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }

        OpResult op = config_set(state, sa->object_name, sa->key, sa->value);
        if (!op.ok) { emit_error(mode, op.error_code, op.error_message); std::exit(op.exit_code); }

        const std::string& out = sa->out_path.empty() ? sa->in_path : sa->out_path;
        IoResult sr = save_project(state, out);
        if (!sr.ok) { emit_error(mode, sr.error_code, sr.error_message); std::exit(sr.exit_code); }

        std::string target = sa->object_name.empty() ? "project" : ("object '" + sa->object_name + "'");
        emit_ok(mode, "ok", "config set: " + target + " " + sa->key + "=" + sa->value + " -> " + out);
    });

    // --- config unset -----------------------------------------------------
    auto* unset_cmd = cfg->add_subcommand("unset", "remove a config key from the project or an object");
    auto ua = std::make_shared<ConfigUnsetArgs>();
    unset_cmd->add_option("in",       ua->in_path,     "input .3mf")->required();
    unset_cmd->add_option("--object", ua->object_name, "object name (omit for project-level)");
    unset_cmd->add_option("--key",    ua->key,         "config key to remove")->required();
    unset_cmd->add_option("--output", ua->out_path,    "output .3mf (defaults to in-place)");
    unset_cmd->callback([ua, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json)
                          ? OutputMode::Json : OutputMode::Text;
        ProjectState state;
        IoResult lr = load_project(ua->in_path, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }

        OpResult op = config_unset(state, ua->object_name, ua->key);
        if (!op.ok) { emit_error(mode, op.error_code, op.error_message); std::exit(op.exit_code); }

        const std::string& out = ua->out_path.empty() ? ua->in_path : ua->out_path;
        IoResult sr = save_project(state, out);
        if (!sr.ok) { emit_error(mode, sr.error_code, sr.error_message); std::exit(sr.exit_code); }

        std::string target = ua->object_name.empty() ? "project" : ("object '" + ua->object_name + "'");
        emit_ok(mode, "ok", "config unset: " + target + " " + ua->key + " -> " + out);
    });

    // --- config list ------------------------------------------------------
    // Read-only — no --output flag.
    auto* list_cmd = cfg->add_subcommand("list", "list config entries for the project or an object");
    auto la = std::make_shared<ConfigListArgs>();
    list_cmd->add_option("in",            la->in_path,     "input .3mf")->required();
    list_cmd->add_option("--object",      la->object_name, "object name (omit for project-level)");
    list_cmd->add_flag("--changed-only",  la->changed_only, "only show keys differing from defaults");
    list_cmd->callback([la, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json)
                          ? OutputMode::Json : OutputMode::Text;
        ProjectState state;
        IoResult lr = load_project(la->in_path, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }

        // Validate object reference before listing.
        if (!la->object_name.empty()) {
            if (find_object_by_name(state, la->object_name) < 0) {
                emit_error(mode, "unknown_reference",
                           "object '" + la->object_name + "' not found");
                std::exit(to_int(ExitCode::unknown_reference));
            }
        }

        std::vector<ConfigEntry> entries = config_list(state, la->object_name, la->changed_only);

        if (mode == OutputMode::Json) {
            nlohmann::json data;
            data["count"]   = entries.size();
            data["entries"] = nlohmann::json::array();
            for (const auto& e : entries) {
                data["entries"].push_back({
                    {"key",   e.key},
                    {"value", e.value},
                });
            }
            emit_ok(mode, "ok", "config list", data);
        } else {
            std::ostringstream m;
            for (const auto& e : entries)
                m << e.key << "=" << e.value << "\n";
            emit_ok(mode, "ok", m.str());
        }
    });
}

} // namespace bambu_cli
