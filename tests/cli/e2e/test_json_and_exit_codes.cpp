#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <vector>
#include <string>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

// Helper: get the first plate name from a 3MF via `--json plate list`.
static std::string first_plate_name_m10(const std::string& path) {
    auto r = spawn_cli({"--json", "plate", "list", path});
    auto p = r.stdout_text.find("\"plates\":[\"");
    if (p == std::string::npos) return "";
    p += std::string("\"plates\":[\"").size();
    auto q = r.stdout_text.find("\"", p);
    return r.stdout_text.substr(p, q - p);
}

// M10.1: verify every read-only command emits Shape A JSON on success.
TEST_CASE("--json: every command emits Shape A on success", "[m10][json]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);

    std::vector<std::vector<std::string>> commands = {
        {"--json", "inspect", out},
        {"--json", "plate", "list", out},
        {"--json", "config", "list", out},
    };
    for (auto& c : commands) {
        auto r = spawn_cli(c);
        // Use index 2 as a label for INFO; safe because all our commands have at least 3 args.
        INFO("cmd[2]: " << c[2]);
        INFO("stdout: " << r.stdout_text);
        INFO("stderr: " << r.stderr_text);
        REQUIRE(r.exit_code == 0);
        REQUIRE(r.stdout_text.find("\"status\":\"ok\"") != std::string::npos);
        REQUIRE(r.stdout_text.find("\"code\":")        != std::string::npos);
        REQUIRE(r.stdout_text.find("\"message\":")     != std::string::npos);
    }
    fs::remove(out);
}

// M10.1: verify errors emit Shape A error JSON to stderr with the correct fields.
TEST_CASE("--json: every error emits Shape A error", "[m10][json][errors]") {
    auto r = spawn_cli({"--json", "inspect", "Z:/no/such.3mf"});
    INFO("stdout: " << r.stdout_text);
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 2);
    REQUIRE(r.stderr_text.find("\"status\":\"error\"")        != std::string::npos);
    REQUIRE(r.stderr_text.find("\"code\":\"file_not_found\"") != std::string::npos);
}

// M10.2: one test case exercising each of the deterministically triggerable exit codes.
// Codes skipped: 3 (parse_failure), 7 (invalid_state), 8 (invariant_violation — covered in M1).
TEST_CASE("exit codes 1, 2, 4, 5, 6, 9: deterministic triggers", "[m10][exit_codes]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_option::overwrite_if_exists);
    const std::string stl   = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    const std::string plate = first_plate_name_m10(out);
    INFO("fixture plate name: " << plate);

    SECTION("exit 1 — usage_error: --filament 99 (out of range)") {
        auto r = spawn_cli({"object", "add", out, "--plate", plate,
                            "--stl", stl, "--filament", "99"});
        INFO("stderr: " << r.stderr_text);
        REQUIRE(r.exit_code == 1);
        REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);
    }

    SECTION("exit 2 — file_not_found: inspect on nonexistent path") {
        auto r = spawn_cli({"inspect", "Z:/no/such.3mf"});
        INFO("stderr: " << r.stderr_text);
        REQUIRE(r.exit_code == 2);
        REQUIRE(r.stderr_text.find("file_not_found") != std::string::npos);
    }

    SECTION("exit 4 — bad_config: unknown key") {
        auto r = spawn_cli({"config", "set", out,
                            "--key", "no_such_key_xyz", "--value", "v"});
        INFO("stderr: " << r.stderr_text);
        REQUIRE(r.exit_code == 4);
        REQUIRE(r.stderr_text.find("bad_config") != std::string::npos);
    }

    SECTION("exit 5 — duplicate_name: plate add same name twice") {
        REQUIRE(spawn_cli({"plate", "add", out, "--name", "p_dup_m10"}).exit_code == 0);
        auto r = spawn_cli({"plate", "add", out, "--name", "p_dup_m10"});
        INFO("stderr: " << r.stderr_text);
        REQUIRE(r.exit_code == 5);
        REQUIRE(r.stderr_text.find("duplicate_name") != std::string::npos);
    }

    SECTION("exit 6 — unknown_reference: object remove on nonexistent name") {
        auto r = spawn_cli({"object", "remove", out, "--name", "no_such_object_xyz"});
        INFO("stderr: " << r.stderr_text);
        REQUIRE(r.exit_code == 6);
        REQUIRE(r.stderr_text.find("unknown_reference") != std::string::npos);
    }

    SECTION("exit 9 — placement_failure: translate far off-bed") {
        auto r = spawn_cli({"object", "add", out, "--plate", plate,
                            "--stl", stl, "--translate", "9000,9000"});
        INFO("stderr: " << r.stderr_text);
        REQUIRE(r.exit_code == 9);
        REQUIRE(r.stderr_text.find("placement_failure") != std::string::npos);
    }

    fs::remove(out);
}

TEST_CASE("save failure: stale .tmp.3mf directory -> exit 7 + JSON envelope, "
          "not a crash", "[m10][exit_codes][io_errors]") {
    const std::string out = fresh_temp_path("_stale.3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    // Occupy the save scratch path with a non-empty directory. fs::remove()
    // can't clear it, so the save must fail via the documented error
    // envelope — not by letting a boost::filesystem exception escape to
    // std::terminate (abort exit code, no JSON).
    const std::string tmp = out + ".tmp.3mf";
    fs::create_directory(tmp);
    { std::ofstream f(tmp + "/occupant.txt"); f << "x"; }

    auto r = spawn_cli({"--json", "plate", "add", out, "--name", "StaleTmpPlate"});
    INFO("stdout: " << r.stdout_text);
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 7);
    REQUIRE(r.stderr_text.find("\"status\":\"error\"") != std::string::npos);
    REQUIRE(r.stderr_text.find("\"code\":\"invalid_state\"") != std::string::npos);

    fs::remove_all(tmp);
    fs::remove(out);
}
