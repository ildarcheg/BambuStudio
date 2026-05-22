#include "test_helpers.hpp"
#include "archive_invariants.hpp"

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

    SECTION("plate_1.png byte-equal to source") {
        REQUIRE(read_zip_entry(out, "Metadata/plate_1.png") == src_big);
    }
    SECTION("plate_1_small.png byte-equal to source") {
        REQUIRE(read_zip_entry(out, "Metadata/plate_1_small.png") == src_small);
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
