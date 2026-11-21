# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

# arm64 Linux for the Raspberry Pi 4 (Cortex-A72, Armv8.0).
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)

# cmake/check-ggml-baseline.sh reads this -march and knows which instructions
# fall outside it.
if (PORT STREQUAL "ggml")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS
        -DGGML_NATIVE=OFF
        -DGGML_CPU_ARM_ARCH=armv8-a)
endif ()
