#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_tab_ops.hpp"
#include "io.hpp"

#include "libslic3r/Model.hpp"

#include <boost/filesystem.hpp>
#include <fstream>

namespace fs = boost::filesystem;
using bambu_cli::ProjectState;

static const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";
static const std::string kJpg = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.jpg";

TEST_CASE("cover decoupling: designer cover lands in Model Pictures, profile in Profile Pictures",
          "[unit][cover_decouple]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = kPng;
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));

    bambu_cli::ProfileSetParams pp;
    pp.cover_path = kJpg;
    REQUIRE_NOTHROW(bambu_cli::profile_set(s, pp));

    REQUIRE(s.model.model_info);
    REQUIRE(s.model.profile_info);
    REQUIRE(s.model.model_info->cover_file == fs::path(kPng).filename().string());
    REQUIRE(s.model.profile_info->ProfileCover == fs::path(kJpg).filename().string());

    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    REQUIRE(fs::exists(aux / "Model Pictures"   / fs::path(kPng).filename()));
    REQUIRE(fs::exists(aux / "Profile Pictures" / fs::path(kJpg).filename()));

    REQUIRE_FALSE(fs::exists(aux / "Model Pictures"   / "cover.png"));
    REQUIRE_FALSE(fs::exists(aux / "Profile Pictures" / "cover.png"));
}

TEST_CASE("cover decoupling: info clear cover leaves profile cover intact",
          "[unit][cover_decouple]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;     ip.cover_path = kPng;
    bambu_cli::ProfileSetParams pp;  pp.cover_path = kJpg;
    bambu_cli::info_set(s, ip);
    bambu_cli::profile_set(s, pp);

    bambu_cli::info_clear(s, {"cover"});

    REQUIRE(s.model.model_info->cover_file.empty());
    REQUIRE(s.model.profile_info->ProfileCover == fs::path(kJpg).filename().string());

    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    REQUIRE(fs::exists(aux / "Profile Pictures" / fs::path(kJpg).filename()));
}

TEST_CASE("cover decoupling: profile clear cover leaves designer cover intact",
          "[unit][cover_decouple]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;     ip.cover_path = kPng;
    bambu_cli::ProfileSetParams pp;  pp.cover_path = kJpg;
    bambu_cli::info_set(s, ip);
    bambu_cli::profile_set(s, pp);

    bambu_cli::profile_clear(s, {"cover"});

    REQUIRE(s.model.profile_info->ProfileCover.empty());
    REQUIRE(s.model.model_info->cover_file == fs::path(kPng).filename().string());

    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    REQUIRE(fs::exists(aux / "Model Pictures" / fs::path(kPng).filename()));
}

TEST_CASE("cover decoupling: JPEG cover accepted (was PNG-only)",
          "[unit][cover_decouple]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = kJpg;
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));
    REQUIRE(s.model.model_info->cover_file == fs::path(kJpg).filename().string());
}

TEST_CASE("info_set negative: non-existent cover path throws BadCoverImage",
          "[unit][cover_decouple][negative]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = "C:/does/not/exist/anywhere/cover.png";
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), bambu_cli::BadCoverImage);
}

TEST_CASE("info_set negative: zero-byte cover throws BadCoverImage",
          "[unit][cover_decouple][negative]") {
    const fs::path empty = fs::temp_directory_path() /
                           fs::unique_path("empty-%%%%-%%%%.png");
    { std::ofstream f(empty.string(), std::ios::binary); } // touch empty

    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = empty.string();
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), bambu_cli::BadCoverImage);

    fs::remove(empty);
}

TEST_CASE("info_set negative: cover path is a directory throws BadCoverImage",
          "[unit][cover_decouple][negative]") {
    const fs::path dir = fs::temp_directory_path() /
                         fs::unique_path("notafile-%%%%-%%%%");
    fs::create_directory(dir);

    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = dir.string();
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), bambu_cli::BadCoverImage);

    fs::remove(dir);
}

TEST_CASE("info_set negative: GIF file (wrong signature) throws BadCoverImage",
          "[unit][cover_decouple][negative]") {
    const fs::path gif = fs::temp_directory_path() /
                         fs::unique_path("bad-%%%%-%%%%.gif");
    {
        std::ofstream f(gif.string(), std::ios::binary);
        const char header[] = "GIF89a";
        f.write(header, 6);
    }

    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = gif.string();
    REQUIRE_THROWS_AS(bambu_cli::info_set(s, ip), bambu_cli::BadCoverImage);

    fs::remove(gif);
}

TEST_CASE("info_set baseline: tiny known-good PNG succeeds and matches byte-for-byte",
          "[unit][cover_decouple][negative]") {
    // Smallest practical PNG: 8-byte signature + IHDR + IEND. Any extra bytes
    // past the signature are fine — is_png_or_jpeg is signature-only.
    const std::vector<uint8_t> png = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,
        0x00,0x00,0x00,0x0D,
        'I','H','D','R',
        0x00,0x00,0x00,0x01, 0x00,0x00,0x00,0x01,
        0x08,0x06, 0x00,0x00,0x00,
        0x1F,0x15,0xC4,0x89,
        0x00,0x00,0x00,0x00,
        'I','E','N','D',
        0xAE,0x42,0x60,0x82
    };
    const fs::path png_path = fs::temp_directory_path() /
                              fs::unique_path("tiny-%%%%-%%%%.png");
    {
        std::ofstream f(png_path.string(), std::ios::binary);
        f.write(reinterpret_cast<const char*>(png.data()),
                static_cast<std::streamsize>(png.size()));
    }

    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::InfoSetParams ip;
    ip.cover_path = png_path.string();
    REQUIRE_NOTHROW(bambu_cli::info_set(s, ip));

    REQUIRE(s.model.model_info);
    REQUIRE(s.model.model_info->cover_file == png_path.filename().string());

    const fs::path aux = s.model.get_auxiliary_file_temp_path();
    const fs::path landed = aux / "Model Pictures" / png_path.filename();
    REQUIRE(fs::exists(landed));
    REQUIRE(fs::file_size(landed) == png.size());

    fs::remove(png_path);
}
