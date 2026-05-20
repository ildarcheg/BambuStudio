#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <regex>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

// Helper: get the first plate name from a 3MF.
static std::string first_plate_name_cfg(const std::string& path) {
    auto r = spawn_cli({"--json", "plate", "list", path});
    REQUIRE(r.exit_code == 0);
    auto p = r.stdout_text.find("\"plates\":[\"");
    if (p == std::string::npos) return "";
    p += std::string("\"plates\":[\"").size();
    auto q = r.stdout_text.find("\"", p);
    return r.stdout_text.substr(p, q - p);
}

// ---- Test 1: project-level config set persists in archive -----------------
TEST_CASE("config set: project-level line_width override persists in archive", "[m7][config_set]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    auto r = spawn_cli({"config", "set", out, "--key", "line_width", "--value", "0.5"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    // Verify via archive: Metadata/project_settings.config must contain line_width and 0.5.
    auto bytes = read_zip_entry(out, "Metadata/project_settings.config");
    REQUIRE_FALSE(bytes.empty());
    std::string xml(bytes.begin(), bytes.end());
    INFO("project_settings.config: " << xml.substr(0, std::min((int)xml.size(), 500)));

    REQUIRE(xml.find("line_width") != std::string::npos);
    REQUIRE(xml.find("0.5") != std::string::npos);
}

// ---- Test 2: per-object config set appears in model_settings.config -------
TEST_CASE("config set --object: per-object override appears in model_settings.config", "[m7][config_set_object]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    // First add a cube object.
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name_cfg(out);
    auto add_r = spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl});
    INFO("object add stderr: " << add_r.stderr_text);
    REQUIRE(add_r.exit_code == 0);

    // Set per-object line_width on the cube.
    auto r = spawn_cli({"config", "set", out, "--object", "cube",
                        "--key", "line_width", "--value", "0.4"});
    INFO("config set stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    // Verify via archive: Metadata/model_settings.config must contain line_width and 0.4.
    auto bytes = read_zip_entry(out, "Metadata/model_settings.config");
    REQUIRE_FALSE(bytes.empty());
    std::string xml(bytes.begin(), bytes.end());
    INFO("model_settings.config: " << xml.substr(0, std::min((int)xml.size(), 1000)));

    REQUIRE(xml.find("line_width") != std::string::npos);
    REQUIRE(xml.find("0.4") != std::string::npos);
}

// ---- Test 3: unknown key -> exit 4 (bad_config) ---------------------------
TEST_CASE("config set: unknown key -> exit 4 (bad_config)", "[m7][config_bad_key]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    auto r = spawn_cli({"config", "set", out, "--key", "no_such_key_xyz", "--value", "1"});
    INFO("stdout: " << r.stdout_text);
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 4);
    REQUIRE(r.stderr_text.find("bad_config") != std::string::npos);
}

// ---- Test 4: config list --changed-only shows set key --------------------
TEST_CASE("config list --changed-only: diff matches set/unset round-trip", "[m7][config_list]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    // Set line_width = 0.5.
    auto set_r = spawn_cli({"config", "set", out, "--key", "line_width", "--value", "0.5"});
    INFO("set stderr: " << set_r.stderr_text);
    REQUIRE(set_r.exit_code == 0);

    // List changed-only — must include line_width=0.5.
    auto list_r = spawn_cli({"--json", "config", "list", out, "--changed-only"});
    INFO("list stderr: " << list_r.stderr_text);
    INFO("list stdout: " << list_r.stdout_text);
    REQUIRE(list_r.exit_code == 0);
    REQUIRE(list_r.stdout_text.find("line_width") != std::string::npos);
    REQUIRE(list_r.stdout_text.find("0.5") != std::string::npos);
}

// ---- Test 5: config unset removes the key --------------------------------
TEST_CASE("config unset: removes the key", "[m7][config_unset]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    // Set line_width first.
    auto set_r = spawn_cli({"config", "set", out, "--key", "line_width", "--value", "0.5"});
    INFO("set stderr: " << set_r.stderr_text);
    REQUIRE(set_r.exit_code == 0);

    // Unset it.
    auto unset_r = spawn_cli({"config", "unset", out, "--key", "line_width"});
    INFO("unset stderr: " << unset_r.stderr_text);
    REQUIRE(unset_r.exit_code == 0);

    // List changed-only — must NOT contain the exact key "line_width" (it was unset).
    // Use the JSON key pattern to avoid false positives from keys that contain
    // "line_width" as a substring (e.g. "initial_layer_line_width").
    auto list_r = spawn_cli({"--json", "config", "list", out, "--changed-only"});
    INFO("list stderr: " << list_r.stderr_text);
    INFO("list stdout: " << list_r.stdout_text);
    REQUIRE(list_r.exit_code == 0);
    // The JSON format is: {"key":"line_width","value":"..."} — search for the
    // exact key pattern to avoid matching keys that contain "line_width" as a
    // substring (e.g. "initial_layer_line_width").
    REQUIRE(list_r.stdout_text.find("\"key\":\"line_width\"") == std::string::npos);
}

// ---- Test 6 (hotfix): different_settings_to_system tracking ---------------
// Verifies that BS will actually honor project-level overrides by ensuring the
// special different_settings_to_system key is updated by config set/unset.
// BS's PresetBundle::validate_presets reads this key to know which keys were
// user-modified. Without it BS silently ignores our project-level overrides.
TEST_CASE("config set: project-level override registers in different_settings_to_system (BS honor)",
          "[m7][hotfix][bs_honor]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    // --- Step 1: set line_width = 0.6 ---
    auto set_r = spawn_cli({"config", "set", out, "--key", "line_width", "--value", "0.6"});
    INFO("set stderr: " << set_r.stderr_text);
    REQUIRE(set_r.exit_code == 0);

    // Verify via archive: project_settings.config must contain line_width=0.6
    // AND different_settings_to_system with line_width in position 0.
    {
        auto bytes = read_zip_entry(out, "Metadata/project_settings.config");
        REQUIRE_FALSE(bytes.empty());
        std::string xml(bytes.begin(), bytes.end());
        INFO("project_settings.config after set:\n" << xml.substr(0, std::min((int)xml.size(), 1000)));

        // The value must be updated.
        REQUIRE(xml.find("\"line_width\": \"0.6\"") != std::string::npos);

        // different_settings_to_system must be present as a JSON array.
        REQUIRE(xml.find("\"different_settings_to_system\"") != std::string::npos);

        // Find the array content. We need position 0 (the process-tab slot) to
        // contain "line_width". The JSON format produced by BS is an array like:
        //   "different_settings_to_system": [
        //       "line_width",   <- position 0 (process-tab)
        //       "",
        //       ...
        //   ]
        // After our set, position 0 must be "line_width" (or a semicolon-list
        // that contains "line_width" as a substring).
        auto pos = xml.find("\"different_settings_to_system\"");
        REQUIRE(pos != std::string::npos);
        // Find the opening bracket of the array value.
        auto bracket = xml.find('[', pos);
        REQUIRE(bracket != std::string::npos);
        // Find the closing bracket.
        auto close_bracket = xml.find(']', bracket);
        REQUIRE(close_bracket != std::string::npos);
        std::string array_content = xml.substr(bracket, close_bracket - bracket + 1);
        INFO("different_settings_to_system array: " << array_content);
        // Position 0 is the first quoted string in the array — it must contain "line_width".
        REQUIRE(array_content.find("line_width") != std::string::npos);
        // Verify it appears in the first array element (before the second comma).
        // The first element is the substring between the first '"' and its closing '"'.
        auto first_quote = array_content.find('"');
        REQUIRE(first_quote != std::string::npos);
        auto first_close = array_content.find('"', first_quote + 1);
        REQUIRE(first_close != std::string::npos);
        std::string first_elem = array_content.substr(first_quote + 1,
                                                       first_close - first_quote - 1);
        INFO("different_settings_to_system[0]: " << first_elem);
        REQUIRE(first_elem.find("line_width") != std::string::npos);
    }

    // --- Step 2: unset line_width — position 0 must no longer contain it ---
    auto unset_r = spawn_cli({"config", "unset", out, "--key", "line_width"});
    INFO("unset stderr: " << unset_r.stderr_text);
    REQUIRE(unset_r.exit_code == 0);

    {
        auto bytes2 = read_zip_entry(out, "Metadata/project_settings.config");
        REQUIRE_FALSE(bytes2.empty());
        std::string xml2(bytes2.begin(), bytes2.end());
        INFO("project_settings.config after unset:\n" << xml2.substr(0, std::min((int)xml2.size(), 1000)));

        // different_settings_to_system must still be present (BS relies on its
        // presence; we never erase the option, we just clear position 0).
        REQUIRE(xml2.find("\"different_settings_to_system\"") != std::string::npos);

        // Position 0 must NOT contain "line_width" anymore.
        auto pos2 = xml2.find("\"different_settings_to_system\"");
        REQUIRE(pos2 != std::string::npos);
        auto bracket2 = xml2.find('[', pos2);
        REQUIRE(bracket2 != std::string::npos);
        auto close_bracket2 = xml2.find(']', bracket2);
        REQUIRE(close_bracket2 != std::string::npos);
        std::string array2 = xml2.substr(bracket2, close_bracket2 - bracket2 + 1);
        INFO("different_settings_to_system array after unset: " << array2);

        // Find position 0 (first element).
        auto first_quote2 = array2.find('"');
        REQUIRE(first_quote2 != std::string::npos);
        auto first_close2 = array2.find('"', first_quote2 + 1);
        REQUIRE(first_close2 != std::string::npos);
        std::string first_elem2 = array2.substr(first_quote2 + 1,
                                                 first_close2 - first_quote2 - 1);
        INFO("different_settings_to_system[0] after unset: " << first_elem2);
        REQUIRE(first_elem2.find("line_width") == std::string::npos);
    }
}
