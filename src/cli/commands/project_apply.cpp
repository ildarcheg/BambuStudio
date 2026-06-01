// bambu-cli `project apply` — batch manifest verb.
// See docs/superpowers/specs/2026-05-31-project-apply-batch-design.md.

#include "../apply_helpers.hpp"
#include "../exception_dispatch.hpp"
#include "../exceptions.hpp"
#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_ops.hpp"
#include "../project_state.hpp"
#include "../extern/CLI11/CLI11.hpp"
#include "project_apply_internal.hpp"

#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace bambu_cli {

namespace fs = boost::filesystem;
using nlohmann::json;

// ---------- manifest header validation ----------

void parse_and_validate_manifest(const json& m)
{
    if (!m.is_object())
        throw ManifestFieldError("manifest: top-level value must be an object");

    if (!m.contains("version"))
        throw ManifestFieldError("manifest: missing required key 'version'");
    if (!m["version"].is_number_integer())
        throw ManifestFieldError("manifest: 'version' must be an integer");
    int version = m["version"].get<int>();
    if (version != 1)
        throw ManifestFieldError(
            "manifest: unsupported manifest version: " + std::to_string(version));

    if (!m.contains("operations"))
        throw ManifestFieldError("manifest: missing required key 'operations'");
    if (!m["operations"].is_array())
        throw ManifestFieldError("manifest: 'operations' must be an array");

    for (auto it = m.begin(); it != m.end(); ++it) {
        const std::string& key = it.key();
        if (key != "version" && key != "operations")
            throw ManifestFieldError("manifest: unknown top-level key '" + key + "'");
    }

    if (m["operations"].size() > MAX_MANIFEST_OPS) {
        std::ostringstream os;
        os << "manifest exceeds maximum of " << MAX_MANIFEST_OPS
           << " operations (got " << m["operations"].size() << ")";
        throw ManifestFieldError(os.str());
    }

    // Per-step minimum shape: must be object with a non-empty string "op".
    std::size_t i = 0;
    for (const auto& step : m["operations"]) {
        ++i;
        if (!step.is_object())
            throw ManifestFieldError(
                "step " + std::to_string(i) + ": must be a JSON object");
        if (!step.contains("op"))
            throw ManifestFieldError(
                "step " + std::to_string(i) + ": missing required field 'op'");
        if (!step["op"].is_string())
            throw ManifestFieldError(
                "step " + std::to_string(i) + ": 'op' must be a string");
        if (step["op"].get<std::string>().empty())
            throw ManifestFieldError(
                "step " + std::to_string(i) + ": 'op' must be non-empty");
    }
}

// ---------- handler registry ----------

HandlerRegistry::HandlerRegistry()
{
    // ---------- plate ops ----------
    m_handlers["plate.add"].fn = [](ProjectState& s, const json& step) {
        require_only(step, {"op", "name"});
        if (!step.contains("name"))
            throw ManifestFieldError("plate.add: missing required field 'name'");
        if (!step["name"].is_string())
            throw ManifestFieldError("plate.add: 'name' must be a string");
        add_plate(s, step["name"].get<std::string>());
    };
}

const HandlerEntry& HandlerRegistry::lookup(const std::string& op) const
{
    auto it = m_handlers.find(op);
    if (it == m_handlers.end())
        throw ManifestFieldError("unknown op: '" + op + "'");
    return it->second;
}

// ---------- file load ----------

namespace {

json load_manifest_file(const std::string& path)
{
    if (!fs::exists(path)) {
        throw FileNotFoundError("manifest file not found: " + path);
    }
    std::ifstream in(path);
    if (!in) {
        throw FileNotFoundError("cannot open manifest file: " + path);
    }
    json m;
    try {
        in >> m;
    } catch (const json::parse_error& e) {
        // Re-throw as a std::runtime_error so the dispatch table maps it
        // to exit 3 (parse_failure) with nlohmann's line/column message.
        throw std::runtime_error(
            std::string("manifest JSON parse error: ") + e.what());
    }
    return m;
}

} // namespace

// ---------- CLI registration ----------

struct ApplyArgs {
    std::string in_path;
    std::string manifest_path;
    std::string out_path;     // empty = in-place
    bool        dry_run = false;
};

static void run_apply(OutputMode mode, const ApplyArgs& a);

void register_project_apply_subcommand(CLI::App* project_cmd, OutputMode* mode_out)
{
    auto* apply = project_cmd->add_subcommand(
        "apply",
        "apply a JSON manifest of mutations against the input project");
    auto a = std::make_shared<ApplyArgs>();
    apply->add_option("in",         a->in_path,       "input .3mf")->required();
    apply->add_option("--manifest", a->manifest_path, "path to manifest JSON")->required();
    apply->add_option("--output",   a->out_path,      "output .3mf (defaults to in-place)");
    apply->add_flag(  "--dry-run",  a->dry_run,
                      "validate + apply in-memory; skip save_project");
    apply->callback([a, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json)
                          ? OutputMode::Json : OutputMode::Text;
        run_apply(mode, *a);
    });
}

// ---------- main flow ----------

static void run_apply(OutputMode mode, const ApplyArgs& a)
{
    // Stage 1-3: load + parse + header-validate the manifest. No .3mf touched yet.
    json manifest;
    try {
        manifest = load_manifest_file(a.manifest_path);
        parse_and_validate_manifest(manifest);
    } catch (const std::exception& e) {
        auto d = exception_dispatch::dispatch(e);
        emit_error(mode, d.code, d.message);
        std::exit(d.exit_code);
    }

    // Stage 4: load the .3mf.
    ProjectState state;
    IoResult lr = load_project(a.in_path, state);
    if (!lr.ok) {
        emit_error(mode, lr.error_code, lr.error_message);
        std::exit(lr.exit_code);
    }

    // Stage 5: dispatch each op.
    static const HandlerRegistry registry;
    std::size_t step_index = 0;
    for (const auto& step : manifest["operations"]) {
        ++step_index;
        const std::string op = step["op"].get<std::string>();
        const HandlerEntry* entry = nullptr;
        try {
            entry = &registry.lookup(op);   // may throw ManifestFieldError
            entry->fn(state, step);
        } catch (const std::exception& e) {
            auto d = exception_dispatch::dispatch(
                e, entry ? entry->overrides : MutationExceptionMap{});
            json data;
            data["step"] = step_index;
            data["op"]   = op;
            std::ostringstream msg;
            msg << "step " << step_index << " (op '" << op << "'): " << d.message;
            emit_error(mode, d.code, msg.str(), data);
            std::exit(d.exit_code);
        }
    }

    // Stage 6: save (skip on --dry-run).
    const std::string& out = a.out_path.empty() ? a.in_path : a.out_path;
    if (!a.dry_run) {
        IoResult sr = save_project(state, out);
        if (!sr.ok) {
            emit_error(mode, sr.error_code, sr.error_message);
            std::exit(sr.exit_code);
        }
    }

    json data;
    data["steps_applied"] = manifest["operations"].size();
    data["dry_run"] = a.dry_run;
    if (!a.dry_run) data["output"] = out;

    std::string msg = (a.dry_run ? "dry-run: " : "applied ")
                    + std::to_string(manifest["operations"].size()) + " ops"
                    + (a.dry_run ? std::string{} : (" -> " + out));
    emit_ok(mode, "ok", msg, data);
}

} // namespace bambu_cli
