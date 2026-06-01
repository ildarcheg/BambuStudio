// Roundtrip tests for bambu-cli project apply.
//
// 1. empty-manifest roundtrip: project apply with zero operations must not
//    perturb the project state (plate count is preserved).
// 2. sequential-vs-batch equivalence: a batch manifest with N operations
//    produces the same ProjectState as N individual CLI calls in sequence.

#include "../test_helpers.hpp"
#include "../archive_invariants.hpp"

#include "io.hpp"
#include "project_ops.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <fstream>

using namespace bambu_cli_test;
using bambu_cli::ProjectState;
namespace fs = boost::filesystem;

namespace {
std::string write_mf(const std::string& dir, const std::string& body) {
    std::string path = dir + "/m.json";
    std::ofstream(path) << body;
    return path;
}
} // namespace

TEST_CASE("apply roundtrip: empty manifest preserves the project",
          "[apply][roundtrip]") {
    const std::string in    = fresh_temp_path("_rt_apply_empty.3mf");
    const std::string out_a = fresh_temp_path("_rt_apply_empty_a.3mf");
    const std::string mfdir = fresh_temp_path("_rt_apply_empty_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);
    std::string mf = write_mf(mfdir, R"({"version":1,"operations":[]})");

    // Run: load + apply empty manifest + save.
    auto a = spawn_cli({"project", "apply", in,
                        "--manifest", mf, "--output", out_a});
    INFO("stderr: " << a.stderr_text);
    REQUIRE(a.exit_code == 0);
    REQUIRE(fs::exists(out_a));

    // Reload both files and assert the same plate count.
    ProjectState s1; REQUIRE(bambu_cli::load_project(out_a, s1).ok);
    ProjectState s0; REQUIRE(bambu_cli::load_project(in,    s0).ok);
    REQUIRE(bambu_cli::list_plate_names(s0).size()
         == bambu_cli::list_plate_names(s1).size());

    run_all_basic(out_a);
    fs::remove(in); fs::remove(out_a);
    fs::remove_all(mfdir);
}

TEST_CASE("apply roundtrip: sequential vs batch produce equivalent state",
          "[apply][roundtrip]") {
    // Two parallel projects: one built by 3 individual CLI calls, one by
    // a single project apply manifest. Reload both, assert plate names match.
    const std::string mf_in  = fresh_temp_path("_rt_apply_eq_mf_in.3mf");
    const std::string mf_out = fresh_temp_path("_rt_apply_eq_mf_out.3mf");
    const std::string seq    = fresh_temp_path("_rt_apply_eq_seq.3mf");
    const std::string mfdir  = fresh_temp_path("_rt_apply_eq_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), mf_in, fs::copy_options::overwrite_existing);
    fs::copy_file(canonical_committed_3mf(), seq,   fs::copy_options::overwrite_existing);

    // Sequential build via individual CLI calls.
    REQUIRE(spawn_cli({"plate", "add",    seq, "--name", "A"}).exit_code == 0);
    REQUIRE(spawn_cli({"plate", "add",    seq, "--name", "B"}).exit_code == 0);
    REQUIRE(spawn_cli({"plate", "rename", seq, "--from", "A", "--to", "A2"}).exit_code == 0);

    // Batch build via project apply.
    std::string mf = write_mf(mfdir,
        R"({"version":1,"operations":[)"
        R"({"op":"plate.add","name":"A"},)"
        R"({"op":"plate.add","name":"B"},)"
        R"({"op":"plate.rename","from":"A","to":"A2"}]})");
    auto r = spawn_cli({"project", "apply", mf_in, "--manifest", mf, "--output", mf_out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    ProjectState s_seq, s_batch;
    REQUIRE(bambu_cli::load_project(seq,    s_seq).ok);
    REQUIRE(bambu_cli::load_project(mf_out, s_batch).ok);
    REQUIRE(bambu_cli::list_plate_names(s_seq)
         == bambu_cli::list_plate_names(s_batch));

    fs::remove(mf_in); fs::remove(mf_out); fs::remove(seq);
    fs::remove_all(mfdir);
}
