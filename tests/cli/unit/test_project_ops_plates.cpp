#include <catch2/catch.hpp>
#include "unit_helpers.hpp"
#include "project_ops.hpp"

using bambu_cli::ProjectState;

TEST_CASE("add_plate: appends to plate_data with monotonic index",
          "[unit][plates]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);
    auto r = bambu_cli::add_plate(s, "Plate-2");
    REQUIRE(r.ok);
    REQUIRE(s.plate_data.size() == 2);
    REQUIRE(s.plate_data[1]->plate_name == "Plate-2");
    REQUIRE(s.plate_data[1]->plate_index == 1);
}

TEST_CASE("add_plate: empty name -> usage_error", "[unit][plates]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);
    auto r = bambu_cli::add_plate(s, "");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error_code == "usage_error");
    REQUIRE(r.exit_code == bambu_cli::to_int(bambu_cli::ExitCode::usage_error));
}

TEST_CASE("add_plate: duplicate name -> duplicate_name", "[unit][plates]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);
    REQUIRE(bambu_cli::add_plate(s, "Plate-2").ok);
    auto r = bambu_cli::add_plate(s, "Plate-2");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error_code == "duplicate_name");
    REQUIRE(r.exit_code == bambu_cli::to_int(bambu_cli::ExitCode::duplicate_name));
}

TEST_CASE("remove_plate: removes named plate and compacts indices",
          "[unit][plates]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 3);   // Plate-1, Plate-2, Plate-3
    REQUIRE(bambu_cli::remove_plate(s, "Plate-2").ok);
    REQUIRE(s.plate_data.size() == 2);
    REQUIRE(s.plate_data[0]->plate_name == "Plate-1");
    REQUIRE(s.plate_data[1]->plate_name == "Plate-3");
    REQUIRE(s.plate_data[0]->plate_index == 0);
    REQUIRE(s.plate_data[1]->plate_index == 1);   // compacted from 2 to 1
}

TEST_CASE("remove_plate: name not found -> unknown_reference",
          "[unit][plates]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);
    auto r = bambu_cli::remove_plate(s, "Missing");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error_code == "unknown_reference");
    REQUIRE(r.exit_code == bambu_cli::to_int(bambu_cli::ExitCode::unknown_reference));
}

TEST_CASE("rename_plate: name updated, index unchanged", "[unit][plates]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 2);
    REQUIRE(bambu_cli::rename_plate(s, "Plate-2", "FinalLayout").ok);
    REQUIRE(s.plate_data[1]->plate_name == "FinalLayout");
    REQUIRE(s.plate_data[1]->plate_index == 1);
}

TEST_CASE("rename_plate: empty target -> usage_error", "[unit][plates]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 2);
    auto r = bambu_cli::rename_plate(s, "Plate-2", "");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error_code == "usage_error");
}

TEST_CASE("rename_plate: collision -> duplicate_name", "[unit][plates]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 2);
    auto r = bambu_cli::rename_plate(s, "Plate-2", "Plate-1");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error_code == "duplicate_name");
}

TEST_CASE("rename_plate: source not found -> unknown_reference",
          "[unit][plates]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);
    auto r = bambu_cli::rename_plate(s, "Missing", "NewName");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error_code == "unknown_reference");
}

TEST_CASE("list_plate_names: returns names in plate_data order",
          "[unit][plates]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 3);
    auto names = bambu_cli::list_plate_names(s);
    REQUIRE(names.size() == 3);
    REQUIRE(names[0] == "Plate-1");
    REQUIRE(names[1] == "Plate-2");
    REQUIRE(names[2] == "Plate-3");
}
