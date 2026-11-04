# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

# Overrides vcpkg's community wasm32-emscripten triplet.
#
# Two flags have to be here rather than in the project, because every object in
# the final link must agree on them and vcpkg builds its ports out of process.
#
# -msimd128: OpenCV's intrin_wasm.hpp calls wasm_i8x16_splat and its neighbours
# unconditionally, and Clang refuses to inline them into a translation unit
# compiled without the simd128 target feature. That is the whole of
# microsoft/vcpkg#45698, and without this flag opencv4 cannot be built for this
# triplet at all.
#
# -pthread: Qt for WebAssembly is used here in its wasm_multithread flavour.
# Mixing threaded and unthreaded objects produces a link that either fails on
# shared memory or corrupts at runtime, and the second failure mode is much
# harder to find than the first.
set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)
set(VCPKG_BUILD_TYPE release)

set(KERFUR_WASM_FLAGS "-msimd128 -pthread -fexceptions")
set(VCPKG_C_FLAGS "${KERFUR_WASM_FLAGS}")
set(VCPKG_CXX_FLAGS "${KERFUR_WASM_FLAGS}")
set(VCPKG_LINKER_FLAGS "-pthread")

# libwebp builds its SSE4.1 sources for Emscripten without the SSE2 helpers they include.
if (PORT STREQUAL "libwebp")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DWEBP_ENABLE_SIMD=OFF)
endif ()

# OpenCV's CPU detection treats wasm32 as x86 and probes an SSE baseline; with
# both lists empty its SIMD comes from intrin_wasm.hpp alone, as in OpenCV's
# own build_js.py. WITH_V4L defaults on for every UNIX target and the port
# verifies it, so it is turned off explicitly.
if (PORT STREQUAL "opencv4")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS
        -DX86=OFF
        -DCPU_BASELINE=
        -DCPU_DISPATCH=
        -DWITH_V4L=OFF
    )
endif ()

# The port builds run Emscripten from the environment the build was started in.
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED EMSCRIPTEN_ROOT EMSDK PATH)
if (NOT DEFINED ENV{EMSDK} AND NOT DEFINED ENV{EMSCRIPTEN_ROOT})
    message(FATAL_ERROR "Set EMSDK (or EMSCRIPTEN_ROOT) before building for wasm32-emscripten")
endif ()

# Chainloading Emscripten.cmake directly would drop the flags above; see the
# wrapper for how it applies them.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/emscripten-chainload.cmake")
