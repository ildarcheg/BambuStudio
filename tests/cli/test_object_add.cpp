#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <string>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

// Helper: name of the single plate in the committed reference 3mf.
// BS does not name plates by default; the prior spec uses "Plate 1" or empty.
// We probe via plate list and use the first non-empty (or empty) name.
static std::string first_plate_name(const std::string& path) {
    auto r = spawn_cli({"--json", "plate", "list", path});
    REQUIRE(r.exit_code == 0);
    // crude: pull first "plates":["..."] entry
    auto p = r.stdout_text.find("\"plates\":[\"");
    if (p == std::string::npos) return "";
    p += std::string("\"plates\":[\"").size();
    auto q = r.stdout_text.find("\"", p);
    return r.stdout_text.substr(p, q - p);
}

TEST_CASE("object add: STL appears on plate; source_file stamped", "[m4][object_add]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    REQUIRE(fs::exists(stl));

    const std::string plate = first_plate_name(out);

    auto r = spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    SECTION("model_settings.config carries source_file for the new part (Bug B regression)") {
        auto bytes = read_zip_entry(out, "Metadata/model_settings.config");
        REQUIRE_FALSE(bytes.empty());
        std::string xml(bytes.begin(), bytes.end());
        REQUIRE(xml.find("source_file") != std::string::npos);
        REQUIRE(xml.find("cube.stl")    != std::string::npos);
    }

    SECTION("object list reports the new object") {
        auto lr = spawn_cli({"--json", "object", "list", out});
        REQUIRE(lr.exit_code == 0);
        REQUIRE(lr.stdout_text.find("cube") != std::string::npos);
    }

    fs::remove(out);
}

TEST_CASE("object add: missing STL -> exit 2", "[m4][object_add]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    auto r = spawn_cli({"object", "add", out, "--plate", first_plate_name(out),
                        "--stl", "Z:/no/such/file.stl"});
    REQUIRE(r.exit_code == 2);
    fs::remove(out);
}

TEST_CASE("object add: unknown plate -> exit 6", "[m4][object_add]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";

    auto r = spawn_cli({"object", "add", out, "--plate", "no-such-plate", "--stl", stl});
    REQUIRE(r.exit_code == 6);
    fs::remove(out);
}
