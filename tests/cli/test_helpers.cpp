#include "test_helpers.hpp"

#include <boost/process.hpp>
#include <boost/filesystem.hpp>

#include "libslic3r/miniz_extension.hpp"
#include <miniz.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

namespace bp = boost::process;
namespace fs = boost::filesystem;

namespace bambu_cli_test {

CliResult spawn_cli(const std::vector<std::string>& args) {
    CliResult r;
    bp::ipstream out, err;
    bp::child c(BAMBU_CLI_EXE, args, bp::std_out > out, bp::std_err > err);
    // Drain both pipes concurrently. A sequential drain (stdout to EOF,
    // then stderr) deadlocks when the child fills the stderr pipe while
    // stdout is still open: the child blocks in write, this process
    // blocks in read, and neither can make progress.
    std::stringstream so, se;
    std::thread err_drain([&] { se << err.rdbuf(); });
    so << out.rdbuf();
    err_drain.join();
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

namespace {
std::mutex& temp_registry_mutex() {
    static std::mutex m;
    return m;
}
std::vector<std::string>& temp_registry() {
    static std::vector<std::string> paths;
    return paths;
}
} // namespace

void cleanup_recorded_temp_paths() {
    std::vector<std::string> paths;
    {
        std::lock_guard<std::mutex> lock(temp_registry_mutex());
        paths.swap(temp_registry());
    }
    for (const auto& p : paths) {
        boost::system::error_code ec;
        fs::remove_all(p, ec);   // best effort; files still open stay put
    }
}

std::string fresh_temp_path(const std::string& suffix) {
    static std::atomic<int> counter{0};
    auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream oss;
    oss << (fs::temp_directory_path().string()) << "/bambu_cli_test_"
        << t << "_" << counter.fetch_add(1) << suffix;
    std::string path = oss.str();
    static std::once_flag atexit_once;
    std::call_once(atexit_once, [] {
        std::atexit(cleanup_recorded_temp_paths);
    });
    {
        std::lock_guard<std::mutex> lock(temp_registry_mutex());
        temp_registry().push_back(path);
    }
    return path;
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

std::vector<Matrix4> parse_all_matrices(const std::string& xml) {
    std::vector<Matrix4> out;
    static const std::regex re(R"(<matrix>([^<]+)</matrix>)");
    auto begin = std::sregex_iterator(xml.begin(), xml.end(), re);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        std::string body = (*it)[1].str();
        Matrix4 mat{};
        std::istringstream iss(body);
        int i = 0;
        double d;
        while (i < 16 && (iss >> d)) mat.m[i++] = d;
        if (i == 16) out.push_back(mat);
    }
    return out;
}

std::vector<Matrix4> parse_item_transforms(const std::string& xml) {
    std::vector<Matrix4> out;
    // Match transform="..." attribute on <item ...> elements.
    // Use a named delimiter to avoid )" inside the raw string terminating early.
    static const std::regex re(R"x(<item\s[^>]*transform="([^"]+)")x");
    auto begin = std::sregex_iterator(xml.begin(), xml.end(), re);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        std::string body = (*it)[1].str();
        // Parse 12 doubles: row-major columns: c0 c1 c2 c3
        // c0=(r00,r10,r20) c1=(r01,r11,r21) c2=(r02,r12,r22) c3=(tx,ty,tz)
        std::vector<double> v;
        std::istringstream iss(body);
        double d;
        while (iss >> d) v.push_back(d);
        if (v.size() != 12) continue;
        // Convert to column-major 4x4 Matrix4.
        // Column-major: m[col*4 + row]
        // v[0..2] = column 0 (m[0..2])
        // v[3..5] = column 1 (m[4..6])
        // v[6..8] = column 2 (m[8..10])
        // v[9..11] = column 3 = translation (m[12..14])
        Matrix4 mat{};
        mat.m[0] = v[0]; mat.m[1] = v[1]; mat.m[2] = v[2]; mat.m[3] = 0;
        mat.m[4] = v[3]; mat.m[5] = v[4]; mat.m[6] = v[5]; mat.m[7] = 0;
        mat.m[8] = v[6]; mat.m[9] = v[7]; mat.m[10] = v[8]; mat.m[11] = 0;
        mat.m[12] = v[9]; mat.m[13] = v[10]; mat.m[14] = v[11]; mat.m[15] = 1;
        out.push_back(mat);
    }
    return out;
}

} // namespace bambu_cli_test
