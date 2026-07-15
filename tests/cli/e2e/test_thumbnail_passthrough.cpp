#include "test_helpers.hpp"
#include "archive_invariants.hpp"

#include "libslic3r/PNGReadWrite.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

static const uint8_t PNG_SIG[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

static bool is_valid_png(const std::vector<uint8_t>& data) {
    if (data.size() < 8) return false;
    for (int i = 0; i < 8; ++i)
        if (data[i] != PNG_SIG[i]) return false;
    return true;
}

// Decode a PNG byte blob. Test-fails on undecodable input.
static Slic3r::png::ImageColorscale decode_or_fail(const std::vector<uint8_t>& data) {
    Slic3r::png::ImageColorscale img;
    Slic3r::png::ReadBuf rb{data.data(), data.size()};
    REQUIRE(Slic3r::png::decode_colored_png(rb, img));
    return img;
}

// Content-identity: same dimensions and identical RGB per pixel (alpha
// compared only when both sides carry it). The save path re-encodes
// thumbnails through libslic3r's own exporter, so bytes may differ while
// the image must not (PNG is lossless — see the 2026-07-15 thumbnail-
// passthrough design note).
static void require_same_image(const std::vector<uint8_t>& a,
                               const std::vector<uint8_t>& b) {
    auto ia = decode_or_fail(a);
    auto ib = decode_or_fail(b);
    REQUIRE(ia.cols == ib.cols);
    REQUIRE(ia.rows == ib.rows);
    const int cha = ia.bytes_per_pixel, chb = ib.bytes_per_pixel;
    REQUIRE(cha >= 3);
    REQUIRE(chb >= 3);
    for (size_t px = 0; px < ia.cols * ia.rows; ++px) {
        for (int c = 0; c < 3; ++c) {
            if (ia.buf[px * cha + c] != ib.buf[px * chb + c]) {
                INFO("pixel " << px << " channel " << c);
                REQUIRE(ia.buf[px * cha + c] == ib.buf[px * chb + c]);
            }
        }
        if (cha == 4 && chb == 4 && ia.buf[px * 4 + 3] != ib.buf[px * 4 + 3]) {
            INFO("pixel " << px << " alpha");
            REQUIRE(ia.buf[px * 4 + 3] == ib.buf[px * 4 + 3]);
        }
    }
}

TEST_CASE("thumbnail passthrough: plate_1.png preserved after plate add",
          "[e2e][thumbnail_passthrough]") {
    const std::string ref = canonical_committed_3mf();
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(ref, out, fs::copy_option::overwrite_if_exists);

    const auto src_big   = read_zip_entry(out, "Metadata/plate_1.png");
    const auto src_small = read_zip_entry(out, "Metadata/plate_1_small.png");
    REQUIRE(!src_big.empty());
    REQUIRE(!src_small.empty());

    auto r = spawn_cli({"plate", "add", out, "--name", "NewPlate"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    SECTION("plate_1.png content-equal to source") {
        require_same_image(read_zip_entry(out, "Metadata/plate_1.png"), src_big);
    }
    SECTION("plate_1_small.png present and valid") {
        // The small thumbnail is derived from the big one by the exporter
        // (not passed through), so only presence/validity is pinned.
        REQUIRE(is_valid_png(read_zip_entry(out, "Metadata/plate_1_small.png")));
    }
    SECTION("plate_2.png is valid PNG") {
        REQUIRE(is_valid_png(read_zip_entry(out, "Metadata/plate_2.png")));
    }
    SECTION("plate_2_small.png is valid PNG") {
        REQUIRE(is_valid_png(read_zip_entry(out, "Metadata/plate_2_small.png")));
    }

    fs::remove(out);
}

TEST_CASE("thumbnail passthrough: project init preserves plate_1.png",
          "[e2e][thumbnail_passthrough]") {
    const std::string ref = canonical_committed_3mf();
    const std::string out = fresh_temp_path(".3mf");

    auto r = spawn_cli({"project", "init", out, "--template", ref});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);

    REQUIRE(read_zip_entry(out, "Metadata/plate_1.png")
         == read_zip_entry(ref, "Metadata/plate_1.png"));

    fs::remove(out);
}

TEST_CASE("thumbnail passthrough: synthesized plate_2.png starts with PNG signature",
          "[e2e][thumbnail_passthrough]") {
    const std::string ref = canonical_committed_3mf();
    const std::string out = fresh_temp_path(".3mf");
    fs::copy_file(ref, out, fs::copy_option::overwrite_if_exists);

    REQUIRE(spawn_cli({"plate", "add", out, "--name", "TestPlate"}).exit_code == 0);

    auto plate2 = read_zip_entry(out, "Metadata/plate_2.png");
    REQUIRE(is_valid_png(plate2));
    auto plate2s = read_zip_entry(out, "Metadata/plate_2_small.png");
    REQUIRE(is_valid_png(plate2s));

    fs::remove(out);
}
