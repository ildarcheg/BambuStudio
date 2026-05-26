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
