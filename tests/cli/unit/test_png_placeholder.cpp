#include <catch2/catch.hpp>
#include "png_placeholder.hpp"

#include <miniz.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace bambu_cli;

namespace {

static uint32_t read_be32(const std::vector<uint8_t>& b, size_t off) {
    return (uint32_t(b[off]) << 24) | (uint32_t(b[off+1]) << 16)
         | (uint32_t(b[off+2]) <<  8) |  uint32_t(b[off+3]);
}

struct PngChunk {
    std::string          type;
    std::vector<uint8_t> data;
    uint32_t             stored_crc = 0;
};

static std::vector<PngChunk> parse_chunks(const std::vector<uint8_t>& png) {
    REQUIRE(png.size() >= 8);
    std::vector<PngChunk> out;
    size_t pos = 8;
    while (pos + 12 <= png.size()) {
        uint32_t len = read_be32(png, pos);
        REQUIRE(pos + 12 + len <= png.size());
        PngChunk c;
        c.type.assign(reinterpret_cast<const char*>(&png[pos + 4]), 4);
        c.data.assign(png.begin() + (long)pos + 8,
                      png.begin() + (long)pos + 8 + (long)len);
        c.stored_crc = read_be32(png, pos + 8 + len);
        out.push_back(std::move(c));
        pos += 12 + len;
    }
    return out;
}

static uint32_t chunk_crc(const PngChunk& c) {
    mz_ulong crc = mz_crc32(0,
        reinterpret_cast<const mz_uint8*>(c.type.data()), 4);
    if (!c.data.empty())
        crc = mz_crc32(crc, c.data.data(), c.data.size());
    return static_cast<uint32_t>(crc);
}

} // anonymous namespace

TEST_CASE("make_placeholder_png_128: PNG signature", "[unit][png_placeholder]") {
    auto png = make_placeholder_png_128();
    REQUIRE(png.size() >= 8);
    static const uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i)
        CHECK(png[i] == sig[i]);
}

TEST_CASE("make_placeholder_png_128: IHDR fields", "[unit][png_placeholder]") {
    auto chunks = parse_chunks(make_placeholder_png_128());
    REQUIRE(chunks.size() >= 1);
    REQUIRE(chunks[0].type == "IHDR");
    REQUIRE(chunks[0].data.size() == 13);
    CHECK(read_be32(chunks[0].data, 0) == 128u); // width
    CHECK(read_be32(chunks[0].data, 4) == 128u); // height
    CHECK(chunks[0].data[8]  == 8);  // bit depth
    CHECK(chunks[0].data[9]  == 6);  // color type = RGBA
    CHECK(chunks[0].data[10] == 0);  // compression method
    CHECK(chunks[0].data[11] == 0);  // filter method
    CHECK(chunks[0].data[12] == 0);  // interlace method
}

TEST_CASE("make_placeholder_png_128: IDAT decompresses to 128x513 bytes",
          "[unit][png_placeholder]") {
    auto chunks = parse_chunks(make_placeholder_png_128());
    auto it = std::find_if(chunks.begin(), chunks.end(),
                           [](const PngChunk& c){ return c.type == "IDAT"; });
    REQUIRE(it != chunks.end());

    mz_ulong expected = 128 * 513;
    mz_ulong sz = expected + 100;
    std::vector<uint8_t> uncomp(sz);
    int rc = mz_uncompress(uncomp.data(), &sz,
                           it->data.data(), (mz_ulong)it->data.size());
    CHECK(rc == MZ_OK);
    CHECK(sz == expected);
}

TEST_CASE("make_placeholder_png_128: every pixel is 0xC0", "[unit][png_placeholder]") {
    auto chunks = parse_chunks(make_placeholder_png_128());
    auto it = std::find_if(chunks.begin(), chunks.end(),
                           [](const PngChunk& c){ return c.type == "IDAT"; });
    REQUIRE(it != chunks.end());

    mz_ulong sz = 128 * 513 + 100;
    std::vector<uint8_t> uncomp(sz);
    REQUIRE(mz_uncompress(uncomp.data(), &sz,
                          it->data.data(), (mz_ulong)it->data.size()) == MZ_OK);
    REQUIRE(sz == 128u * 513u);

    size_t bad_filter = 0, bad_pixel = 0;
    for (size_t r = 0; r < 128; ++r) {
        if (uncomp[r * 513] != 0x00) ++bad_filter;
        for (size_t col = 0; col < 128 * 4; ++col)
            if (uncomp[r * 513 + 1 + col] != 0xC0) ++bad_pixel;
    }
    CHECK(bad_filter == 0);
    CHECK(bad_pixel  == 0);
}

TEST_CASE("make_placeholder_png_128: ends with IEND", "[unit][png_placeholder]") {
    auto chunks = parse_chunks(make_placeholder_png_128());
    REQUIRE(!chunks.empty());
    CHECK(chunks.back().type == "IEND");
    CHECK(chunks.back().data.empty());
}

TEST_CASE("make_placeholder_png_128: all chunk CRCs validate", "[unit][png_placeholder]") {
    auto chunks = parse_chunks(make_placeholder_png_128());
    for (const auto& c : chunks) {
        INFO("chunk: " << c.type);
        CHECK(chunk_crc(c) == c.stored_crc);
    }
    // IEND CRC over type bytes "IEND" is a well-known constant
    auto iend = std::find_if(chunks.begin(), chunks.end(),
                             [](const PngChunk& c){ return c.type == "IEND"; });
    REQUIRE(iend != chunks.end());
    CHECK(iend->stored_crc == 0xAE426082u);
}
