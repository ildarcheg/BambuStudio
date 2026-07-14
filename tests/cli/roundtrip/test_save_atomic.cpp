// Roundtrip test for bambu-cli save scratch-file cleanup (cross-project port
// from OrcaSlicer tests/cli/roundtrip/test_save_atomic.cpp). Verifies the
// .bak-swap save pattern leaves no scratch files behind on a successful save.
// Bambu's scratch names are "<out>.tmp.3mf" and "<out>.bak" (see io.cpp).
// Orca's "<out>.rewrite" is Orca-only and not checked here.
#include "../test_helpers.hpp"

#include "io.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

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
