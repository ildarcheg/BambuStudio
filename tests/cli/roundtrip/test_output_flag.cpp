// Roundtrip test for the --output side-car flag (cross-project port from
// OrcaSlicer tests/cli/roundtrip/test_output_flag.cpp). Verifies that
// `plate add IN --name N --output OUT` writes OUT and leaves IN
// byte-identical (size + mtime unchanged) with only OUT carrying the new
// plate.
#include "../test_helpers.hpp"

#include "io.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
using namespace bambu_cli_test;

TEST_CASE("bambu-cli: --output leaves input byte-identical, writes side-car",
          "[bambu-cli][roundtrip][output_flag]")
{
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const std::string in  = fresh_temp_path("_outflag_in.3mf");
    const std::string out = fresh_temp_path("_outflag_out.3mf");
    fs::copy_file(ref, in, fs::copy_options::overwrite_existing);

    const auto in_size_before  = fs::file_size(in);
    const auto in_mtime_before = fs::last_write_time(in);

    auto r = spawn_cli({"plate", "add", in,
                        "--name", "SideCar",
                        "--output", out});
    INFO("stdout: " << r.stdout_text << "\nstderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    // Input untouched: size and mtime unchanged.
    REQUIRE(fs::file_size(in) == in_size_before);
    REQUIRE(fs::last_write_time(in) == in_mtime_before);
    REQUIRE(fs::exists(out));

    // Input must NOT have the new plate; output must.
    auto has = [](const bambu_cli::ProjectState& s, const std::string& n) {
        for (auto* p : s.plate_data) if (p && p->plate_name == n) return true;
        return false;
    };
    bambu_cli::ProjectState s_in, s_out;
    REQUIRE(bambu_cli::load_project(in,  s_in ).ok);
    REQUIRE(bambu_cli::load_project(out, s_out).ok);
    REQUIRE_FALSE(has(s_in,  "SideCar"));
    REQUIRE      (has(s_out, "SideCar"));

    fs::remove(in);
    fs::remove(out);
}
