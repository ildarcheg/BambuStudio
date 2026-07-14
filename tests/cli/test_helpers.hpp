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

// M6: 4×4 matrix helpers.
// Represents a 4×4 column-major matrix (Eigen convention).
// Translation is at m[12]=tx, m[13]=ty, m[14]=tz.
struct Matrix4 { double m[16]; };

// Parse every <matrix>16 doubles</matrix> element from <xml> (space-separated,
// column-major per Eigen convention). Elements that don't yield exactly 16
// doubles are skipped. Returns matrices in document order.
std::vector<Matrix4> parse_all_matrices(const std::string& xml);

// Parse every <item transform="..."> attribute from 3D/3dmodel.model XML.
// The BBS 3MF format uses a 12-element row-major matrix:
//   col0 col1 col2 translation  (each column written as a row)
// i.e. r00 r10 r20 | r01 r11 r21 | r02 r12 r22 | tx ty tz
// Translation is at indices 9 (tx), 10 (ty), 11 (tz).
// Converts each to a Matrix4 (column-major 4x4) with m[12]=tx, m[13]=ty, m[14]=tz.
// Skips items that don't yield exactly 12 doubles.
std::vector<Matrix4> parse_item_transforms(const std::string& xml);

} // namespace bambu_cli_test
