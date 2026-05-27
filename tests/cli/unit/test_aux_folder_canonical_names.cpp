#include <catch2/catch.hpp>
#include "project_tab_ops.hpp"

// Pin the exact canonical names. These must match Bambu Studio's
// auxiliary directory layout (src/slic3r/GUI/Auxiliary.hpp:75 and
// src/slic3r/GUI/Project.cpp:214-226). A future accidental rename will
// fail loudly here with a clear diff.

TEST_CASE("AuxFolder: canonical subdir strings",
          "[unit][aux_folder_names]") {
    using bambu_cli::folder_subdir;
    using bambu_cli::AuxFolder;
    REQUIRE(folder_subdir(AuxFolder::ModelPictures)    == "Model Pictures");
    REQUIRE(folder_subdir(AuxFolder::ProfilePictures)  == "Profile Pictures");
    REQUIRE(folder_subdir(AuxFolder::BillOfMaterials)  == "Bill of Materials");
    REQUIRE(folder_subdir(AuxFolder::AssemblyGuide)    == "Assembly Guide");
    REQUIRE(folder_subdir(AuxFolder::Others)           == "Others");
}

TEST_CASE("AuxFolder: canonical --folder flag spellings",
          "[unit][aux_folder_names]") {
    using bambu_cli::folder_flag;
    using bambu_cli::AuxFolder;
    REQUIRE(folder_flag(AuxFolder::ModelPictures)    == "model-pictures");
    REQUIRE(folder_flag(AuxFolder::ProfilePictures)  == "profile-pictures");
    REQUIRE(folder_flag(AuxFolder::BillOfMaterials)  == "bill-of-materials");
    REQUIRE(folder_flag(AuxFolder::AssemblyGuide)    == "assembly-guide");
    REQUIRE(folder_flag(AuxFolder::Others)           == "others");
}

TEST_CASE("AuxFolder: canonical JSON keys",
          "[unit][aux_folder_names]") {
    using bambu_cli::folder_json_key;
    using bambu_cli::AuxFolder;
    REQUIRE(folder_json_key(AuxFolder::ModelPictures)    == "model_pictures");
    REQUIRE(folder_json_key(AuxFolder::ProfilePictures)  == "profile_pictures");
    REQUIRE(folder_json_key(AuxFolder::BillOfMaterials)  == "bill_of_materials");
    REQUIRE(folder_json_key(AuxFolder::AssemblyGuide)    == "assembly_guide");
    REQUIRE(folder_json_key(AuxFolder::Others)           == "others");
}
