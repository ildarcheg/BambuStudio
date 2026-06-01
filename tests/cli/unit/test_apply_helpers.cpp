#include "apply_helpers.hpp"
#include "exceptions.hpp"

#include <catch2/catch.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;
using bambu_cli::ManifestFieldError;
using bambu_cli::require_only;

TEST_CASE("require_only: accepts when all keys known", "[apply_helpers][require_only]") {
    json step = {{"op", "plate.add"}, {"name", "P1"}};
    REQUIRE_NOTHROW(require_only(step, {"op", "name"}));
}

TEST_CASE("require_only: rejects unknown field", "[apply_helpers][require_only]") {
    json step = {{"op", "plate.add"}, {"name", "P1"}, {"filement", 2}};
    REQUIRE_THROWS_AS(require_only(step, {"op", "name"}), ManifestFieldError);
}

TEST_CASE("require_only: error names the offending field", "[apply_helpers][require_only]") {
    json step = {{"op", "plate.add"}, {"filement", 2}};
    try {
        require_only(step, {"op", "name"});
        FAIL("expected ManifestFieldError");
    } catch (const ManifestFieldError& e) {
        std::string what = e.what();
        REQUIRE(what.find("filement") != std::string::npos);
    }
}

TEST_CASE("require_only: empty step accepted regardless of known list",
          "[apply_helpers][require_only]") {
    json step = json::object();
    REQUIRE_NOTHROW(require_only(step, {"op", "name"}));
}
