#include "project_tab.hpp"

#include "../exceptions.hpp"
#include "../exit_codes.hpp"
#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_state.hpp"
#include "../project_tab_ops.hpp"
#include "mutation_runner.hpp"

#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>

#include <sstream>
#include <string>
#include <vector>

namespace bambu_cli {

namespace {

// ---- helpers ---------------------------------------------------------------

static OutputMode resolve_mode(OutputMode* mode_out) {
    return (mode_out && *mode_out == OutputMode::Json) ? OutputMode::Json : OutputMode::Text;
}

// Emit a 6-field info show response.
static void emit_info_show(OutputMode mode, const InfoView& v) {
    auto sentinel = [](const std::string& s) -> std::string {
        return s.empty() ? "(empty)" : s;
    };
    if (mode == OutputMode::Json) {
        nlohmann::json data;
        data["title"]       = v.title;
        data["description"] = v.description;
        data["license"]     = v.license;
        data["copyright"]   = v.copyright;
        data["cover"]       = v.cover;
        data["origin"]      = v.origin;
        emit_ok(mode, "ok", "project info", data);
    } else {
        std::ostringstream m;
        m << "title: "       << sentinel(v.title)       << "\n"
          << "description: " << sentinel(v.description) << "\n"
          << "license: "     << sentinel(v.license)     << "\n"
          << "copyright: "   << sentinel(v.copyright)   << "\n"
          << "cover: "       << sentinel(v.cover)       << "\n"
          << "origin: "      << sentinel(v.origin)      << "\n";
        emit_ok(mode, "ok", m.str());
    }
}

// Emit a 5-field profile show response.
static void emit_profile_show(OutputMode mode, const ProfileView& v) {
    auto sentinel = [](const std::string& s) -> std::string {
        return s.empty() ? "(empty)" : s;
    };
    if (mode == OutputMode::Json) {
        nlohmann::json data;
        data["title"]       = v.title;
        data["description"] = v.description;
        data["cover"]       = v.cover;
        data["user_id"]     = v.user_id;
        data["user_name"]   = v.user_name;
        emit_ok(mode, "ok", "project profile", data);
    } else {
        std::ostringstream m;
        m << "title: "       << sentinel(v.title)       << "\n"
          << "description: " << sentinel(v.description) << "\n"
          << "cover: "       << sentinel(v.cover)       << "\n"
          << "user_id: "     << sentinel(v.user_id)     << "\n"
          << "user_name: "   << sentinel(v.user_name)   << "\n";
        emit_ok(mode, "ok", m.str());
    }
}

// Parse "field1,field2,..." into a vector.
static std::vector<std::string> split_fields(const std::string& csv) {
    std::vector<std::string> out;
    std::istringstream ss(csv);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (!tok.empty()) out.push_back(tok);
    }
    return out;
}

// ---- project info ----------------------------------------------------------

struct InfoShowArgs  { std::string file; };
struct InfoSetArgs   {
    std::string file, output;
    std::string title, description, license, copyright, cover, cover_name;
    bool has_title{}, has_desc{}, has_license{}, has_copyright{},
         has_cover{}, has_cover_name{};
};
struct InfoClearArgs { std::string file, output, field; };

static void register_info(CLI::App* project, OutputMode* mode_out) {
    auto* info = project->add_subcommand("info", "project info fields (show/set/clear)");

    // --- info show ---
    auto* show = info->add_subcommand("show", "show project info fields (read-only)");
    auto show_a = std::make_shared<InfoShowArgs>();
    show->add_option("file", show_a->file, "input .3mf")->required();
    show->callback([show_a, mode_out]() {
        OutputMode mode = resolve_mode(mode_out);
        ProjectState state;
        IoResult lr = load_project(show_a->file, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }
        emit_info_show(mode, info_show(state));
    });

    // --- info set ---
    auto* set = info->add_subcommand("set", "set project info fields");
    auto set_a = std::make_shared<InfoSetArgs>();
    set->add_option("file",          set_a->file,        "input .3mf")->required();
    set->add_option("--output,-o",   set_a->output,      "output .3mf (default: in-place)");
    set->add_option("--title",       set_a->title,       "project title")
       ->each([set_a](const std::string&){ set_a->has_title = true; });
    set->add_option("--description", set_a->description, "project description")
       ->each([set_a](const std::string&){ set_a->has_desc = true; });
    set->add_option("--license",     set_a->license,     "project license")
       ->each([set_a](const std::string&){ set_a->has_license = true; });
    set->add_option("--copyright",   set_a->copyright,   "project copyright")
       ->each([set_a](const std::string&){ set_a->has_copyright = true; });
    set->add_option("--cover",       set_a->cover,
                    "cover image to embed (PNG or JPEG)")
       ->each([set_a](const std::string&){ set_a->has_cover = true; });
    set->add_option("--cover-name",  set_a->cover_name,
                    "select existing image in Model Pictures as cover "
                    "(mutually exclusive with --cover)")
       ->each([set_a](const std::string&){ set_a->has_cover_name = true; });

    set->callback([set_a, mode_out]() {
        OutputMode mode = resolve_mode(mode_out);
        if (!set_a->has_title && !set_a->has_desc && !set_a->has_license &&
            !set_a->has_copyright && !set_a->has_cover && !set_a->has_cover_name) {
            emit_error(mode, "usage_error", "at least one field must be specified for info set");
            std::exit(to_int(ExitCode::usage_error));
        }
        if (set_a->has_cover && set_a->has_cover_name) {
            emit_error(mode, "usage_error",
                       "--cover and --cover-name are mutually exclusive");
            std::exit(to_int(ExitCode::usage_error));
        }
        InfoSetParams p;
        if (set_a->has_title)     p.title       = set_a->title;
        if (set_a->has_desc)      p.description = set_a->description;
        if (set_a->has_license)   p.license     = set_a->license;
        if (set_a->has_copyright) p.copyright   = set_a->copyright;
        if (set_a->has_cover)     p.cover_path  = set_a->cover;
        if (set_a->has_cover_name) {
            try {
                p.cover_name = sanitize_aux_name(set_a->cover_name);
            } catch (const AuxNameError& e) {
                emit_error(mode, "usage_error",
                           std::string("--cover-name: ") + e.what());
                std::exit(to_int(ExitCode::usage_error));
            }
        }

        const std::string out = set_a->output.empty() ? set_a->file : set_a->output;
        run_mutation(mode, set_a->file, out, [&p](ProjectState& state) {
            return info_set(state, p);
        });
    });

    // --- info clear ---
    auto* clr = info->add_subcommand("clear", "clear project info fields");
    auto clr_a = std::make_shared<InfoClearArgs>();
    clr->add_option("file",        clr_a->file,   "input .3mf")->required();
    clr->add_option("--output,-o", clr_a->output, "output .3mf (default: in-place)");
    clr->add_option("--field",     clr_a->field,  "comma-separated field names to clear")->required();

    clr->callback([clr_a, mode_out]() {
        OutputMode mode = resolve_mode(mode_out);
        const auto fields = split_fields(clr_a->field);
        const std::string out = clr_a->output.empty() ? clr_a->file : clr_a->output;
        run_mutation(mode, clr_a->file, out, [&fields](ProjectState& state) {
            return info_clear(state, fields);
        });
    });
}

// ---- project profile -------------------------------------------------------

struct ProfileShowArgs  { std::string file; };
struct ProfileSetArgs   {
    std::string file, output;
    std::string title, description, cover, cover_name;
    bool has_title{}, has_desc{}, has_cover{}, has_cover_name{};
};
struct ProfileClearArgs { std::string file, output, field; };

static void register_profile(CLI::App* project, OutputMode* mode_out) {
    auto* profile = project->add_subcommand("profile", "project profile fields (show/set/clear)");

    // --- profile show ---
    auto* show = profile->add_subcommand("show", "show project profile fields (read-only)");
    auto show_a = std::make_shared<ProfileShowArgs>();
    show->add_option("file", show_a->file, "input .3mf")->required();
    show->callback([show_a, mode_out]() {
        OutputMode mode = resolve_mode(mode_out);
        ProjectState state;
        IoResult lr = load_project(show_a->file, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }
        emit_profile_show(mode, profile_show(state));
    });

    // --- profile set ---
    auto* set = profile->add_subcommand("set", "set project profile fields");
    auto set_a = std::make_shared<ProfileSetArgs>();
    set->add_option("file",          set_a->file,        "input .3mf")->required();
    set->add_option("--output,-o",   set_a->output,      "output .3mf (default: in-place)");
    set->add_option("--title",       set_a->title,       "profile title")
       ->each([set_a](const std::string&){ set_a->has_title = true; });
    set->add_option("--description", set_a->description, "profile description")
       ->each([set_a](const std::string&){ set_a->has_desc = true; });
    set->add_option("--cover",       set_a->cover,
                    "cover image to embed (PNG or JPEG)")
       ->each([set_a](const std::string&){ set_a->has_cover = true; });
    set->add_option("--cover-name",  set_a->cover_name,
                    "select existing image in Profile Pictures as cover "
                    "(mutually exclusive with --cover)")
       ->each([set_a](const std::string&){ set_a->has_cover_name = true; });

    set->callback([set_a, mode_out]() {
        OutputMode mode = resolve_mode(mode_out);
        if (!set_a->has_title && !set_a->has_desc &&
            !set_a->has_cover && !set_a->has_cover_name) {
            emit_error(mode, "usage_error", "at least one field must be specified for profile set");
            std::exit(to_int(ExitCode::usage_error));
        }
        if (set_a->has_cover && set_a->has_cover_name) {
            emit_error(mode, "usage_error",
                       "--cover and --cover-name are mutually exclusive");
            std::exit(to_int(ExitCode::usage_error));
        }
        ProfileSetParams p;
        if (set_a->has_title) p.title       = set_a->title;
        if (set_a->has_desc)  p.description = set_a->description;
        if (set_a->has_cover) p.cover_path  = set_a->cover;
        if (set_a->has_cover_name) {
            try {
                p.cover_name = sanitize_aux_name(set_a->cover_name);
            } catch (const AuxNameError& e) {
                emit_error(mode, "usage_error",
                           std::string("--cover-name: ") + e.what());
                std::exit(to_int(ExitCode::usage_error));
            }
        }

        const std::string out = set_a->output.empty() ? set_a->file : set_a->output;
        run_mutation(mode, set_a->file, out, [&p](ProjectState& state) {
            return profile_set(state, p);
        });
    });

    // --- profile clear ---
    auto* clr = profile->add_subcommand("clear", "clear project profile fields");
    auto clr_a = std::make_shared<ProfileClearArgs>();
    clr->add_option("file",        clr_a->file,   "input .3mf")->required();
    clr->add_option("--output,-o", clr_a->output, "output .3mf (default: in-place)");
    clr->add_option("--field",     clr_a->field,  "comma-separated field names to clear")->required();

    clr->callback([clr_a, mode_out]() {
        OutputMode mode = resolve_mode(mode_out);
        const auto fields = split_fields(clr_a->field);
        const std::string out = clr_a->output.empty() ? clr_a->file : clr_a->output;
        run_mutation(mode, clr_a->file, out, [&fields](ProjectState& state) {
            return profile_clear(state, fields);
        });
    });
}

// ---- project aux -----------------------------------------------------------

struct AuxListArgs   { std::string file; };
struct AuxAddArgs    {
    std::string input, folder_str, file_path, name, output;
    bool force = false;
};
struct AuxRemoveArgs { std::string file, folder_str, name, output; };
struct AuxExportArgs { std::string file, folder_str, name, to; };

static AuxFolder parse_folder(const std::string& s, OutputMode mode) {
    if (s == "model-pictures")    return AuxFolder::ModelPictures;
    if (s == "profile-pictures")  return AuxFolder::ProfilePictures;
    if (s == "bill-of-materials") return AuxFolder::BillOfMaterials;
    if (s == "assembly-guide")    return AuxFolder::AssemblyGuide;
    if (s == "others")            return AuxFolder::Others;
    emit_error(mode, "usage_error", "unknown folder: " + s +
               " (expected: model-pictures|profile-pictures|bill-of-materials|assembly-guide|others)");
    std::exit(to_int(ExitCode::usage_error));
}

static void register_aux(CLI::App* project, OutputMode* mode_out) {
    auto* aux = project->add_subcommand("aux", "project auxiliary files (list/add/remove/export)");

    // --- aux list ---
    auto* lst = aux->add_subcommand("list", "list auxiliary files in all folders");
    auto lst_a = std::make_shared<AuxListArgs>();
    lst->add_option("file", lst_a->file, "input .3mf")->required();
    lst->callback([lst_a, mode_out]() {
        OutputMode mode = resolve_mode(mode_out);
        ProjectState state;
        IoResult lr = load_project(lst_a->file, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }
        const auto entries = aux_list(state);

        auto to_json = [](const AuxEntry& e) -> nlohmann::json {
            nlohmann::json j;
            j["folder"] = folder_json_key(e.folder);
            j["name"]   = e.name;
            j["size"]   = e.size;
            return j;
        };
        auto to_line = [](size_t, const AuxEntry& e) -> std::string {
            return folder_json_key(e.folder) + "/" + e.name +
                   " (" + std::to_string(e.size) + " bytes)\n";
        };
        emit_list_response(mode, "aux files", "count", "items", entries, to_json, to_line);
    });

    // --- aux add ---
    auto* add = aux->add_subcommand("add", "add a file to an auxiliary folder");
    auto add_a = std::make_shared<AuxAddArgs>();
    add->add_option("input",       add_a->input,      "input .3mf")->required();
    add->add_option("--folder",    add_a->folder_str, "target folder")->required();
    add->add_option("--file",      add_a->file_path,  "source file path")->required();
    add->add_option("--name",      add_a->name,       "override name in archive");
    add->add_flag ("--force",      add_a->force,      "overwrite if name exists");
    add->add_option("--output,-o", add_a->output,     "output .3mf (default: in-place)");

    add->callback([add_a, mode_out]() {
        OutputMode mode = resolve_mode(mode_out);
        const AuxFolder folder = parse_folder(add_a->folder_str, mode);
        AuxAddParams p;
        p.input_path  = add_a->input;
        p.folder      = folder;
        p.file_path   = add_a->file_path;
        p.name        = add_a->name;
        p.force       = add_a->force;
        const std::string out = add_a->output.empty() ? add_a->input : add_a->output;
        run_mutation(mode, add_a->input, out, [&p](ProjectState& state) {
            return aux_add(state, p);
        });
    });

    // --- aux remove ---
    auto* rm = aux->add_subcommand("remove", "remove a file from an auxiliary folder");
    auto rm_a = std::make_shared<AuxRemoveArgs>();
    rm->add_option("file",        rm_a->file,       "input .3mf")->required();
    rm->add_option("--folder",    rm_a->folder_str, "target folder")->required();
    rm->add_option("--name",      rm_a->name,       "file name in archive")->required();
    rm->add_option("--output,-o", rm_a->output,     "output .3mf (default: in-place)");

    rm->callback([rm_a, mode_out]() {
        OutputMode mode = resolve_mode(mode_out);
        const AuxFolder folder = parse_folder(rm_a->folder_str, mode);
        const std::string nm   = rm_a->name;
        const std::string out  = rm_a->output.empty() ? rm_a->file : rm_a->output;
        run_mutation(mode, rm_a->file, out, [folder, nm](ProjectState& state) {
            return aux_remove(state, folder, nm);
        });
    });

    // --- aux export ---
    auto* exp = aux->add_subcommand("export", "export a file from an auxiliary folder");
    auto exp_a = std::make_shared<AuxExportArgs>();
    exp->add_option("file",     exp_a->file,       "input .3mf")->required();
    exp->add_option("--folder", exp_a->folder_str, "source folder")->required();
    exp->add_option("--name",   exp_a->name,       "file name in archive")->required();
    exp->add_option("--to",     exp_a->to,         "destination path")->required();

    exp->callback([exp_a, mode_out]() {
        OutputMode mode = resolve_mode(mode_out);
        const AuxFolder folder = parse_folder(exp_a->folder_str, mode);
        ProjectState state;
        IoResult lr = load_project(exp_a->file, state);
        if (!lr.ok) { emit_error(mode, lr.error_code, lr.error_message); std::exit(lr.exit_code); }
        try {
            aux_export(state, folder, exp_a->name, exp_a->to);
        } catch (const std::out_of_range& e) {
            emit_error(mode, "unknown_reference", e.what());
            std::exit(to_int(ExitCode::unknown_reference));
        } catch (const std::invalid_argument& e) {
            emit_error(mode, "usage_error", e.what());
            std::exit(to_int(ExitCode::usage_error));
        }
        emit_ok(mode, "ok", "exported " + exp_a->name);
    });
}

} // anonymous namespace

// ============================================================
// Public entry point
// ============================================================

void register_project_tab_subcommands(CLI::App* project, OutputMode* mode_out) {
    register_info(project, mode_out);
    register_profile(project, mode_out);
    register_aux(project, mode_out);
}

} // namespace bambu_cli
