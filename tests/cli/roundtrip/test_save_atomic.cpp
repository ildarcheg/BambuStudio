// Roundtrip test for bambu-cli save scratch-file cleanup (cross-project port
// from OrcaSlicer tests/cli/roundtrip/test_save_atomic.cpp). Verifies the
// .bak-swap save pattern leaves no scratch files behind on a successful save.
// Bambu's scratch names are "<out>.tmp.3mf" and "<out>.bak" (see io.cpp).
// Orca's "<out>.rewrite" is Orca-only and not checked here.
#include "../test_helpers.hpp"

#include "io.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <fstream>

namespace fs = boost::filesystem;
using namespace bambu_cli_test;

TEST_CASE("save_project leaves destination present and cleans up scratch files",
          "[bambu-cli][roundtrip][save_atomic]") {
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));

    const std::string dst = fresh_temp_path("_atomic.3mf");
    fs::copy_file(ref, dst, fs::copy_options::overwrite_existing);

    bambu_cli::ProjectState state;
    REQUIRE(bambu_cli::load_project(dst, state).ok);
    REQUIRE(bambu_cli::save_project(state, dst).ok);

    REQUIRE(fs::exists(dst));
    REQUIRE_FALSE(fs::exists(dst + ".tmp.3mf"));
    REQUIRE_FALSE(fs::exists(dst + ".bak"));

    fs::remove(dst);
}

TEST_CASE("save_project: stale tmp path occupied by a non-empty directory -> "
          "IoResult error, no throw",
          "[bambu-cli][roundtrip][save_atomic]") {
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const std::string dst = fresh_temp_path("_staletmp.3mf");
    fs::copy_file(ref, dst, fs::copy_options::overwrite_existing);

    // Occupy the scratch path with a non-empty directory: fs::remove()
    // cannot clear it, so the save must fail as a reported IoResult —
    // never as an escaping boost::filesystem exception (which would hit
    // std::terminate in production; main has no handler for it).
    const std::string tmp = dst + ".tmp.3mf";
    fs::create_directory(tmp);
    { std::ofstream f(tmp + "/occupant.txt"); f << "x"; }

    bambu_cli::ProjectState state;
    REQUIRE(bambu_cli::load_project(dst, state).ok);

    bambu_cli::IoResult r;
    REQUIRE_NOTHROW(r = bambu_cli::save_project(state, dst));
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.exit_code ==
            bambu_cli::to_int(bambu_cli::ExitCode::invalid_state));
    // Original untouched by the failed save.
    REQUIRE(fs::exists(dst));

    fs::remove_all(tmp);
    fs::remove(dst);
}

TEST_CASE("atomic_copy: stale tmp path occupied by a non-empty directory -> "
          "IoResult error, no throw",
          "[bambu-cli][roundtrip][save_atomic]") {
    const std::string ref = canonical_committed_3mf();
    REQUIRE(fs::exists(ref));
    const std::string dst = fresh_temp_path("_copystale.3mf");
    const std::string tmp = dst + ".tmp.3mf";
    fs::create_directory(tmp);
    { std::ofstream f(tmp + "/occupant.txt"); f << "x"; }

    bambu_cli::IoResult r;
    REQUIRE_NOTHROW(r = bambu_cli::atomic_copy(ref, dst));
    REQUIRE_FALSE(r.ok);

    fs::remove_all(tmp);
    boost::system::error_code ignore;
    fs::remove(dst, ignore);
}
