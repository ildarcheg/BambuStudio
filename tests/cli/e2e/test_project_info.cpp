#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

static const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";
static const std::string kJpg = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.jpg";

// ---- info show ---------------------------------------------------------------

TEST_CASE("info show: exits 0 and emits 6 text lines", "[c1][info_show]") {
    const std::string f = canonical_committed_3mf();
    auto r = spawn_cli({"project", "info", "show", f});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_text.find("title:")       != std::string::npos);
    REQUIRE(r.stdout_text.find("description:") != std::string::npos);
    REQUIRE(r.stdout_text.find("license:")     != std::string::npos);
    REQUIRE(r.stdout_text.find("copyright:")   != std::string::npos);
    REQUIRE(r.stdout_text.find("cover:")       != std::string::npos);
    REQUIRE(r.stdout_text.find("origin:")      != std::string::npos);
}

TEST_CASE("info show: JSON has 6 data keys", "[c1][info_show_json]") {
    const std::string f = canonical_committed_3mf();
    auto r = spawn_cli({"--json", "project", "info", "show", f});
    INFO("stdout: " << r.stdout_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_text.find("\"title\"")       != std::string::npos);
    REQUIRE(r.stdout_text.find("\"description\"") != std::string::npos);
    REQUIRE(r.stdout_text.find("\"license\"")     != std::string::npos);
    REQUIRE(r.stdout_text.find("\"copyright\"")   != std::string::npos);
    REQUIRE(r.stdout_text.find("\"cover\"")       != std::string::npos);
    REQUIRE(r.stdout_text.find("\"origin\"")      != std::string::npos);
    REQUIRE(r.stdout_text.find("\"status\":\"ok\"") != std::string::npos);
}

TEST_CASE("info show: missing file -> exit 2", "[c1][info_show_missing]") {
    auto r = spawn_cli({"project", "info", "show", "Z:/no/such/file.3mf"});
    REQUIRE(r.exit_code == 2);
}

// ---- info set ---------------------------------------------------------------

TEST_CASE("info set --title: persists title in 3MF metadata", "[c1][info_set_title]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    auto r = spawn_cli({"project", "info", "set", out, "--title", "My Test Title"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    // Verify title is readable back via info show.
    auto r2 = spawn_cli({"--json", "project", "info", "show", out});
    REQUIRE(r2.exit_code == 0);
    REQUIRE(r2.stdout_text.find("My Test Title") != std::string::npos);
}

TEST_CASE("info set --description and --license: both persist", "[c1][info_set_multi]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    auto r = spawn_cli({"project", "info", "set", out,
                         "--description", "Some desc", "--license", "MIT"});
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "info", "show", out});
    REQUIRE(r2.stdout_text.find("Some desc") != std::string::npos);
    REQUIRE(r2.stdout_text.find("MIT")        != std::string::npos);
}

TEST_CASE("info set --cover PNG: cover embedded in Model Pictures", "[c1][info_set_cover]") {
    REQUIRE(fs::exists(kPng));  // fixture must be committed
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    auto r = spawn_cli({"project", "info", "set", out, "--cover", kPng});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    // The PNG bytes should now be in the archive under Auxiliaries/Model Pictures/<basename>.
    auto bytes = read_zip_entry(out, "Auxiliaries/Model Pictures/cover_smoke.png");
    REQUIRE_FALSE(bytes.empty());
    // Verify PNG signature.
    REQUIRE(bytes.size() >= 8);
    REQUIRE(bytes[0] == 0x89);
    REQUIRE(bytes[1] == 0x50); // 'P'
    REQUIRE(bytes[2] == 0x4E); // 'N'
    REQUIRE(bytes[3] == 0x47); // 'G'
}

TEST_CASE("info set --cover JPG: accepted, embedded in Model Pictures",
          "[c1][info_set_cover]") {
    REQUIRE(fs::exists(kJpg));  // fixture must be committed
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    auto r = spawn_cli({"project", "info", "set", out, "--cover", kJpg});
    INFO("stdout: " << r.stdout_text);
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    auto bytes = read_zip_entry(out, "Auxiliaries/Model Pictures/cover_smoke.jpg");
    REQUIRE_FALSE(bytes.empty());
    REQUIRE(bytes.size() >= 3);
    REQUIRE(bytes[0] == 0xFF);
    REQUIRE(bytes[1] == 0xD8);
    REQUIRE(bytes[2] == 0xFF);
}

TEST_CASE("info set: no fields -> exit 1 (usage error)", "[c1][info_set_no_fields]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);
    auto r = spawn_cli({"project", "info", "set", out});
    REQUIRE(r.exit_code == 1);
}

TEST_CASE("info set --output: writes to sidecar, does not modify source", "[c1][info_set_output]") {
    const std::string src = canonical_committed_3mf();
    const std::string out = fresh_temp_path(".3mf");

    auto r = spawn_cli({"project", "info", "set", src, "--title", "SidecarTitle", "--output", out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(fs::exists(out));

    // Source unchanged.
    auto r_src = spawn_cli({"--json", "project", "info", "show", src});
    REQUIRE(r_src.stdout_text.find("SidecarTitle") == std::string::npos);

    // Output has the title.
    auto r_out = spawn_cli({"--json", "project", "info", "show", out});
    REQUIRE(r_out.stdout_text.find("SidecarTitle") != std::string::npos);
}

// ---- info clear -------------------------------------------------------------

TEST_CASE("info clear --field title: clears the title field", "[c1][info_clear_title]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    // Set a title first.
    auto r1 = spawn_cli({"project", "info", "set", out, "--title", "ToBeCleared"});
    REQUIRE(r1.exit_code == 0);

    // Clear it.
    auto r2 = spawn_cli({"project", "info", "clear", out, "--field", "title"});
    INFO("stderr: " << r2.stderr_text);
    REQUIRE(r2.exit_code == 0);

    // Verify cleared.
    auto r3 = spawn_cli({"--json", "project", "info", "show", out});
    REQUIRE(r3.stdout_text.find("ToBeCleared") == std::string::npos);
}

TEST_CASE("info clear: multi-field comma-separated", "[c1][info_clear_multi]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    spawn_cli({"project", "info", "set", out, "--title", "T1", "--license", "L1"});
    auto r = spawn_cli({"project", "info", "clear", out, "--field", "title,license"});
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "info", "show", out});
    REQUIRE(r2.stdout_text.find("T1") == std::string::npos);
    REQUIRE(r2.stdout_text.find("L1") == std::string::npos);
}

TEST_CASE("info clear: unknown field -> exit 4 (invalid_field)", "[c1][info_clear_unknown]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);
    auto r = spawn_cli({"project", "info", "clear", out, "--field", "no_such_field"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 4);
}

TEST_CASE("info clear: idempotent on already-empty field", "[c1][info_clear_idempotent]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);
    // Clear a field that's already empty - should succeed (exit 0).
    auto r = spawn_cli({"project", "info", "clear", out, "--field", "license"});
    REQUIRE(r.exit_code == 0);
}

TEST_CASE("info set: --cover and --cover-name together fails with exit 1",
          "[c1][info_set_cover_name]") {
    const std::string src = canonical_committed_3mf();
    REQUIRE_FALSE(src.empty());
    const std::string dst = fresh_temp_path(".3mf");
    REQUIRE(read_zip_entry(src, "3D/3dmodel.model").size() > 0);  // sanity

    auto r = spawn_cli({
        "project", "info", "set",
        src,
        "--output", dst,
        "--cover", kPng,
        "--cover-name", "anything.png",
    });
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("mutually exclusive") != std::string::npos);
}

TEST_CASE("info set: --cover-name with path separator fails with exit 1",
          "[c1][info_set_cover_name]") {
    const std::string src = canonical_committed_3mf();
    const std::string dst = fresh_temp_path(".3mf");

    auto r = spawn_cli({
        "project", "info", "set",
        src,
        "--output", dst,
        "--cover-name", "subdir/cover.png",
    });
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("--cover-name") != std::string::npos);
}
