#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

static const std::string kTxt = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../assembly_smoke.txt";
static const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";

// ---- aux list ---------------------------------------------------------------

TEST_CASE("aux list: exits 0 on empty archive (no aux files)", "[c3][aux_list]") {
    const std::string f = canonical_committed_3mf();
    auto r = spawn_cli({"project", "aux", "list", f});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
}

TEST_CASE("aux list: JSON has count and items keys", "[c3][aux_list_json]") {
    const std::string f = canonical_committed_3mf();
    auto r = spawn_cli({"--json", "project", "aux", "list", f});
    INFO("stdout: " << r.stdout_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_text.find("\"count\"") != std::string::npos);
    REQUIRE(r.stdout_text.find("\"items\"") != std::string::npos);
}

// ---- aux add ----------------------------------------------------------------

TEST_CASE("aux add: adds file to others folder", "[c3][aux_add]") {
    REQUIRE(fs::exists(kTxt));
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    auto r = spawn_cli({"project", "aux", "add", out,
                        "--folder", "others", "--file", kTxt});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    // Verify the file appears in aux list.
    auto r2 = spawn_cli({"--json", "project", "aux", "list", out});
    REQUIRE(r2.stdout_text.find("assembly_smoke.txt") != std::string::npos);
}

TEST_CASE("aux add: adds file to pictures folder", "[c3][aux_add]") {
    REQUIRE(fs::exists(kPng));
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    auto r = spawn_cli({"project", "aux", "add", out,
                        "--folder", "pictures", "--file", kPng});
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "aux", "list", out});
    REQUIRE(r2.stdout_text.find("cover_smoke.png") != std::string::npos);
}

TEST_CASE("aux add --name: override name in archive", "[c3][aux_add_name]") {
    REQUIRE(fs::exists(kTxt));
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    auto r = spawn_cli({"project", "aux", "add", out,
                        "--folder", "bom", "--file", kTxt, "--name", "my_bom.txt"});
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "aux", "list", out});
    REQUIRE(r2.stdout_text.find("my_bom.txt") != std::string::npos);
}

TEST_CASE("aux add: missing source file -> exit 2", "[c3][aux_add_missing]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);
    auto r = spawn_cli({"project", "aux", "add", out,
                        "--folder", "others", "--file", "Z:/no/such/file.txt"});
    REQUIRE(r.exit_code == 2);
}

TEST_CASE("aux add: collision without --force -> exit 5", "[c3][aux_add_collision]") {
    REQUIRE(fs::exists(kTxt));
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    // First add succeeds.
    spawn_cli({"project", "aux", "add", out,
               "--folder", "others", "--file", kTxt});

    // Second add (same name, different content) without --force -> exit 5.
    // Use png as a "different" file with same name override.
    auto r = spawn_cli({"project", "aux", "add", out,
                        "--folder", "others", "--file", kPng,
                        "--name", "assembly_smoke.txt"});
    REQUIRE(r.exit_code == 5);
}

TEST_CASE("aux add --force: overwrites existing file", "[c3][aux_add_force]") {
    REQUIRE(fs::exists(kTxt));
    REQUIRE(fs::exists(kPng));
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    spawn_cli({"project", "aux", "add", out,
               "--folder", "others", "--file", kTxt});

    auto r = spawn_cli({"project", "aux", "add", out,
                        "--folder", "others", "--file", kPng,
                        "--name", "assembly_smoke.txt", "--force"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
}

// ---- aux remove -------------------------------------------------------------

TEST_CASE("aux remove: removes an added file", "[c3][aux_remove]") {
    REQUIRE(fs::exists(kTxt));
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    spawn_cli({"project", "aux", "add", out,
               "--folder", "others", "--file", kTxt});

    auto r = spawn_cli({"project", "aux", "remove", out,
                        "--folder", "others", "--name", "assembly_smoke.txt"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "aux", "list", out});
    REQUIRE(r2.stdout_text.find("assembly_smoke.txt") == std::string::npos);
}

TEST_CASE("aux remove: unknown name -> exit 6", "[c3][aux_remove_unknown]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);
    auto r = spawn_cli({"project", "aux", "remove", out,
                        "--folder", "others", "--name", "no_such_file.txt"});
    REQUIRE(r.exit_code == 6);
}

// ---- aux export -------------------------------------------------------------

TEST_CASE("aux export: exports file to directory", "[c3][aux_export]") {
    REQUIRE(fs::exists(kTxt));
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    spawn_cli({"project", "aux", "add", out,
               "--folder", "assembly-guide", "--file", kTxt});

    const std::string export_dir = fresh_temp_path("");
    fs::create_directories(export_dir);

    auto r = spawn_cli({"project", "aux", "export", out,
                        "--folder", "assembly-guide",
                        "--name", "assembly_smoke.txt",
                        "--to", export_dir});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    // File should exist in the export directory.
    REQUIRE(fs::exists(fs::path(export_dir) / "assembly_smoke.txt"));
}

TEST_CASE("aux export: unknown name -> exit 6", "[c3][aux_export_unknown]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);
    const std::string export_dir = fresh_temp_path("");
    fs::create_directories(export_dir);
    auto r = spawn_cli({"project", "aux", "export", out,
                        "--folder", "others", "--name", "no_such.txt",
                        "--to", export_dir});
    REQUIRE(r.exit_code == 6);
}
