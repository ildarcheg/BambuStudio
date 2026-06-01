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

// ---------- manifest directory for object.add ----------

// Per-call manifest directory, set by run_apply before dispatch and read
// by object.add. Thread-local so future parallel invocations remain safe.
static thread_local std::string g_manifest_dir;

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

    m_handlers["plate.remove"].fn = [](ProjectState& s, const json& step) {
        require_only(step, {"op", "name"});
        if (!step.contains("name") || !step["name"].is_string())
            throw ManifestFieldError("plate.remove: missing or non-string 'name'");
        remove_plate(s, step["name"].get<std::string>());
    };

    m_handlers["plate.rename"].fn = [](ProjectState& s, const json& step) {
        require_only(step, {"op", "from", "to"});
        if (!step.contains("from") || !step["from"].is_string())
            throw ManifestFieldError("plate.rename: missing or non-string 'from'");
        if (!step.contains("to") || !step["to"].is_string())
            throw ManifestFieldError("plate.rename: missing or non-string 'to'");
        rename_plate(s, step["from"].get<std::string>(), step["to"].get<std::string>());
    };

    m_handlers["plate.center"].fn = [](ProjectState& s, const json& step) {
        require_only(step, {"op", "plate"});
        if (!step.contains("plate") || !step["plate"].is_string())
            throw ManifestFieldError("plate.center: missing or non-string 'plate'");
        plate_center(s, step["plate"].get<std::string>());
    };

    m_handlers["plate.drop-to-bed"].fn = [](ProjectState& s, const json& step) {
        require_only(step, {"op", "plate"});
        if (!step.contains("plate") || !step["plate"].is_string())
            throw ManifestFieldError("plate.drop-to-bed: missing or non-string 'plate'");
        plate_drop_to_bed(s, step["plate"].get<std::string>());
    };

    m_handlers["plate.arrange"].fn = [](ProjectState& s, const json& step) {
        require_only(step, {"op", "plate"});
        if (!step.contains("plate") || !step["plate"].is_string())
            throw ManifestFieldError("plate.arrange: missing or non-string 'plate'");
        plate_arrange(s, step["plate"].get<std::string>());
    };

    m_handlers["plate.auto-orient"].fn = [](ProjectState& s, const json& step) {
        require_only(step, {"op", "plate"});
        if (!step.contains("plate") || !step["plate"].is_string())
            throw ManifestFieldError("plate.auto-orient: missing or non-string 'plate'");
        plate_auto_orient(s, step["plate"].get<std::string>());
    };
    m_handlers["plate.auto-orient"].overrides = {
        { std::type_index(typeid(std::runtime_error)), {7, "invalid_state"} },
    };

    // ---------- object ops ----------
    m_handlers["object.add"].fn = [](ProjectState& s, const json& step) {
        require_only(step, {"op", "plate", "stl", "name",
                            "filament", "count", "translate", "rotate", "scale"});
        if (!step.contains("plate") || !step["plate"].is_string())
            throw ManifestFieldError("object.add: missing or non-string 'plate'");
        if (!step.contains("stl") || !step["stl"].is_string())
            throw ManifestFieldError("object.add: missing or non-string 'stl'");

        // STL path resolution: JSON-relative.
        fs::path stl_p = step["stl"].get<std::string>();
        if (!stl_p.is_absolute() && !g_manifest_dir.empty())
            stl_p = fs::path(g_manifest_dir) / stl_p;
        stl_p = fs::weakly_canonical(stl_p);

        std::string name      = step.value("name", std::string{});
        int         filament  = step.contains("filament") ? parse_filament(step, "filament") : -1;
        int         count     = 1;
        if (step.contains("count")) {
            if (!step["count"].is_number_integer())
                throw ManifestFieldError("object.add: 'count' must be an integer");
            count = step["count"].get<int>();
            if (count < 1)
                throw ManifestFieldError("object.add: 'count' must be >= 1");
        }

        ManualTransform tf = parse_transform(step);
        const ManualTransform* tf_ptr =
            (tf.has_translate || tf.has_rotate || tf.has_scale) ? &tf : nullptr;

        add_object_to_plate(s,
                            step["plate"].get<std::string>(),
                            stl_p.string(),
                            name,
                            filament,
                            tf_ptr,
                            count,
                            nullptr);
    };

    m_handlers["object.remove"].fn = [](ProjectState& s, const json& step) {
        require_only(step, {"op", "name"});
        if (!step.contains("name") || !step["name"].is_string())
            throw ManifestFieldError("object.remove: missing or non-string 'name'");
        remove_object(s, step["name"].get<std::string>());
    };

    m_handlers["object.set-filament"].fn = [](ProjectState& s, const json& step) {
        require_only(step, {"op", "name", "filament", "part"});
        if (!step.contains("name") || !step["name"].is_string())
            throw ManifestFieldError("object.set-filament: missing or non-string 'name'");
        int filament = parse_filament(step, "filament");
        std::string part = step.value("part", std::string{});
        set_object_filament(s, step["name"].get<std::string>(), filament, part);
    };

    m_handlers["object.auto-orient"].fn = [](ProjectState& s, const json& step) {
        require_only(step, {"op", "name"});
        if (!step.contains("name") || !step["name"].is_string())
            throw ManifestFieldError("object.auto-orient: missing or non-string 'name'");
        object_auto_orient(s, step["name"].get<std::string>());
    };
    m_handlers["object.auto-orient"].overrides = {
        { std::type_index(typeid(std::runtime_error)), {7, "invalid_state"} },
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

    // Stage 3b: capture manifest directory for object.add STL path resolution.
    g_manifest_dir = fs::path(a.manifest_path).parent_path().string();

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
