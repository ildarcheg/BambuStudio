#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_tab_ops.hpp"
#include "exceptions.hpp"

#include <boost/filesystem.hpp>

using bambu_cli::ProjectState;
namespace fs = boost::filesystem;

static const std::string kTxt = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../assembly_smoke.txt";
static const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";

// ---- sanitize_aux_name — DYNAMIC_SECTION over reserved names ---------------

TEST_CASE("sanitize_aux_name: Windows reserved names rejected (case-insensitive)",
          "[unit][c3][sanitize_aux]") {
    const std::vector<std::string> reserved = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };
    for (const auto& name : reserved) {
        DYNAMIC_SECTION("reserved name: " << name) {
            REQUIRE_THROWS_AS(bambu_cli::sanitize_aux_name(name),
                              bambu_cli::AuxNameError);
            // Also with extension.
            REQUIRE_THROWS_AS(bambu_cli::sanitize_aux_name(name + ".txt"),
                              bambu_cli::AuxNameError);
            // Lower-case variant.
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            REQUIRE_THROWS_AS(bambu_cli::sanitize_aux_name(lower),
                              bambu_cli::AuxNameError);
        }
    }
}

TEST_CASE("sanitize_aux_name: path separators rejected",
          "[unit][c3][sanitize_aux]") {
    REQUIRE_THROWS_AS(bambu_cli::sanitize_aux_name("foo/bar.txt"),
                      bambu_cli::AuxNameError);
    REQUIRE_THROWS_AS(bambu_cli::sanitize_aux_name("foo\\bar.txt"),
                      bambu_cli::AuxNameError);
}

TEST_CASE("sanitize_aux_name: dot-only names rejected",
          "[unit][c3][sanitize_aux]") {
    REQUIRE_THROWS_AS(bambu_cli::sanitize_aux_name("."),  bambu_cli::AuxNameError);
    REQUIRE_THROWS_AS(bambu_cli::sanitize_aux_name(".."), bambu_cli::AuxNameError);
    REQUIRE_THROWS_AS(bambu_cli::sanitize_aux_name("..."),bambu_cli::AuxNameError);
}

TEST_CASE("sanitize_aux_name: leading/trailing whitespace rejected",
          "[unit][c3][sanitize_aux]") {
    REQUIRE_THROWS_AS(bambu_cli::sanitize_aux_name(" file.txt"),
                      bambu_cli::AuxNameError);
    REQUIRE_THROWS_AS(bambu_cli::sanitize_aux_name("file.txt "),
                      bambu_cli::AuxNameError);
}

TEST_CASE("sanitize_aux_name: valid names pass through unchanged",
          "[unit][c3][sanitize_aux]") {
    REQUIRE(bambu_cli::sanitize_aux_name("file.txt")    == "file.txt");
    REQUIRE(bambu_cli::sanitize_aux_name("my_bom.csv")  == "my_bom.csv");
    REQUIRE(bambu_cli::sanitize_aux_name("photo.jpg")   == "photo.jpg");
    REQUIRE(bambu_cli::sanitize_aux_name("CONNECT.txt") == "CONNECT.txt"); // not reserved
}

// ---- AuxFolder helpers -----------------------------------------------------

TEST_CASE("folder_flag returns hyphen form", "[unit][c3][folder_helpers]") {
    REQUIRE(bambu_cli::folder_flag(bambu_cli::AuxFolder::ModelPictures)    == "model-pictures");
    REQUIRE(bambu_cli::folder_flag(bambu_cli::AuxFolder::BillOfMaterials)  == "bill-of-materials");
    REQUIRE(bambu_cli::folder_flag(bambu_cli::AuxFolder::AssemblyGuide)    == "assembly-guide");
    REQUIRE(bambu_cli::folder_flag(bambu_cli::AuxFolder::Others)           == "others");
}

TEST_CASE("folder_json_key returns underscore form", "[unit][c3][folder_helpers]") {
    REQUIRE(bambu_cli::folder_json_key(bambu_cli::AuxFolder::AssemblyGuide) == "assembly_guide");
    REQUIRE(bambu_cli::folder_json_key(bambu_cli::AuxFolder::ModelPictures) == "model_pictures");
}

// ---- aux_add / aux_list / aux_remove ---------------------------------------

TEST_CASE("aux_add: adds file to Others folder",
          "[unit][c3][aux_add]") {
    REQUIRE(fs::exists(kTxt));
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::AuxAddParams p;
    p.folder    = bambu_cli::AuxFolder::Others;
    p.file_path = kTxt;
    REQUIRE_NOTHROW(bambu_cli::aux_add(s, p));

    const auto entries = bambu_cli::aux_list(s);
    const bool found = std::any_of(entries.begin(), entries.end(),
        [](const bambu_cli::AuxEntry& e){ return e.name == "assembly_smoke.txt"; });
    REQUIRE(found);
}

TEST_CASE("aux_add: collision without force -> AuxCollisionError",
          "[unit][c3][aux_add]") {
    REQUIRE(fs::exists(kTxt));
    REQUIRE(fs::exists(kPng));
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::AuxAddParams p;
    p.folder    = bambu_cli::AuxFolder::Others;
    p.file_path = kTxt;
    bambu_cli::aux_add(s, p);

    // Second add with different content, same name.
    bambu_cli::AuxAddParams p2;
    p2.folder    = bambu_cli::AuxFolder::Others;
    p2.file_path = kPng;
    p2.name      = "assembly_smoke.txt";
    REQUIRE_THROWS_AS(bambu_cli::aux_add(s, p2), bambu_cli::AuxCollisionError);
}

TEST_CASE("aux_add --force: overwrites existing",
          "[unit][c3][aux_add]") {
    REQUIRE(fs::exists(kTxt));
    REQUIRE(fs::exists(kPng));
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::AuxAddParams p;
    p.folder    = bambu_cli::AuxFolder::Others;
    p.file_path = kTxt;
    bambu_cli::aux_add(s, p);

    bambu_cli::AuxAddParams p2;
    p2.folder    = bambu_cli::AuxFolder::Others;
    p2.file_path = kPng;
    p2.name      = "assembly_smoke.txt";
    p2.force     = true;
    REQUIRE_NOTHROW(bambu_cli::aux_add(s, p2));
}

TEST_CASE("aux_add: missing source -> BadAuxFile",
          "[unit][c3][aux_add]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::AuxAddParams p;
    p.folder    = bambu_cli::AuxFolder::Others;
    p.file_path = "Z:/no/such/file.txt";
    REQUIRE_THROWS_AS(bambu_cli::aux_add(s, p), bambu_cli::BadAuxFile);
}

TEST_CASE("aux_add: bad name with path separator -> AuxNameError",
          "[unit][c3][aux_add]") {
    REQUIRE(fs::exists(kTxt));
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    bambu_cli::AuxAddParams p;
    p.folder    = bambu_cli::AuxFolder::Others;
    p.file_path = kTxt;
    p.name      = "bad/name.txt";
    REQUIRE_THROWS_AS(bambu_cli::aux_add(s, p), bambu_cli::AuxNameError);
}

TEST_CASE("aux_remove: removes added entry",
          "[unit][c3][aux_remove]") {
    REQUIRE(fs::exists(kTxt));
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    bambu_cli::AuxAddParams p;
    p.folder    = bambu_cli::AuxFolder::Others;
    p.file_path = kTxt;
    bambu_cli::aux_add(s, p);

    REQUIRE_NOTHROW(bambu_cli::aux_remove(s, bambu_cli::AuxFolder::Others, "assembly_smoke.txt"));

    const auto entries = bambu_cli::aux_list(s);
    const bool found = std::any_of(entries.begin(), entries.end(),
        [](const bambu_cli::AuxEntry& e){ return e.name == "assembly_smoke.txt"; });
    REQUIRE_FALSE(found);
}

TEST_CASE("aux_remove: unknown name -> std::out_of_range",
          "[unit][c3][aux_remove]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    REQUIRE_THROWS_AS(
        bambu_cli::aux_remove(s, bambu_cli::AuxFolder::Others, "no_such_file.txt"),
        std::out_of_range);
}

TEST_CASE("aux_list: file added under Profile Pictures is enumerated",
          "[unit][c3][aux_list]") {
    bambu_cli::ProjectState s;
    bambu_cli_unit::load_reference_into(s);

    const std::string kPng = std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/../cover_smoke.png";
    bambu_cli::AuxAddParams ap;
    ap.folder    = bambu_cli::AuxFolder::ProfilePictures;
    ap.file_path = kPng;
    bambu_cli::aux_add(s, ap);

    const auto entries = bambu_cli::aux_list(s);
    bool saw = false;
    for (const auto& e : entries) {
        if (e.folder == bambu_cli::AuxFolder::ProfilePictures &&
            e.name == "cover_smoke.png") {
            saw = true;
            break;
        }
    }
    REQUIRE(saw);
}
