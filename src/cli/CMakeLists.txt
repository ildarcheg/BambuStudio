# bambu-cli — a thin orchestrator over libslic3r that composes .3mf project files.
# v1 entry point: src/cli/main.cpp

cmake_minimum_required(VERSION 3.13)

# Local-realistic fixture paths (per design spec §5).
# Defaults point at the in-repo copy under tests/cli/fixtures/local/ so a fresh
# clone is self-contained. Override on the command line to point at a different
# set, e.g. -DBAMBU_CLI_REFERENCE_3MF=/path/to/other.3mf
set(BAMBU_CLI_REFERENCE_3MF
    "${CMAKE_SOURCE_DIR}/tests/cli/fixtures/local/temp_project_for_bambu_studio.3mf"
    CACHE FILEPATH
    "Canonical pre-configured reference 3MF for local dev + manual GUI smoke")

set(BAMBU_CLI_STL_DIR
    "${CMAKE_SOURCE_DIR}/tests/cli/fixtures/local/stls"
    CACHE PATH
    "Directory containing 000_01_test_*.stl canonical fixtures")

# --- Static library: all CLI code except main() --------------------------
# Built once and linked by both `bambu-cli` (the executable) and `cli_tests`
# (the test binary). Sources used to be compiled twice; the lib eliminates
# that duplication (and the drift it caused).
#
# Sibling pattern: matches OrcaSlicer's `orca_cli_core` STATIC target.
set(BAMBU_CLI_CORE_SOURCES
    stubs_for_libslic3r.cpp
    nanosvg_impl.cpp
    io.cpp
    invariant_guard.cpp
    json_output.cpp
    project_ops.cpp
    project_tab_ops.cpp
    png_placeholder.cpp
    commands/project.cpp
    commands/project_tab.cpp
    commands/inspect.cpp
    commands/plate.cpp
    commands/object.cpp
    commands/config.cpp
)

add_library(bambu_cli_core STATIC ${BAMBU_CLI_CORE_SOURCES})

# PUBLIC so consumers (the exe and the tests) inherit the include paths
# and link deps transitively.
target_include_directories(bambu_cli_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/CLI11
    ${CMAKE_SOURCE_DIR}/src         # for <nlohmann/json.hpp> (vendored at src/nlohmann/json.hpp)
)

target_compile_features(bambu_cli_core PUBLIC cxx_std_17)

target_link_libraries(bambu_cli_core PUBLIC
    libslic3r
    cereal
    boost_headeronly
    OpenSSL::Crypto       # MD5_*
)
if(WIN32)
    target_link_libraries(bambu_cli_core PUBLIC bcrypt)   # boost::uuids on Windows
endif()

# IMPORTED-target Debug/Release mapping insurance (build_environment memory).
set_target_properties(bambu_cli_core PROPERTIES
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
    MAP_IMPORTED_CONFIG_MINSIZEREL     RELEASE
)

# --- Executable ----------------------------------------------------------
add_executable(bambu-cli main.cpp)
target_link_libraries(bambu-cli PRIVATE bambu_cli_core)
set_target_properties(bambu-cli PROPERTIES
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
    MAP_IMPORTED_CONFIG_MINSIZEREL     RELEASE
)

# --- POST_BUILD DLL copies (Windows) -------------------------------------
if(WIN32)
    set(_DEP_BIN "${CMAKE_PREFIX_PATH}/bin")
    set(_DEP_OCCT_BIN "${CMAKE_PREFIX_PATH}/bin/occt")
    # Use copy_directory_if_different (CMake 3.26+) if available; otherwise
    # fall back to copy_directory (re-copies on every build but always correct).
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.26")
        set(_COPY_CMD copy_directory_if_different)
    else()
        set(_COPY_CMD copy_directory)
    endif()
    add_custom_command(TARGET bambu-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E ${_COPY_CMD} "${_DEP_BIN}" "$<TARGET_FILE_DIR:bambu-cli>"
        VERBATIM)
    if(EXISTS "${_DEP_OCCT_BIN}")
        add_custom_command(TARGET bambu-cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E ${_COPY_CMD} "${_DEP_OCCT_BIN}" "$<TARGET_FILE_DIR:bambu-cli>"
            VERBATIM)
    endif()
endif()

# --- Install (out of scope for v1; bambu-cli ships from build/) ---
