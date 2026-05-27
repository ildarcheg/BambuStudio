#include <catch2/catch.hpp>
#include "invariant_guard.hpp"

#include <boost/filesystem.hpp>
#include <miniz.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace fs = boost::filesystem;

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

TEST_CASE("aux passthrough: identical archives pass",
          "[unit][invariant_aux]") {
    const auto pre  = make_zip({{"Auxiliaries/Model Pictures/a.jpg", "ABC"},
                                {"Auxiliaries/Assembly Guide/m.pdf", "PDF "}});
    const auto post = make_zip({{"Auxiliaries/Model Pictures/a.jpg", "ABC"},
                                {"Auxiliaries/Assembly Guide/m.pdf", "PDF "}});
    std::string err;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(pre, post, &err));
    REQUIRE(err.empty());
    fs::remove(pre); fs::remove(post);
}

TEST_CASE("aux passthrough: missing entry in post fails",
          "[unit][invariant_aux]") {
    const auto pre  = make_zip({{"Auxiliaries/Profile Pictures/x.jpg", "XYZ"}});
    const auto post = make_zip({{"3D/3dmodel.model", "X"}});
    std::string err;
    REQUIRE_FALSE(bambu_cli::check_auxiliary_passthrough(pre, post, &err));
    REQUIRE(err.find("Profile Pictures/x.jpg") != std::string::npos);
    fs::remove(pre); fs::remove(post);
}

TEST_CASE("aux passthrough: content drift fails",
          "[unit][invariant_aux]") {
    const auto pre  = make_zip({{"Auxiliaries/Others/note.txt", "hello"}});
    const auto post = make_zip({{"Auxiliaries/Others/note.txt", "HELLO"}});
    std::string err;
    REQUIRE_FALSE(bambu_cli::check_auxiliary_passthrough(pre, post, &err));
    REQUIRE(err.find("note.txt") != std::string::npos);
    fs::remove(pre); fs::remove(post);
}

TEST_CASE("aux passthrough: non-Auxiliary entries ignored",
          "[unit][invariant_aux]") {
    const auto pre  = make_zip({{"3D/3dmodel.model", "X"},
                                {"Auxiliaries/Others/k.txt", "K"}});
    const auto post = make_zip({{"3D/3dmodel.model", "Y"},
                                {"Auxiliaries/Others/k.txt", "K"}});
    std::string err;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(pre, post, &err));
    fs::remove(pre); fs::remove(post);
}

TEST_CASE("aux passthrough: empty pre yields pass",
          "[unit][invariant_aux]") {
    const auto pre  = make_zip({{"3D/3dmodel.model", "X"}});
    const auto post = make_zip({{"3D/3dmodel.model", "X"}});
    std::string err;
    REQUIRE(bambu_cli::check_auxiliary_passthrough(pre, post, &err));
    fs::remove(pre); fs::remove(post);
}
