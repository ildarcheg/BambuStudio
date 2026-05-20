#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <regex>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

// Helper: get the first plate name from a 3mf via `--json plate list`.
static std::string first_plate_name(const std::string& path) {
    auto r = spawn_cli({"--json", "plate", "list", path});
    REQUIRE(r.exit_code == 0);
    auto p = r.stdout_text.find("\"plates\":[\"");
    if (p == std::string::npos) return "";
    p += std::string("\"plates\":[\"").size();
    auto q = r.stdout_text.find("\"", p);
    return r.stdout_text.substr(p, q - p);
}

// Returns the substring of model_settings.config covering the <object> block
// whose full text contains <name_substring>. Empty if none found.
static std::string object_block(const std::string& xml, const std::string& name_substring) {
    static const std::regex obj_re(R"(<object[^>]*>([\s\S]*?)</object>)");
    auto begin = std::sregex_iterator(xml.begin(), xml.end(), obj_re);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        std::string body = (*it).str();
        if (body.find(name_substring) != std::string::npos) return body;
    }
    return {};
}

// -------------------------------------------------------------------
// Case 1: object remove — object is gone from the archive
// -------------------------------------------------------------------
TEST_CASE("object remove: object gone from archive", "[m9][object_remove]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    // Add then remove.
    REQUIRE(spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl}).exit_code == 0);
    auto r = spawn_cli({"object", "remove", out, "--name", "cube"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    // Verify: model_settings.config should contain no reference to cube.stl.
    auto bytes = read_zip_entry(out, "Metadata/model_settings.config");
    REQUIRE_FALSE(bytes.empty());
    std::string xml(bytes.begin(), bytes.end());
    REQUIRE(xml.find("cube.stl") == std::string::npos);

    fs::remove(out);
}

// -------------------------------------------------------------------
// Case 2: set-filament retrofit preserves source_file (Bug B retrofit guard)
// -------------------------------------------------------------------
TEST_CASE("object set-filament: retrofit preserves source_file (Bug B retrofit guard)",
          "[m9][set_filament][bug_b]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    // Add without filament, then retrofit.
    REQUIRE(spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl}).exit_code == 0);
    auto r = spawn_cli({"object", "set-filament", out, "--name", "cube", "--filament", "3"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    auto bytes = read_zip_entry(out, "Metadata/model_settings.config");
    REQUIRE_FALSE(bytes.empty());
    std::string xml(bytes.begin(), bytes.end());

    std::string body = object_block(xml, "cube");
    REQUIRE_FALSE(body.empty());

    SECTION("source_file attribution present (Bug B retrofit proof)") {
        REQUIRE(body.find("source_file") != std::string::npos);
        REQUIRE(body.find("cube.stl")    != std::string::npos);
    }
    SECTION("extruder = 3 set on this <object>") {
        std::regex extr_re(R"(extruder[^>]*value\s*=\s*"3")");
        REQUIRE(std::regex_search(body, extr_re));
    }

    fs::remove(out);
}

// -------------------------------------------------------------------
// Case 3: set-filament out of range -> exit 1
// -------------------------------------------------------------------
TEST_CASE("object set-filament: out of range -> exit 1", "[m9][set_filament]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    REQUIRE(spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl}).exit_code == 0);
    auto r = spawn_cli({"object", "set-filament", out, "--name", "cube", "--filament", "99"});
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);

    fs::remove(out);
}

// -------------------------------------------------------------------
// Case 4 (group semantics): object remove --count 3 removes all 3 copies
// -------------------------------------------------------------------
TEST_CASE("object remove: removes all N copies by name", "[m9][object_remove][group_semantics]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    // Add 3 copies — produces 3 ModelObjects all named "cube".
    REQUIRE(spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl,
                       "--count", "3"}).exit_code == 0);

    // Verify 3 objects exist before remove.
    {
        auto lr = spawn_cli({"--json", "object", "list", out});
        INFO("object list before remove: " << lr.stdout_text);
        REQUIRE(lr.exit_code == 0);
        // The JSON should report object_count >= 3.
        REQUIRE(lr.stdout_text.find("\"object_count\":3") != std::string::npos);
    }

    // Remove by name — should remove all 3.
    auto r = spawn_cli({"object", "remove", out, "--name", "cube"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    // Verify 0 objects named "cube" remain.
    {
        auto lr = spawn_cli({"--json", "object", "list", out});
        INFO("object list after remove: " << lr.stdout_text);
        REQUIRE(lr.exit_code == 0);
        REQUIRE(lr.stdout_text.find("\"object_count\":0") != std::string::npos);
    }

    // Also verify model_settings.config has no cube.stl reference.
    auto bytes = read_zip_entry(out, "Metadata/model_settings.config");
    std::string xml(bytes.begin(), bytes.end());
    REQUIRE(xml.find("cube.stl") == std::string::npos);

    fs::remove(out);
}

// -------------------------------------------------------------------
// Case 5 (group semantics): set-filament stamps extruder on all N copies
// -------------------------------------------------------------------
TEST_CASE("object set-filament: stamps extruder on all N copies by name",
          "[m9][set_filament][group_semantics]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    // Add 3 copies without filament assignment.
    REQUIRE(spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl,
                       "--count", "3"}).exit_code == 0);

    // Retrofit filament 2 on all 3 copies.
    auto r = spawn_cli({"object", "set-filament", out, "--name", "cube", "--filament", "2"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    // Inspect model_settings.config: all 3 <object> blocks for "cube" must have
    // extruder=2 AND source_file referencing cube.stl.
    auto bytes = read_zip_entry(out, "Metadata/model_settings.config");
    REQUIRE_FALSE(bytes.empty());
    std::string xml(bytes.begin(), bytes.end());

    // Count how many <object> blocks mention "cube.stl" and have extruder=2.
    static const std::regex obj_re(R"(<object[^>]*>([\s\S]*?)</object>)");
    static const std::regex extr2_re(R"(extruder[^>]*value\s*=\s*"2")");
    auto begin = std::sregex_iterator(xml.begin(), xml.end(), obj_re);
    auto end   = std::sregex_iterator();
    int cube_count  = 0;
    int extr2_count = 0;
    for (auto it = begin; it != end; ++it) {
        std::string body = (*it).str();
        if (body.find("cube.stl") != std::string::npos) {
            ++cube_count;
            // Must have source_file (Bug B retrofit guard check).
            REQUIRE(body.find("source_file") != std::string::npos);
            if (std::regex_search(body, extr2_re)) ++extr2_count;
        }
    }
    // All 3 copies must be present with extruder=2.
    REQUIRE(cube_count  == 3);
    REQUIRE(extr2_count == 3);

    fs::remove(out);
}
