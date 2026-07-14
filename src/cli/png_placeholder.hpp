#pragma once

#include <cstdint>
#include <vector>

namespace bambu_cli {

// Returns a complete 128x128 RGBA 0xC0 PNG file as a byte buffer.
// Chunks: IHDR + IDAT (MZ_NO_COMPRESSION deflate) + IEND.
std::vector<uint8_t> make_placeholder_png_128();

} // namespace bambu_cli
