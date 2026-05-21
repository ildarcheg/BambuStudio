#include "unit_helpers.hpp"
#include "../test_helpers.hpp"
#include "io.hpp"

#include <catch2/catch.hpp>

namespace bambu_cli_unit {

void load_reference_into(bambu_cli::ProjectState& state) {
    auto r = bambu_cli::load_project(bambu_cli_test::canonical_committed_3mf(),
                                     state);
    INFO("load_project: " << r.error_message);
    REQUIRE(r.ok);
}

void make_minimal_state(bambu_cli::ProjectState& state, int n_plates) {
    for (int i = 0; i < n_plates; ++i) {
        auto* pd = new Slic3r::PlateData();
        pd->plate_index = i;
        pd->plate_name  = std::string("Plate-") + std::to_string(i + 1);
        state.plate_data.push_back(pd);
    }
}

std::string fixture_stl(const std::string& name) {
    return std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/" + name;
}

} // namespace bambu_cli_unit
