#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <cmath>
#include <regex>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

// Extract "object_count":N from a --json inspect output.
static int extract_object_count(const std::string& json) {
    static const std::regex re(R"(\"object_count\":(\d+))");
    std::smatch m;
    if (std::regex_search(json, m, re))
        return std::stoi(m[1].str());
    return -1;
}

static std::string first_plate_name(const std::string& path) {
    auto r = spawn_cli({"--json", "plate", "list", path});
    REQUIRE(r.exit_code == 0);
    auto p = r.stdout_text.find("\"plates\":[\"");
    if (p == std::string::npos) return "";
    p += std::string("\"plates\":[\"").size();
    auto q = r.stdout_text.find("\"", p);
    return r.stdout_text.substr(p, q - p);
}

TEST_CASE("object add --translate places object at given offset", "[m6][transforms]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    auto r = spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl,
                        "--translate", "50,40"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    // Archive-level: instance transforms are stored as the <item transform="...">
    // attribute in 3D/3dmodel.model (12-element row-major; tx=v[9], ty=v[10]).
    // parse_item_transforms converts these to Matrix4 with m[12]=tx, m[13]=ty.
    auto model_bytes = read_zip_entry(out, "3D/3dmodel.model");
    REQUIRE_FALSE(model_bytes.empty());
    std::string model_xml(model_bytes.begin(), model_bytes.end());
    INFO("3D/3dmodel.model (excerpt): " << model_xml.substr(
        std::max((int)model_xml.size() - 500, 0)));

    auto mats = parse_item_transforms(model_xml);
    INFO("item transform count: " << mats.size());

    bool found = false;
    for (const auto& mm : mats) {
        if (std::abs(mm.m[12] - 50.0) < 1e-3 && std::abs(mm.m[13] - 40.0) < 1e-3) {
            found = true; break;
        }
    }

    // Fallback: also check <matrix> elements in model_settings.config
    if (!found) {
        auto bytes = read_zip_entry(out, "Metadata/model_settings.config");
        if (!bytes.empty()) {
            std::string xml(bytes.begin(), bytes.end());
            auto config_mats = parse_all_matrices(xml);
            for (const auto& mm : config_mats) {
                if (std::abs(mm.m[12] - 50.0) < 1e-3 &&
                    std::abs(mm.m[13] - 40.0) < 1e-3) {
                    found = true; break;
                }
            }
        }
    }

    REQUIRE(found);
    fs::remove(out);
}

TEST_CASE("object add --translate off-bed -> exit 9 (placement_failure)", "[m6][transforms][offbed]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    auto r = spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl,
                        "--translate", "9000,9000"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 9);
    REQUIRE(r.stderr_text.find("placement_failure") != std::string::npos);
    // File is not modified on placement_failure (save never called).
    fs::remove(out);
}

TEST_CASE("object add --count 3 --translate stacks 3 copies", "[m6][transforms][count]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    auto r = spawn_cli({"object", "add", out, "--plate", plate, "--stl", stl,
                        "--translate", "30,30", "--count", "3"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    auto lr = spawn_cli({"--json", "object", "list", out});
    REQUIRE(lr.exit_code == 0);
    // 3 instances of one object — list_objects iterates via loaded_id/obj_inst_map,
    // so we expect 3 "cube" entries.
    int matches = 0;
    size_t pos = 0;
    while ((pos = lr.stdout_text.find("\"name\":\"cube\"", pos)) != std::string::npos) {
        ++matches; ++pos;
    }
    INFO("object list stdout: " << lr.stdout_text);
    REQUIRE(matches == 3);

    fs::remove(out);
}

// M6 hotfix regression: --count N must produce exactly N entries in object list
// AND N entries in inspect's object_count (one ModelObject per copy — required
// for correct BBS 3MF obj_inst_map round-trip: the BBS loader keys obj_inst_map
// on the 3MF object_id, so multiple instances of the same object collapse to a
// single entry on reload, breaking objects_and_instances reconstruction).
//
// Pre-hotfix (broken): N copies used a hardcoded 4-column step-grid with step=60mm
// and no clear_instances(), causing off-by-one counts and incorrect placement.
// Post-hotfix: N ModelObjects with sqrt-grid placement, clear_instances, and
// scale-only off-bed AABB.
TEST_CASE("object add --count 3: N ModelObjects, sqrt-grid, correct list rows [M6 hotfix]",
          "[m6][hotfix][regression]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name(out);

    // 1. Capture baseline object_count before add.
    auto ir_before = spawn_cli({"--json", "inspect", out});
    REQUIRE(ir_before.exit_code == 0);
    int base_count = extract_object_count(ir_before.stdout_text);
    REQUIRE(base_count >= 0);
    INFO("base object_count: " << base_count);

    // 2. Add with --count 3 (auto-arrange, no translate).
    auto ar = spawn_cli({"object", "add", out, "--plate", plate,
                         "--stl", stl, "--count", "3"});
    INFO("add stderr: " << ar.stderr_text);
    REQUIRE(ar.exit_code == 0);

    // 3. Reload and inspect: object_count delta must be exactly 3 (one per copy).
    //    The BBS 3MF format requires distinct 3MF object_ids per plate entry;
    //    each copy gets its own ModelObject so obj_inst_map can round-trip correctly.
    auto ir_after = spawn_cli({"--json", "inspect", out});
    REQUIRE(ir_after.exit_code == 0);
    int after_count = extract_object_count(ir_after.stdout_text);
    REQUIRE(after_count >= 0);
    INFO("after object_count: " << after_count);

    // THE INVARIANT: exactly N ModelObjects added (one per copy).
    REQUIRE(after_count - base_count == 3);

    // 4. object list must show 3 rows (one per copy/instance).
    auto lr = spawn_cli({"--json", "object", "list", out});
    REQUIRE(lr.exit_code == 0);
    int list_matches = 0;
    size_t pos = 0;
    while ((pos = lr.stdout_text.find("\"name\":\"cube\"", pos)) != std::string::npos) {
        ++list_matches; ++pos;
    }
    INFO("object list stdout: " << lr.stdout_text);
    REQUIRE(list_matches == 3);

    fs::remove(out);
}
