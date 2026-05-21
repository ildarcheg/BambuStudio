#include "test_helpers.hpp"
#include "archive_invariants.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

// Helper: return all plate names from a 3MF via plate list --json.
static std::vector<std::string> get_plate_names(const std::string& path) {
    auto r = spawn_cli({"--json", "plate", "list", path});
    REQUIRE(r.exit_code == 0);
    std::vector<std::string> names;
    // Parse "plates":["name1","name2",...] crudely.
    const std::string prefix = "\"plates\":[";
    auto pos = r.stdout_text.find(prefix);
    if (pos == std::string::npos) return names;
    pos += prefix.size();
    // Walk through quoted strings until we hit ']'.
    while (pos < r.stdout_text.size() && r.stdout_text[pos] != ']') {
        if (r.stdout_text[pos] == '"') {
            ++pos; // skip opening quote
            std::string name;
            while (pos < r.stdout_text.size() && r.stdout_text[pos] != '"') {
                name += r.stdout_text[pos++];
            }
            names.push_back(name);
            if (pos < r.stdout_text.size()) ++pos; // skip closing quote
        } else {
            ++pos; // skip comma or whitespace
        }
    }
    return names;
}

TEST_CASE("plate remove: empty plate is removed", "[m8][plate_remove]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    // Add an empty plate named "to-remove".
    auto add_r = spawn_cli({"plate", "add", out, "--name", "to-remove"});
    INFO("plate add stderr: " << add_r.stderr_text);
    REQUIRE(add_r.exit_code == 0);

    // Remove it.
    auto rem_r = spawn_cli({"plate", "remove", out, "--name", "to-remove"});
    INFO("plate remove stderr: " << rem_r.stderr_text);
    REQUIRE(rem_r.exit_code == 0);

    bambu_cli_test::run_all_basic(out);

    // Verify "to-remove" is gone from plate list.
    auto names = get_plate_names(out);
    for (const auto& n : names) {
        REQUIRE(n != "to-remove");
    }

    fs::remove(out);
}

TEST_CASE("plate remove: non-empty plate -> exit 6", "[m8][plate_remove]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    const std::string stl = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    REQUIRE(fs::exists(stl));

    // Add an empty plate, then add an object to it.
    auto add_plate_r = spawn_cli({"plate", "add", out, "--name", "with-object"});
    INFO("plate add stderr: " << add_plate_r.stderr_text);
    REQUIRE(add_plate_r.exit_code == 0);

    auto add_obj_r = spawn_cli({"object", "add", out, "--plate", "with-object", "--stl", stl});
    INFO("object add stderr: " << add_obj_r.stderr_text);
    REQUIRE(add_obj_r.exit_code == 0);

    // Attempt to remove the non-empty plate -> must fail with exit 6.
    auto rem_r = spawn_cli({"plate", "remove", out, "--name", "with-object"});
    INFO("plate remove stderr: " << rem_r.stderr_text);
    REQUIRE(rem_r.exit_code == 6);
    REQUIRE(rem_r.stderr_text.find("unknown_reference") != std::string::npos);

    fs::remove(out);
}

TEST_CASE("plate rename: name updated, no duplicates created", "[m8][plate_rename]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    // Add a plate with name "oldname".
    auto add_r = spawn_cli({"plate", "add", out, "--name", "oldname"});
    INFO("plate add stderr: " << add_r.stderr_text);
    REQUIRE(add_r.exit_code == 0);

    // Rename "oldname" -> "newname".
    auto ren_r = spawn_cli({"plate", "rename", out, "--from", "oldname", "--to", "newname"});
    INFO("plate rename stderr: " << ren_r.stderr_text);
    REQUIRE(ren_r.exit_code == 0);

    bambu_cli_test::run_all_basic(out);

    // Verify "newname" exists and "oldname" is gone.
    auto names = get_plate_names(out);
    bool has_newname = false;
    for (const auto& n : names) {
        REQUIRE(n != "oldname");
        if (n == "newname") has_newname = true;
    }
    REQUIRE(has_newname);

    fs::remove(out);
}

TEST_CASE("plate rename: target name in use -> exit 5", "[m8][plate_rename]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    // Add two plates "a" and "b".
    REQUIRE(spawn_cli({"plate", "add", out, "--name", "a"}).exit_code == 0);
    REQUIRE(spawn_cli({"plate", "add", out, "--name", "b"}).exit_code == 0);

    // Try to rename "a" to "b" — "b" already exists.
    auto ren_r = spawn_cli({"plate", "rename", out, "--from", "a", "--to", "b"});
    INFO("plate rename stderr: " << ren_r.stderr_text);
    REQUIRE(ren_r.exit_code == 5);
    REQUIRE(ren_r.stderr_text.find("duplicate_name") != std::string::npos);

    fs::remove(out);
}
