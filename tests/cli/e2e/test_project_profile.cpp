#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

static const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";
static const std::string kJpg = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.jpg";

// ---- profile show -----------------------------------------------------------

TEST_CASE("profile show: exits 0 and emits 5 text fields", "[c2][profile_show]") {
    const std::string f = canonical_committed_3mf();
    auto r = spawn_cli({"project", "profile", "show", f});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_text.find("title:")       != std::string::npos);
    REQUIRE(r.stdout_text.find("description:") != std::string::npos);
    REQUIRE(r.stdout_text.find("cover:")       != std::string::npos);
    REQUIRE(r.stdout_text.find("user_id:")     != std::string::npos);
    REQUIRE(r.stdout_text.find("user_name:")   != std::string::npos);
}

TEST_CASE("profile show: JSON has 5 data keys", "[c2][profile_show_json]") {
    const std::string f = canonical_committed_3mf();
    auto r = spawn_cli({"--json", "project", "profile", "show", f});
    INFO("stdout: " << r.stdout_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_text.find("\"title\"")       != std::string::npos);
    REQUIRE(r.stdout_text.find("\"description\"") != std::string::npos);
    REQUIRE(r.stdout_text.find("\"cover\"")       != std::string::npos);
    REQUIRE(r.stdout_text.find("\"user_id\"")     != std::string::npos);
    REQUIRE(r.stdout_text.find("\"user_name\"")   != std::string::npos);
    REQUIRE(r.stdout_text.find("\"status\":\"ok\"") != std::string::npos);
}

TEST_CASE("profile show: missing file -> exit 2", "[c2][profile_show_missing]") {
    auto r = spawn_cli({"project", "profile", "show", "Z:/no/such/file.3mf"});
    REQUIRE(r.exit_code == 2);
}

// ---- profile set ------------------------------------------------------------

TEST_CASE("profile set --title: persists title", "[c2][profile_set_title]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    auto r = spawn_cli({"project", "profile", "set", out, "--title", "MyProfileTitle"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "profile", "show", out});
    REQUIRE(r2.exit_code == 0);
    REQUIRE(r2.stdout_text.find("MyProfileTitle") != std::string::npos);
}

TEST_CASE("profile set --description: persists description", "[c2][profile_set_desc]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    auto r = spawn_cli({"project", "profile", "set", out, "--description", "ProfileDesc"});
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "profile", "show", out});
    REQUIRE(r2.stdout_text.find("ProfileDesc") != std::string::npos);
}

TEST_CASE("profile set --cover PNG: cover embedded in Profile Pictures",
          "[c2][profile_set_cover]") {
    REQUIRE(fs::exists(kPng));
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    auto r = spawn_cli({"project", "profile", "set", out, "--cover", kPng});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    auto bytes = read_zip_entry(out, "Auxiliaries/Profile Pictures/cover_smoke.png");
    REQUIRE_FALSE(bytes.empty());
    REQUIRE(bytes.size() >= 8);
    REQUIRE(bytes[0] == 0x89);
    REQUIRE(bytes[1] == 0x50);
}

TEST_CASE("profile set --cover JPG: accepted, embedded in Profile Pictures",
          "[c2][profile_set_cover]") {
    REQUIRE(fs::exists(kJpg));
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);
    auto r = spawn_cli({"project", "profile", "set", out, "--cover", kJpg});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    auto bytes = read_zip_entry(out, "Auxiliaries/Profile Pictures/cover_smoke.jpg");
    REQUIRE_FALSE(bytes.empty());
    REQUIRE(bytes.size() >= 3);
    REQUIRE(bytes[0] == 0xFF);
    REQUIRE(bytes[1] == 0xD8);
    REQUIRE(bytes[2] == 0xFF);
}

TEST_CASE("profile set: no fields -> exit 1", "[c2][profile_set_no_fields]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);
    auto r = spawn_cli({"project", "profile", "set", out});
    REQUIRE(r.exit_code == 1);
}

TEST_CASE("profile set --output: writes sidecar, source unchanged", "[c2][profile_set_output]") {
    const std::string src = canonical_committed_3mf();
    const std::string out = fresh_temp_path(".3mf");

    auto r = spawn_cli({"project", "profile", "set", src,
                        "--title", "SidecarProfile", "--output", out});
    REQUIRE(r.exit_code == 0);
    REQUIRE(fs::exists(out));

    auto r_src = spawn_cli({"--json", "project", "profile", "show", src});
    REQUIRE(r_src.stdout_text.find("SidecarProfile") == std::string::npos);

    auto r_out = spawn_cli({"--json", "project", "profile", "show", out});
    REQUIRE(r_out.stdout_text.find("SidecarProfile") != std::string::npos);
}

// ---- profile clear ----------------------------------------------------------

TEST_CASE("profile clear --field title: clears title", "[c2][profile_clear_title]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    spawn_cli({"project", "profile", "set", out, "--title", "ClearMe"});
    auto r = spawn_cli({"project", "profile", "clear", out, "--field", "title"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "profile", "show", out});
    REQUIRE(r2.stdout_text.find("ClearMe") == std::string::npos);
}

TEST_CASE("profile clear: multi-field clear", "[c2][profile_clear_multi]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    spawn_cli({"project", "profile", "set", out, "--title", "T2", "--description", "D2"});
    auto r = spawn_cli({"project", "profile", "clear", out, "--field", "title,description"});
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "profile", "show", out});
    REQUIRE(r2.stdout_text.find("T2") == std::string::npos);
    REQUIRE(r2.stdout_text.find("D2") == std::string::npos);
}

TEST_CASE("profile clear: unknown field -> exit 4", "[c2][profile_clear_unknown]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);
    auto r = spawn_cli({"project", "profile", "clear", out, "--field", "no_such_field"});
    REQUIRE(r.exit_code == 4);
}

TEST_CASE("profile clear: user_id is not clearable -> exit 4", "[c2][profile_clear_readonly]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);
    auto r = spawn_cli({"project", "profile", "clear", out, "--field", "user_id"});
    REQUIRE(r.exit_code == 4);
}

TEST_CASE("profile clear: idempotent on already-empty field", "[c2][profile_clear_idempotent]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);
    auto r = spawn_cli({"project", "profile", "clear", out, "--field", "description"});
    REQUIRE(r.exit_code == 0);
}

TEST_CASE("profile set: --cover and --cover-name together fails with exit 1",
          "[c2][profile_set_cover_name]") {
    const std::string src = canonical_committed_3mf();
    const std::string dst = fresh_temp_path(".3mf");

    auto r = spawn_cli({
        "project", "profile", "set",
        src,
        "--output", dst,
        "--cover", kPng,
        "--cover-name", "anything.png",
    });
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("mutually exclusive") != std::string::npos);
}

TEST_CASE("profile set: --cover-name with path separator fails with exit 1",
          "[c2][profile_set_cover_name]") {
    const std::string src = canonical_committed_3mf();
    const std::string dst = fresh_temp_path(".3mf");

    auto r = spawn_cli({
        "project", "profile", "set",
        src,
        "--output", dst,
        "--cover-name", "subdir/cover.png",
    });
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("--cover-name") != std::string::npos);
}
