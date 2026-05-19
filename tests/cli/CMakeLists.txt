# bambu-cli test suite. Catch2 v2.x via tests/catch2/.
# Source files added per milestone (see docs/superpowers/plans/2026-05-19-bambu-cli.md).

set(BAMBU_CLI_TEST_SOURCES
    # Per-milestone additions appended below.
)

# Empty list at M0 — guard with a sentinel source so the target is well-formed
# but ctest doesn't run anything until M1 lands a real test file.
if(NOT BAMBU_CLI_TEST_SOURCES)
    set(BAMBU_CLI_TEST_SOURCES tests_placeholder.cpp)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/tests_placeholder.cpp
         "int main(){ return 0; }\n")
    add_executable(cli_tests ${CMAKE_CURRENT_BINARY_DIR}/tests_placeholder.cpp)
else()
    add_executable(cli_tests ${BAMBU_CLI_TEST_SOURCES})
    target_link_libraries(cli_tests PRIVATE test_common libslic3r OpenSSL::Crypto)
    if(WIN32)
        target_link_libraries(cli_tests PRIVATE bcrypt)
    endif()
    target_compile_definitions(cli_tests PRIVATE
        BAMBU_CLI_EXE="$<TARGET_FILE:bambu-cli>"
        BAMBU_CLI_FIXTURE_3MF="${CMAKE_SOURCE_DIR}/tests/cli/fixtures/reference.3mf"
        BAMBU_CLI_FIXTURE_STL_DIR="${CMAKE_SOURCE_DIR}/tests/cli/fixtures/stls"
        BAMBU_CLI_LOCAL_REFERENCE_3MF="${BAMBU_CLI_REFERENCE_3MF}"
        BAMBU_CLI_LOCAL_STL_DIR="${BAMBU_CLI_STL_DIR}"
    )
    add_dependencies(cli_tests bambu-cli)
    set_target_properties(cli_tests PROPERTIES
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
        MAP_IMPORTED_CONFIG_MINSIZEREL     RELEASE)
    add_test(NAME cli:all COMMAND cli_tests --order rand --warn NoAssertions)
endif()
