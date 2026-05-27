#include <catch2/catch.hpp>
#include "invariant_guard.hpp"

#include <boost/filesystem.hpp>
#include <miniz.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace fs = boost::filesystem;

// Build a tiny zip with the listed entries (archive-path, content-bytes).
static std::string make_zip(const std::vector<std::pair<std::string, std::string>>& entries) {
    const fs::path p = fs::temp_directory_path() /
                       fs::unique_path("auxpass-%%%%-%%%%.zip");
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    REQUIRE(mz_zip_writer_init_file(&zip, p.string().c_str(), 0));
    for (const auto& e : entries) {
        REQUIRE(mz_zip_writer_add_mem(&zip, e.first.c_str(),
                                      e.second.data(), e.second.size(),
                                      MZ_DEFAULT_COMPRESSION));
    }
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return p.string();
}

// Materialize a directory tree from (relative-path, contents) pairs.
// Returns the root path (suitable for passing as aux_temp_dir).
static std::string make_dir(const std::vector<std::pair<std::string, std::string>>& files) {
    const fs::path root = fs::temp_directory_path() /
                          fs::unique_path("auxpass-dir-%%%%-%%%%");
    fs::create_directories(root);
    for (const auto& f : files) {
        const fs::path full = root / f.first;
        fs::create_directories(full.parent_path());
        std::ofstream out(full.string(), std::ios::binary);
        out.write(f.second.data(), static_cast<std::streamsize>(f.second.size()));
    }
    return root.string();
}

TEST_CASE("aux passthrough: temp dir matches archive (Auxiliaries prefix added)",
          "[unit][invariant_aux]") {
    const auto dir  = make_dir({{"Model Pictures/a.jpg", "ABC"},
                                {"Assembly Guide/m.pdf", "PDF "}});
    const auto post = make_zip({{"Auxiliaries/Model Pictures/a.jpg", "ABC"},
                                {"Auxiliaries/Assembly Guide/m.pdf", "PDF "}});
    std::string err;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(dir, post, &err));
    REQUIRE(err.empty());
    fs::remove_all(dir); fs::remove(post);
}

TEST_CASE("aux passthrough: file in temp dir missing from archive fails",
          "[unit][invariant_aux]") {
    const auto dir  = make_dir({{"Profile Pictures/x.jpg", "XYZ"}});
    const auto post = make_zip({{"3D/3dmodel.model", "X"}});
    std::string err;
    REQUIRE_FALSE(bambu_cli::check_auxiliary_passthrough(dir, post, &err));
    REQUIRE(err.find("Profile Pictures/x.jpg") != std::string::npos);
    fs::remove_all(dir); fs::remove(post);
}

TEST_CASE("aux passthrough: content drift between disk and archive fails",
          "[unit][invariant_aux]") {
    const auto dir  = make_dir({{"Others/note.txt", "hello"}});
    const auto post = make_zip({{"Auxiliaries/Others/note.txt", "HELLO"}});
    std::string err;
    REQUIRE_FALSE(bambu_cli::check_auxiliary_passthrough(dir, post, &err));
    REQUIRE(err.find("note.txt") != std::string::npos);
    fs::remove_all(dir); fs::remove(post);
}

TEST_CASE("aux passthrough: extra archive entries are ignored",
          "[unit][invariant_aux]") {
    // Temp dir has one file; archive has the same plus extras. Pass: we
    // only verify temp-dir contents survived into the archive, not the
    // converse. (Extras typically appear because Bambu's save also writes
    // .thumbnails/ from a separate code path.)
    const auto dir  = make_dir({{"Others/k.txt", "K"}});
    const auto post = make_zip({{"3D/3dmodel.model", "X"},
                                {"Auxiliaries/Others/k.txt", "K"},
                                {"Auxiliaries/.thumbnails/thumbnail_3mf.png", "P"}});
    std::string err;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(dir, post, &err));
    fs::remove_all(dir); fs::remove(post);
}

TEST_CASE("aux passthrough: nonexistent or empty temp dir passes vacuously",
          "[unit][invariant_aux]") {
    const auto post = make_zip({{"3D/3dmodel.model", "X"}});

    // Nonexistent path.
    std::string err1;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(
        "C:/does/not/exist/anywhere", post, &err1));

    // Empty path.
    std::string err2;
    REQUIRE(bambu_cli::check_auxiliary_passthrough("", post, &err2));

    // Empty directory.
    const fs::path empty_dir = fs::temp_directory_path() /
                               fs::unique_path("auxpass-empty-%%%%");
    fs::create_directories(empty_dir);
    std::string err3;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(empty_dir.string(), post, &err3));

    fs::remove_all(empty_dir);
    fs::remove(post);
}

TEST_CASE("aux passthrough: nested subdirs in temp dir map to nested archive paths",
          "[unit][invariant_aux]") {
    // Aux temp dir contains deeper subdirs (e.g. .thumbnails/ inside the
    // root). Verify the recursive walk + path mapping handles it.
    const auto dir  = make_dir({{".thumbnails/thumbnail_3mf.png", "PNG1"},
                                {"Model Pictures/sub/nested.jpg", "NEST"}});
    const auto post = make_zip({{"Auxiliaries/.thumbnails/thumbnail_3mf.png", "PNG1"},
                                {"Auxiliaries/Model Pictures/sub/nested.jpg", "NEST"}});
    std::string err;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(dir, post, &err));
    fs::remove_all(dir); fs::remove(post);
}
