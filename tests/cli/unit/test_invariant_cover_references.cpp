#include <catch2/catch.hpp>
#include "invariant_guard.hpp"

#include <boost/filesystem.hpp>
#include <miniz.h>

#include <cstring>
#include <string>
#include <vector>

namespace fs = boost::filesystem;

static const std::string kModelHead =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<model xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n";
static const std::string kModelTail =
    " <resources/>\n <build/>\n</model>\n";

static std::string model_with(const std::string& designer_cover,
                              const std::string& profile_cover) {
    std::string s = kModelHead;
    if (!designer_cover.empty())
        s += " <metadata name=\"DesignerCover\">" + designer_cover + "</metadata>\n";
    if (!profile_cover.empty())
        s += " <metadata name=\"ProfileCover\">" + profile_cover + "</metadata>\n";
    s += kModelTail;
    return s;
}

static std::string make_zip(const std::vector<std::pair<std::string, std::string>>& entries) {
    const fs::path p = fs::temp_directory_path() /
                       fs::unique_path("covref-%%%%-%%%%.zip");
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    REQUIRE(mz_zip_writer_init_file(&zip, p.string().c_str(), 0));
    for (const auto& e : entries)
        REQUIRE(mz_zip_writer_add_mem(&zip, e.first.c_str(),
                                      e.second.data(), e.second.size(),
                                      MZ_DEFAULT_COMPRESSION));
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return p.string();
}

TEST_CASE("cover refs: empty metadata passes", "[unit][invariant_covref]") {
    const auto z = make_zip({{"3D/3dmodel.model", model_with("", "")}});
    std::string err;
    REQUIRE(bambu_cli::check_cover_references_resolve(z, &err));
    fs::remove(z);
}

TEST_CASE("cover refs: both covers present in canonical folders passes",
          "[unit][invariant_covref]") {
    const auto z = make_zip({
        {"3D/3dmodel.model", model_with("a.jpg", "b.jpg")},
        {"Auxiliaries/Model Pictures/a.jpg",   "AAA"},
        {"Auxiliaries/Profile Pictures/b.jpg", "BBB"},
    });
    std::string err;
    REQUIRE(bambu_cli::check_cover_references_resolve(z, &err));
    fs::remove(z);
}

TEST_CASE("cover refs: DesignerCover references absent file fails",
          "[unit][invariant_covref]") {
    const auto z = make_zip({
        {"3D/3dmodel.model", model_with("missing.png", "")},
    });
    std::string err;
    REQUIRE_FALSE(bambu_cli::check_cover_references_resolve(z, &err));
    REQUIRE(err.find("DesignerCover") != std::string::npos);
    REQUIRE(err.find("missing.png")   != std::string::npos);
    fs::remove(z);
}

TEST_CASE("cover refs: ProfileCover references absent file fails",
          "[unit][invariant_covref]") {
    const auto z = make_zip({
        {"3D/3dmodel.model", model_with("", "missing.jpg")},
    });
    std::string err;
    REQUIRE_FALSE(bambu_cli::check_cover_references_resolve(z, &err));
    REQUIRE(err.find("ProfileCover") != std::string::npos);
    fs::remove(z);
}
