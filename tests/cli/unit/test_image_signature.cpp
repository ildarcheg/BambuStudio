#include <catch2/catch.hpp>
#include "project_tab_ops.hpp"

#include <boost/filesystem.hpp>
#include <fstream>
#include <vector>

namespace fs = boost::filesystem;

static std::string write_temp(const std::vector<uint8_t>& bytes,
                              const std::string& ext) {
    const fs::path p = fs::temp_directory_path() /
                       fs::unique_path("img-%%%%-%%%%" + ext);
    std::ofstream f(p.string(), std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return p.string();
}

TEST_CASE("is_png_or_jpeg: accepts PNG signature",
          "[unit][image_signature]") {
    const std::vector<uint8_t> png = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A, 0x00,0x00,0x00,0x0D
    };
    const std::string path = write_temp(png, ".png");
    REQUIRE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: accepts JPEG SOI marker (JFIF)",
          "[unit][image_signature]") {
    const std::vector<uint8_t> jpeg = {
        0xFF,0xD8,0xFF,0xE0, 0x00,0x10,'J','F','I','F'
    };
    const std::string path = write_temp(jpeg, ".jpg");
    REQUIRE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: accepts JPEG with EXIF (FF D8 FF E1)",
          "[unit][image_signature]") {
    const std::vector<uint8_t> exif = {0xFF,0xD8,0xFF,0xE1, 0x00,0x10,'E','x','i','f'};
    const std::string path = write_temp(exif, ".jpg");
    REQUIRE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: rejects GIF",
          "[unit][image_signature]") {
    const std::vector<uint8_t> gif = {'G','I','F','8','9','a', 0x01,0x00};
    const std::string path = write_temp(gif, ".gif");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: rejects truncated header",
          "[unit][image_signature]") {
    const std::vector<uint8_t> trunc = {0x89, 0x50};
    const std::string path = write_temp(trunc, ".png");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: rejects empty file",
          "[unit][image_signature]") {
    const std::string path = write_temp({}, ".png");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: rejects text file",
          "[unit][image_signature]") {
    const std::vector<uint8_t> txt(64, 'A');
    const std::string path = write_temp(txt, ".txt");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: rejects nonexistent path",
          "[unit][image_signature]") {
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(
        "C:/does/not/exist/anywhere.png"));
}

TEST_CASE("is_png_or_jpeg: zero-byte file rejected",
          "[unit][image_signature][negative]") {
    const std::string path = write_temp({}, ".bin");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: 1-byte file rejected",
          "[unit][image_signature][negative]") {
    const std::string path = write_temp({0x89}, ".bin");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: 2-byte file rejected",
          "[unit][image_signature][negative]") {
    const std::string path = write_temp({0xFF, 0xD8}, ".bin");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: 7-byte truncated PNG signature rejected",
          "[unit][image_signature][negative]") {
    const std::vector<uint8_t> trunc = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A};
    const std::string path = write_temp(trunc, ".png");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: PNG signature plus random garbage accepted (pins current behavior)",
          "[unit][image_signature][negative]") {
    // is_png_or_jpeg is a signature check only — it does not validate
    // anything beyond the first 8 (PNG) or 3 (JPEG) bytes. Document this.
    std::vector<uint8_t> bytes = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    for (int i = 0; i < 64; ++i) bytes.push_back(static_cast<uint8_t>(0xA5 ^ i));
    const std::string path = write_temp(bytes, ".png");
    REQUIRE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: JPEG SOI plus random garbage accepted (pins current behavior)",
          "[unit][image_signature][negative]") {
    std::vector<uint8_t> bytes = {0xFF,0xD8,0xFF};
    for (int i = 0; i < 64; ++i) bytes.push_back(static_cast<uint8_t>(0xA5 ^ i));
    const std::string path = write_temp(bytes, ".jpg");
    REQUIRE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: PNG signature with embedded NUL bytes accepted (pins current behavior)",
          "[unit][image_signature][negative]") {
    std::vector<uint8_t> bytes = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,
                                   0x00,0x00,0x00,0x00, 'd','a','t','a'};
    const std::string path = write_temp(bytes, ".png");
    REQUIRE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: DOS exe (MZ header) rejected",
          "[unit][image_signature][negative]") {
    const std::vector<uint8_t> mz = {'M','Z', 0x90,0x00, 0x03,0x00, 0x00,0x00};
    const std::string path = write_temp(mz, ".exe");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}

TEST_CASE("is_png_or_jpeg: PDF header (%PDF) rejected",
          "[unit][image_signature][negative]") {
    const std::vector<uint8_t> pdf = {'%','P','D','F','-','1','.','4', 0x0A};
    const std::string path = write_temp(pdf, ".pdf");
    REQUIRE_FALSE(bambu_cli::detail::is_png_or_jpeg(path));
    fs::remove(path);
}
