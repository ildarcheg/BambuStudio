# bambu-cli test suite. Catch2 v2.x via tests/catch2/.
# Source files added per milestone (see docs/superpowers/plans/2026-05-19-bambu-cli.md).

set(BAMBU_CLI_TEST_SOURCES
    cli_tests_main.cpp
    test_helpers.cpp
    test_project_init.cpp
    test_inspect.cpp
    test_plate_add.cpp
    test_plate_list.cpp
    test_object_add.cpp
    test_object_filament.cpp
    test_object_transforms.cpp
    test_config.cpp
)

add_executable(cli_tests ${BAMBU_CLI_TEST_SOURCES}
    # Include the Http/BBL_Encrypt stubs from src/cli/ so the test binary links.
    # libslic3r.lib(LogSink.obj) references these symbols; the stubs satisfy
    # them without pulling in curl or openssl-ssl.
    ${CMAKE_SOURCE_DIR}/src/cli/stubs_for_libslic3r.cpp
)
target_link_libraries(cli_tests PRIVATE test_common libslic3r OpenSSL::Crypto)
if(WIN32)
    target_link_libraries(cli_tests PRIVATE bcrypt)
endif()
target_include_directories(cli_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/cli
)
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
