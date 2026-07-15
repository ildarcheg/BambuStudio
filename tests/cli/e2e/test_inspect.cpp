#include "test_helpers.hpp"

#include <catch2/catch.hpp>

using namespace bambu_cli_test;

TEST_CASE("inspect: prints plate/object/filament counts (text mode)", "[m2][inspect]") {
    auto r = spawn_cli({"inspect", canonical_committed_3mf()});
    INFO("stderr: " << r.stderr_text);
    INFO("stdout: " << r.stdout_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_text.find("plates")    != std::string::npos);
    REQUIRE(r.stdout_text.find("objects")   != std::string::npos);
    REQUIRE(r.stdout_text.find("filaments") != std::string::npos);
}

TEST_CASE("inspect: emits Shape A JSON under --json", "[m2][inspect][json]") {
    auto r = spawn_cli({"--json", "inspect", canonical_committed_3mf()});
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_text.find("\"status\":\"ok\"")     != std::string::npos);
    REQUIRE(r.stdout_text.find("\"plate_count\":")     != std::string::npos);
    REQUIRE(r.stdout_text.find("\"object_count\":")    != std::string::npos);
    REQUIRE(r.stdout_text.find("\"filament_count\":") != std::string::npos);
}

TEST_CASE("inspect: missing file -> exit 2", "[m2][inspect]") {
    auto r = spawn_cli({"inspect", "Z:/no/such/file.3mf"});
    REQUIRE(r.exit_code == 2);
}
