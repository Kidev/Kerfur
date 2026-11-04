# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

# Static libraries on the dynamic CRT, which is what the Qt binaries use.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_BUILD_TYPE release)

# MSVC has no __fp16 type and no dot product intrinsics, yet OpenCV's probes
# report both extensions as available on arm64 and its NEON_FP16 dispatch
# sources then fail to compile (opencv/opencv#25052). Answering the probes up
# front leaves the NEON baseline in place.
if (PORT STREQUAL "opencv4")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS
        -DHAVE_CPU_NEON_FP16_SUPPORT=0
        -DHAVE_CPU_NEON_DOTPROD_SUPPORT=0
    )
endif ()
