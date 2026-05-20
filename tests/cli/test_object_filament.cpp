#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <regex>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

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
// whose full text contains <name_substring>. This block holds both the
// extruder metadata (at object level) and the nested <part> with source_file.
// Empty if none found.
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

TEST_CASE("object add --filament 2: <part> has BOTH extruder=2 AND source_file (Bug B regression)",
          "[m5][object_filament][bug_b]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    auto r = spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl, "--filament", "2"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    auto bytes = read_zip_entry(out, "Metadata/model_settings.config");
    REQUIRE_FALSE(bytes.empty());
    std::string xml(bytes.begin(), bytes.end());

    // The serializer writes extruder at the <object> level, source_file inside
    // the nested <part> — both within the same <object> block.
    std::string body = object_block(xml, "cube");
    REQUIRE_FALSE(body.empty());

    SECTION("source_file attribution present (Bug B fix proof)") {
        REQUIRE(body.find("source_file") != std::string::npos);
        REQUIRE(body.find("cube.stl")    != std::string::npos);
    }
    SECTION("extruder = 2 set on this <object>") {
        // Bambu Studio writes <metadata key="extruder" value="2"/> at the
        // object level (not inside <part>), but still within the <object> block.
        std::regex extr_re(R"(extruder[^>]*value\s*=\s*"2")");
        REQUIRE(std::regex_search(body, extr_re));
    }

    fs::remove(out);
}

TEST_CASE("object add --filament 5: out of range (only 4 slots) -> exit 1", "[m5][object_filament]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    auto r = spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl, "--filament", "5"});
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);
    fs::remove(out);
}

TEST_CASE("object add --filament 0: invalid (1-based) -> exit 1", "[m5][object_filament]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    auto r = spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl, "--filament", "0"});
    REQUIRE(r.exit_code == 1);
    fs::remove(out);
}
