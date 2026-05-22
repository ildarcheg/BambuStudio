#include "png_placeholder.hpp"

#include <miniz.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace bambu_cli {

static void push_be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v      ) & 0xFF));
}

static void write_chunk(std::vector<uint8_t>& out, const char type[4],
                        const uint8_t* data, uint32_t len) {
    push_be32(out, len);
    out.insert(out.end(), type, type + 4);
    if (len > 0 && data != nullptr)
        out.insert(out.end(), data, data + len);
    mz_ulong crc = mz_crc32(0, reinterpret_cast<const mz_uint8*>(type), 4);
    if (len > 0 && data != nullptr)
        crc = mz_crc32(crc, data, len);
    push_be32(out, static_cast<uint32_t>(crc));
}

std::vector<uint8_t> make_placeholder_png_128() {
    std::vector<uint8_t> out;
    out.reserve(2048);

    // PNG signature
    static const uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    out.insert(out.end(), sig, sig + 8);

    // IHDR: 128x128 RGBA (color_type=6), bit_depth=8, no compression/filter/interlace
    {
        uint8_t ihdr[13] = {};
        ihdr[3] = 128;  // width  = 0x00000080
        ihdr[7] = 128;  // height = 0x00000080
        ihdr[8] = 8;    // bit depth
        ihdr[9] = 6;    // color type = RGBA
        // compression=0, filter=0, interlace=0 already zeroed
        write_chunk(out, "IHDR", ihdr, 13);
    }

    // IDAT: 128 rows × (1 filter byte 0x00 + 128 × 4 RGBA bytes 0xC0)
    // Deflate-wrapped (zlib format) with MZ_NO_COMPRESSION (stored blocks).
    {
        const size_t row_bytes = 1 + 128 * 4;   // 513 bytes per row
        const size_t raw_size  = 128 * row_bytes; // 65664 bytes total
        std::vector<uint8_t> raw(raw_size, 0xC0);
        for (size_t r = 0; r < 128; ++r)
            raw[r * row_bytes] = 0x00;  // filter byte = None

        mz_ulong comp_bound = mz_compressBound(static_cast<mz_ulong>(raw_size));
        std::vector<uint8_t> comp(comp_bound);
        mz_ulong comp_len = comp_bound;
        int rc = mz_compress2(comp.data(), &comp_len,
                              raw.data(), static_cast<mz_ulong>(raw_size),
                              MZ_NO_COMPRESSION);
        if (rc != MZ_OK)
            throw std::runtime_error("mz_compress2 failed in make_placeholder_png_128");
        comp.resize(comp_len);

        write_chunk(out, "IDAT", comp.data(), static_cast<uint32_t>(comp_len));
    }

    // IEND: no data
    write_chunk(out, "IEND", nullptr, 0);

    return out;
}

} // namespace bambu_cli
