// Catch2 main for bambu-cli tests.
// This file provides the CATCH_CONFIG_MAIN implementation; it must be compiled
// into exactly one translation unit in the cli_tests executable.
#include <catch_main.hpp>

#include <boost/filesystem.hpp>
#include <libslic3r/Utils.hpp>

// libslic3r's Model backup machinery resolves its scratch directory via
// Slic3r::get_temporary_dir(); when unset it falls back to "/bamboo_model"
// (filesystem root), which is read-only on macOS and makes every load/store
// roundtrip throw. The CLI entry point sets this at startup (src/cli/main.cpp);
// the test harness must do the same. A run-scoped listener fires once before
// any test case, matching that behaviour without touching the shared
// catch_main.hpp header.
namespace {
struct SetTemporaryDirListener : Catch::TestEventListenerBase {
    using TestEventListenerBase::TestEventListenerBase;

    void testRunStarting(Catch::TestRunInfo const&) override
    {
        Slic3r::set_temporary_dir(boost::filesystem::temp_directory_path().string());
    }
};
} // namespace

CATCH_REGISTER_LISTENER(SetTemporaryDirListener)
