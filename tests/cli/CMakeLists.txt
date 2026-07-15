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
    e2e/test_plate_layout_commands.cpp
    e2e/test_object_add.cpp
    e2e/test_object_filament.cpp
    e2e/test_object_transforms.cpp
    e2e/test_object_remove_set_filament.cpp
    e2e/test_object_split.cpp
    e2e/test_object_merge.cpp
    e2e/test_object_auto_orient.cpp
    e2e/test_config.cpp
    e2e/test_json_and_exit_codes.cpp
    e2e/test_project_info.cpp
    e2e/test_project_info_non_ascii.cpp
    e2e/test_project_profile.cpp
    e2e/test_project_aux.cpp
    e2e/test_thumbnail_passthrough.cpp
    e2e/test_project_apply.cpp
    # Unit tests: link project_ops directly; no subprocess.
    unit/unit_helpers.cpp
    unit/test_project_ops_plates.cpp
    unit/test_plate_layout.cpp
    unit/test_project_ops_objects.cpp
    unit/test_project_ops_split.cpp
    unit/test_project_ops_merge.cpp
    unit/test_project_ops_config.cpp
    unit/test_plate_stride.cpp
    unit/test_project_info_ops.cpp
    unit/test_project_profile_ops.cpp
    unit/test_project_aux_ops.cpp
    unit/test_png_placeholder.cpp
    unit/test_image_signature.cpp
    unit/test_aux_folder_canonical_names.cpp
    unit/test_cover_decoupling.cpp
    unit/test_cover_pick_by_name.cpp
    unit/test_cover_real_images.cpp
    unit/test_invariant_aux_passthrough.cpp
    unit/test_invariant_cover_references.cpp
    unit/test_index_conventions.cpp
    unit/test_no_gui_includes.cpp
    unit/test_apply_helpers.cpp
    unit/test_apply_manifest.cpp
    unit/test_project_apply_handlers.cpp
    # Roundtrip tests: in-process load -> mutate -> save -> reload cycles.
    # Ported from OrcaSlicer tests/cli/roundtrip/ as cross-project convergence
    # (2026-05-22).
    roundtrip/test_project_init.cpp
    roundtrip/test_plate.cpp
    roundtrip/test_plate_layout.cpp
    roundtrip/test_save_atomic.cpp
    roundtrip/test_output_flag.cpp
    roundtrip/test_split.cpp
    roundtrip/test_merge.cpp
    roundtrip/test_project_tab.cpp
    roundtrip/test_reference_3mf_passthrough.cpp
    roundtrip/test_non_ascii_metadata.cpp
    roundtrip/test_apply_roundtrip.cpp
)

# cli_tests links the bambu_cli_core static library (defined in src/cli/
# CMakeLists.txt) so production sources compile exactly once. Include paths
# and transitive link deps (libslic3r, OpenSSL::Crypto, bcrypt, cereal,
# boost_headeronly) propagate via PUBLIC on bambu_cli_core.
add_executable(cli_tests ${BAMBU_CLI_TEST_SOURCES})
target_link_libraries(cli_tests PRIVATE test_common bambu_cli_core)
target_include_directories(cli_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}     # for "test_helpers.hpp" / "archive_invariants.hpp" from e2e/ and unit/
)
target_compile_definitions(cli_tests PRIVATE
    BAMBU_CLI_EXE="$<TARGET_FILE:bambu-cli>"
    BAMBU_CLI_FIXTURE_3MF="${CMAKE_SOURCE_DIR}/tests/cli/fixtures/reference.3mf"
    BAMBU_CLI_FIXTURE_TEST_REFERENCE_3MF="${CMAKE_SOURCE_DIR}/tests/cli/fixtures/test_reference.3mf"
    BAMBU_CLI_FIXTURE_STL_DIR="${CMAKE_SOURCE_DIR}/tests/cli/fixtures/stls"
    BAMBU_CLI_LOCAL_REFERENCE_3MF="${BAMBU_CLI_REFERENCE_3MF}"
    BAMBU_CLI_LOCAL_STL_DIR="${BAMBU_CLI_STL_DIR}"
    BAMBU_CLI_SRC_DIR="${CMAKE_SOURCE_DIR}/src/cli"
)
add_dependencies(cli_tests bambu-cli)
set_target_properties(cli_tests PROPERTIES
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
    MAP_IMPORTED_CONFIG_MINSIZEREL     RELEASE)
add_test(NAME cli:all COMMAND cli_tests --order rand --warn NoAssertions)
