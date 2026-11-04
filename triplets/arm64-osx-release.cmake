# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

# Overrides vcpkg's community arm64-osx-release triplet.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

set(VCPKG_BUILD_TYPE release)

# ggml targets the default Apple arm64 baseline, so the binary runs on any
# Apple silicon Mac. Its -mcpu=native probe also disagrees with Apple Clang on
# whether i8mm is available.
if (PORT STREQUAL "ggml")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DGGML_NATIVE=OFF)
endif ()
