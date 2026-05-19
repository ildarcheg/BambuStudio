#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace bambu_cli_test {

struct CliResult {
    int         exit_code = 0;
    std::string stdout_text;
    std::string stderr_text;
};

// Spawn the bambu-cli binary at BAMBU_CLI_EXE with the given args. Capture
// stdout/stderr and the exit code. Blocks until the process exits.
CliResult spawn_cli(const std::vector<std::string>& args);

// Read a single entry from a zip archive into a byte buffer. Returns empty
// vector if entry is absent.
std::vector<uint8_t> read_zip_entry(const std::string& zip_path, const std::string& entry_name);

// Return all entry names in a zip archive, in archive order.
std::vector<std::string> list_zip_entries(const std::string& zip_path);

// Path helpers
std::string fresh_temp_path(const std::string& suffix);   // unique path in $TEMP
std::string canonical_committed_3mf();                    // BAMBU_CLI_FIXTURE_3MF
std::string canonical_committed_stl_dir();                // BAMBU_CLI_FIXTURE_STL_DIR
std::string local_reference_3mf_or_skip();                // BAMBU_CLI_LOCAL_REFERENCE_3MF; "" if path absent
std::string local_stl_dir_or_skip();                      // BAMBU_CLI_LOCAL_STL_DIR; "" if dir absent

} // namespace bambu_cli_test
