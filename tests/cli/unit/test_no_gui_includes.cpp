// Guardrail: no GUI-layer coupling may creep into the CLI.
//
// bambu_cli_core deliberately links libslic3r only (no libslic3r_gui), but
// its CMakeLists exposes ${CMAKE_SOURCE_DIR}/src as an include dir — so an
// accidental #include "slic3r/..." (the GUI layer) in a CLI source would
// compile fine and only surface as link errors (or silent coupling) much
// later, typically during an upstream rebase. This test scans src/cli
// sources and fails on any GUI-layer include outside the single documented
// exception: stubs_for_libslic3r.cpp includes slic3r/Utils/Http.hpp to
// define no-op stubs against the real declarations (drift there is a
// deliberate loud compile error).
#include <catch2/catch.hpp>

#include <boost/filesystem.hpp>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace fs = boost::filesystem;

namespace {

struct GuiInclude {
    std::string file;    // path relative to src/cli
    std::string line;    // the offending include line
};

// Scan all .cpp/.hpp under src/cli (skipping extern/) for GUI-layer
// includes: #include "slic3r/..." or <slic3r/...>.
std::vector<GuiInclude> scan_gui_includes() {
    static const std::regex re("^\\s*#\\s*include\\s*[\"<]slic3r/");
    std::vector<GuiInclude> hits;
    const fs::path root(BAMBU_CLI_SRC_DIR);
    REQUIRE(fs::is_directory(root));

    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (fs::is_directory(it->path())) {
            if (it->path().filename() == "extern")
                it.no_push();   // vendored CLI11 is out of scope
            continue;
        }
        const std::string ext = it->path().extension().string();
        if (ext != ".cpp" && ext != ".hpp" && ext != ".h")
            continue;
        std::ifstream in(it->path().string());
        std::string line;
        while (std::getline(in, line)) {
            if (std::regex_search(line, re)) {
                GuiInclude g;
                g.file = fs::relative(it->path(), root).generic_string();
                g.line = line;
                hits.push_back(std::move(g));
            }
        }
    }
    return hits;
}

} // namespace

TEST_CASE("GUI-include guardrail: scanner detects the known stubs include",
          "[unit][gui_include_guard]") {
    // Self-check: if the scanner were broken (wrong dir, wrong regex), the
    // guardrail below would pass vacuously. The one documented GUI include
    // must be found.
    auto hits = scan_gui_includes();
    bool found_stubs = false;
    for (const auto& h : hits)
        if (h.file == "stubs_for_libslic3r.cpp" &&
            h.line.find("slic3r/Utils/Http.hpp") != std::string::npos)
            found_stubs = true;
    REQUIRE(found_stubs);
}

TEST_CASE("GUI-include guardrail: no GUI-layer includes outside the stubs "
          "file", "[unit][gui_include_guard]") {
    auto hits = scan_gui_includes();
    std::vector<GuiInclude> violations;
    for (const auto& h : hits)
        if (h.file != "stubs_for_libslic3r.cpp")
            violations.push_back(h);

    for (const auto& v : violations) {
        INFO("GUI-layer include in src/cli/" << v.file << ": " << v.line);
        CHECK(false);
    }
    REQUIRE(violations.empty());
}
