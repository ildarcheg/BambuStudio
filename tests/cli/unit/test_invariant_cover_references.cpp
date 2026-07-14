#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
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

TEST_CASE("cover refs: known-good archive minus DesignerCover file fails",
          "[unit][invariant_covref]") {
    // Known-good shape: archive with both metadata pointers + both files.
    const auto z = make_zip({
        {"3D/3dmodel.model", model_with("designer.png", "profile.jpg")},
        {"Auxiliaries/Model Pictures/designer.png",   "PNGDATA"},
        {"Auxiliaries/Profile Pictures/profile.jpg",  "JPGDATA"},
    });
    std::string err0;
    REQUIRE(bambu_cli::check_cover_references_resolve(z, &err0));
    REQUIRE(err0.empty());

    bambu_cli_unit::mutate_archive_remove_entry(
        z, "Auxiliaries/Model Pictures/designer.png");

    std::string err;
    REQUIRE_FALSE(bambu_cli::check_cover_references_resolve(z, &err));
    REQUIRE(err.find("DesignerCover") != std::string::npos);
    REQUIRE(err.find("designer.png")   != std::string::npos);
    fs::remove(z);
}

TEST_CASE("cover refs: known-good archive minus ProfileCover file fails",
          "[unit][invariant_covref]") {
    const auto z = make_zip({
        {"3D/3dmodel.model", model_with("designer.png", "profile.jpg")},
        {"Auxiliaries/Model Pictures/designer.png",   "PNGDATA"},
        {"Auxiliaries/Profile Pictures/profile.jpg",  "JPGDATA"},
    });
    bambu_cli_unit::mutate_archive_remove_entry(
        z, "Auxiliaries/Profile Pictures/profile.jpg");

    std::string err;
    REQUIRE_FALSE(bambu_cli::check_cover_references_resolve(z, &err));
    REQUIRE(err.find("ProfileCover") != std::string::npos);
    REQUIRE(err.find("profile.jpg")  != std::string::npos);
    fs::remove(z);
}

TEST_CASE("cover refs: ProfileCover injected into model XML with no file fails",
          "[unit][invariant_covref]") {
    // Start from a known-good archive with only DesignerCover, then inject
    // a ProfileCover metadata pointer via a wholesale model XML rewrite.
    // (No targeted helper for in-archive XML mutation — it's a one-line
    // replacement of the model bytes, which mutate_archive_remove_entry +
    // a fresh add via make_zip would also achieve. We use the simpler
    // path: rebuild the zip with the desired shape.)
    const auto z = make_zip({
        {"3D/3dmodel.model", model_with("d.png", "dangling.jpg")},
        {"Auxiliaries/Model Pictures/d.png", "OK"},
        // Auxiliaries/Profile Pictures/dangling.jpg deliberately absent.
    });
    std::string err;
    REQUIRE_FALSE(bambu_cli::check_cover_references_resolve(z, &err));
    REQUIRE(err.find("ProfileCover") != std::string::npos);
    REQUIRE(err.find("dangling.jpg") != std::string::npos);
    fs::remove(z);
}
