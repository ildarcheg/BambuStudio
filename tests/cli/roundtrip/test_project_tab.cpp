// Roundtrip tests for bambu-cli project-tab edits (cross-project port from
// OrcaSlicer tests/cli/roundtrip/test_project_tab.cpp). Verifies that:
//   (a) info/profile/aux field edits survive a save/load round-trip
//   (b) --cover bytes + pointer survive a save/load round-trip
//   (c) aux add then aux remove leaves the bucket empty
//
// Path divergence from Orca: Bambu embeds the cover at
// "Auxiliaries/cover.png" (matches Bambu GUI behavior — see
// docs/cli/notes/2026-05-21-bbs-profile-storage.md). Orca uses
// "Auxiliaries/.thumbnails/thumbnail_3mf.png". This test asserts Bambu's path.
#include "../test_helpers.hpp"

#include "io.hpp"
#include "project_ops.hpp"
#include "project_tab_ops.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

#include <fstream>
#include <vector>

namespace fs = boost::filesystem;
using namespace bambu_cli_test;

namespace {
std::vector<unsigned char> read_all(const fs::path& p) {
    std::ifstream f(p.string(), std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f),
                                      std::istreambuf_iterator<char>{});
}
}

TEST_CASE("bambu-cli: project info+profile+aux survive save/load roundtrip",
          "[bambu-cli][roundtrip][project_tab]")
{
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const std::string cube = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    REQUIRE(fs::exists(cube));
    const std::string in = fresh_temp_path("_ptab_rt.3mf");
    fs::copy_file(ref, in, fs::copy_options::overwrite_existing);

    REQUIRE(spawn_cli({"project","info","set",in,
                       "--title","RT","--description","D","--license","MIT"}).exit_code == 0);
    REQUIRE(spawn_cli({"project","profile","set",in,
                       "--title","RPT","--description","RPD"}).exit_code == 0);
    REQUIRE(spawn_cli({"project","aux","add",in,
                       "--folder","others","--file",cube,"--name","x.bin"}).exit_code == 0);

    bambu_cli::ProjectState s;
    REQUIRE(bambu_cli::load_project(in, s).ok);
    auto iv = bambu_cli::info_show(s);
    auto pv = bambu_cli::profile_show(s);
    REQUIRE(iv.title       == "RT");
    REQUIRE(iv.description == "D");
    REQUIRE(iv.license     == "MIT");
    REQUIRE(pv.title       == "RPT");
    REQUIRE(pv.description == "RPD");
    auto entries = bambu_cli::aux_list(s);
    bool saw = false;
    for (const auto& e : entries)
        if (e.folder == bambu_cli::AuxFolder::Others && e.name == "x.bin") saw = true;
    REQUIRE(saw);

    fs::remove(in);
}

TEST_CASE("bambu-cli: project info set --cover survives save/load (pointer + bytes)",
          "[bambu-cli][roundtrip][project_tab]")
{
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const fs::path png = fs::path(BAMBU_CLI_FIXTURE_STL_DIR) / ".." / "cover_smoke.png";
    REQUIRE(fs::exists(png));
    const auto src_bytes = read_all(png);
    const std::string in = fresh_temp_path("_ptab_cover_rt.3mf");
    fs::copy_file(ref, in, fs::copy_options::overwrite_existing);

    REQUIRE(spawn_cli({"project","info","set",in,"--cover",png.string()}).exit_code == 0);

    bambu_cli::ProjectState s;
    REQUIRE(bambu_cli::load_project(in, s).ok);
    REQUIRE(s.model.model_info != nullptr);
    // Bambu cover-path divergence: "Auxiliaries/cover.png" (vs Orca's
    // "Auxiliaries/.thumbnails/thumbnail_3mf.png").
    REQUIRE(s.model.model_info->cover_file == "Auxiliaries/cover.png");
    const fs::path landed = fs::path(s.model.get_auxiliary_file_temp_path()) / "cover.png";
    REQUIRE(fs::exists(landed));
    REQUIRE(read_all(landed) == src_bytes);

    fs::remove(in);
}

TEST_CASE("bambu-cli: project aux add then remove leaves bucket empty after roundtrip",
          "[bambu-cli][roundtrip][project_tab]")
{
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const std::string cube = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/cube.stl";
    REQUIRE(fs::exists(cube));
    const std::string in = fresh_temp_path("_ptab_aux_rt.3mf");
    fs::copy_file(ref, in, fs::copy_options::overwrite_existing);

    REQUIRE(spawn_cli({"project","aux","add",in,"--folder","pictures",
                       "--file",cube,"--name","x.png"}).exit_code == 0);
    REQUIRE(spawn_cli({"project","aux","remove",in,"--folder","pictures",
                       "--name","x.png"}).exit_code == 0);

    bambu_cli::ProjectState s;
    REQUIRE(bambu_cli::load_project(in, s).ok);
    auto entries = bambu_cli::aux_list(s);
    for (const auto& e : entries)
        REQUIRE(!(e.folder == bambu_cli::AuxFolder::Pictures && e.name == "x.png"));

    fs::remove(in);
}
