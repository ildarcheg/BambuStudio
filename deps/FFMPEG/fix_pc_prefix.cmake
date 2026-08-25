# Rewrite the relative prefix baked into the Windows ffmpeg prebuilt's
# pkg-config files.
#
# deps/FFMPEG/FFMPEG.cmake takes ffmpeg from a prebuilt archive on MSVC
# (bambulab/ffmpeg_prebuilts) and copies bin/ lib/ include/ into the destdir
# verbatim. That archive's *.pc files were generated with `prefix=./dist`, a
# path relative to whatever directory ffmpeg happened to be built in. The
# headers and import libs land in the right place, but pkg-config keeps
# reporting `-I./dist/include`.
#
# src/slic3r/CMakeLists.txt feeds that through
# `pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET ...)` into libslic3r_gui,
# and CMake rejects a relative entry in INTERFACE_INCLUDE_DIRECTORIES the
# moment another directory consumes the target:
#
#   CMake Error in tests/slic3rutils/CMakeLists.txt:
#     Target "libslic3r_gui" contains relative path in its
#     INTERFACE_INCLUDE_DIRECTORIES: "./dist/include"
#
# Only -DSLIC3R_BUILD_TESTS=ON trips it (tests/slic3rutils links
# libslic3r_gui), which is why upstream CI - which builds with tests off -
# never sees it.
#
# Invoked as: cmake -DPCDIR=<pkgconfig dir> -DPREFIX=<abs install prefix> -P

file(GLOB _pc_files "${PCDIR}/*.pc")
foreach(_pc IN LISTS _pc_files)
    file(READ "${_pc}" _content)
    string(REPLACE "./dist" "${PREFIX}" _patched "${_content}")
    if(NOT _patched STREQUAL _content)
        file(WRITE "${_pc}" "${_patched}")
        message(STATUS "fix_pc_prefix: rewrote relative prefix in ${_pc}")
    endif()
endforeach()
