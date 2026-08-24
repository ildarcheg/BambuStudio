#include "test_helpers.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

// Green regression guard for non-ASCII metadata going through the CLI
// subprocess path. On Windows, narrow-main argv arrives in the active
// code page (typically windows-1252) — an em-dash typed directly in
// PowerShell becomes the single byte 0x97 and trips expat on reload as
// invalid UTF-8 (the Phase G symptom). This test does NOT reproduce that
// specific failure mode because boost::process routes through
// CreateProcessW after UTF-8→UTF-16 conversion, bypassing the ACP path.
// See commit cddaaa4b4 ("descope Phase A Task 2") for why the underlying
// argv bug isn't reproducible inside this repo's automated test infra.
TEST_CASE("info set --description with em-dash (non-ASCII): persists correctly",
          "[e2e][info_set_non_ascii]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    // UTF-8 em-dash (U+2014). boost::process on Windows routes through
    // CreateProcessW after ACP conversion, mirroring real-world PowerShell
    // invocations.
    const std::string em_dash_desc = "Resin print \xE2\x80\x94 test";

    auto r = spawn_cli({"project", "info", "set", out,
                        "--description", em_dash_desc});
    INFO("stderr: " << r.stderr_text);
    INFO("stdout: " << r.stdout_text);
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "info", "show", out});
    INFO("show stdout: " << r2.stdout_text);
    REQUIRE(r2.exit_code == 0);
    REQUIRE(r2.stdout_text.find("\xE2\x80\x94") != std::string::npos);
}

TEST_CASE("info set --title with CJK characters: persists correctly",
          "[e2e][info_set_non_ascii]") {
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(canonical_committed_3mf(), out, fs::copy_options::overwrite_existing);

    const std::string cjk_title = "\xE6\xB5\x8B\xE8\xAF\x95 v1"; // 测试 v1

    auto r = spawn_cli({"project", "info", "set", out, "--title", cjk_title});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    auto r2 = spawn_cli({"--json", "project", "info", "show", out});
    INFO("show stdout: " << r2.stdout_text);
    REQUIRE(r2.exit_code == 0);
    REQUIRE(r2.stdout_text.find("\xE6\xB5\x8B\xE8\xAF\x95") != std::string::npos);
}
