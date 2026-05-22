# bambu-cli test suite. Catch2 v2.x via tests/catch2/.
# Source files added per milestone (see docs/superpowers/plans/2026-05-19-bambu-cli.md).

set(BAMBU_CLI_TEST_SOURCES
    cli_tests_main.cpp
    test_helpers.cpp
    archive_invariants.cpp
    # E2E tests: spawn bambu-cli.exe via run_cli + assert on stdout/stderr/exit/archive.
    e2e/test_project_init.cpp
    e2e/test_inspect.cpp
    e2e/test_plate_add.cpp
    e2e/test_plate_list.cpp
    e2e/test_plate_remove_rename.cpp
    e2e/test_object_add.cpp
    e2e/test_object_filament.cpp
    e2e/test_object_transforms.cpp
    e2e/test_object_remove_set_filament.cpp
    e2e/test_config.cpp
    e2e/test_json_and_exit_codes.cpp
    # Unit tests: link project_ops directly; no subprocess.
    unit/unit_helpers.cpp
    unit/test_project_ops_plates.cpp
    unit/test_project_ops_objects.cpp
    unit/test_project_ops_config.cpp
    unit/test_plate_stride.cpp
    # roundtrip/ is a placeholder for future phases.
)

add_executable(cli_tests ${BAMBU_CLI_TEST_SOURCES}
    # Include the Http/BBL_Encrypt stubs from src/cli/ so the test binary links.
    # libslic3r.lib(LogSink.obj) references these symbols; the stubs satisfy
    # them without pulling in curl or openssl-ssl.
    ${CMAKE_SOURCE_DIR}/src/cli/stubs_for_libslic3r.cpp
    # nanosvg_impl.cpp provides nsvgParse/nsvgParseFromFile/nsvgDelete which
    # libslic3r.lib(NSVGUtils.obj) requires (pulled in transitively by project_ops.cpp
    # -> bbs_3mf.hpp -> NSVGUtils.hpp).
    ${CMAKE_SOURCE_DIR}/src/cli/nanosvg_impl.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/project_ops.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/io.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/invariant_guard.cpp
)
target_link_libraries(cli_tests PRIVATE test_common libslic3r OpenSSL::Crypto)
if(WIN32)
    target_link_libraries(cli_tests PRIVATE bcrypt)
endif()
target_include_directories(cli_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/cli
    ${CMAKE_CURRENT_SOURCE_DIR}     # for "test_helpers.hpp" / "archive_invariants.hpp" from e2e/ and unit/
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
