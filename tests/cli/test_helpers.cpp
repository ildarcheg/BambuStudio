#include "test_helpers.hpp"

#include <boost/process.hpp>
#include <boost/filesystem.hpp>

#include "libslic3r/miniz_extension.hpp"
#include <miniz.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <sstream>

namespace bp = boost::process;
namespace fs = boost::filesystem;

namespace bambu_cli_test {

CliResult spawn_cli(const std::vector<std::string>& args) {
    CliResult r;
    bp::ipstream out, err;
    bp::child c(BAMBU_CLI_EXE, args, bp::std_out > out, bp::std_err > err);
    std::stringstream so, se;
    so << out.rdbuf();
    se << err.rdbuf();
    c.wait();
    r.exit_code   = c.exit_code();
    r.stdout_text = so.str();
    r.stderr_text = se.str();
    return r;
}

std::vector<uint8_t> read_zip_entry(const std::string& zip_path, const std::string& entry_name) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!Slic3r::open_zip_reader(&zip, zip_path)) return {};
    int idx = mz_zip_reader_locate_file(&zip, entry_name.c_str(), nullptr, 0);
    if (idx < 0) { Slic3r::close_zip_reader(&zip); return {}; }
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, idx, &st)) { Slic3r::close_zip_reader(&zip); return {}; }
    std::vector<uint8_t> buf(static_cast<size_t>(st.m_uncomp_size));
    if (!mz_zip_reader_extract_to_mem(&zip, idx, buf.data(), buf.size(), 0)) {
        Slic3r::close_zip_reader(&zip);
        return {};
    }
    Slic3r::close_zip_reader(&zip);
    return buf;
}

std::vector<std::string> list_zip_entries(const std::string& zip_path) {
    std::vector<std::string> out;
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!Slic3r::open_zip_reader(&zip, zip_path)) return out;
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        char name[1024];
        mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        out.emplace_back(name);
    }
    Slic3r::close_zip_reader(&zip);
    return out;
}

std::string fresh_temp_path(const std::string& suffix) {
    static std::atomic<int> counter{0};
    auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream oss;
    oss << (fs::temp_directory_path().string()) << "/bambu_cli_test_"
        << t << "_" << counter.fetch_add(1) << suffix;
    return oss.str();
}

std::string canonical_committed_3mf() { return BAMBU_CLI_FIXTURE_3MF; }
std::string canonical_committed_stl_dir() { return BAMBU_CLI_FIXTURE_STL_DIR; }

std::string local_reference_3mf_or_skip() {
    fs::path p = BAMBU_CLI_LOCAL_REFERENCE_3MF;
    if (p.empty() || !fs::exists(p)) return {};
    return p.string();
}
std::string local_stl_dir_or_skip() {
    fs::path p = BAMBU_CLI_LOCAL_STL_DIR;
    if (p.empty() || !fs::is_directory(p)) return {};
    return p.string();
}

} // namespace bambu_cli_test
