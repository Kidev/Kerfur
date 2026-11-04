# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

# Chainload toolchain for the wasm32-emscripten overlay triplet.
#
# vcpkg applies VCPKG_C_FLAGS, VCPKG_CXX_FLAGS and VCPKG_LINKER_FLAGS only in
# its built-in toolchains. A triplet that chainloads Emscripten.cmake directly
# hands them to every port as unused cache variables, so this file includes
# Emscripten.cmake and then appends them to the CMAKE_*_FLAGS_INIT variables
# the same way scripts/toolchains/linux.cmake does.
if (NOT _KERFUR_VCPKG_EMSCRIPTEN_TOOLCHAIN)
    set(_KERFUR_VCPKG_EMSCRIPTEN_TOOLCHAIN 1)

    # try_compile projects re-read this file without the port's -D options.
    list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
        VCPKG_CRT_LINKAGE VCPKG_TARGET_ARCHITECTURE
        VCPKG_C_FLAGS VCPKG_CXX_FLAGS
        VCPKG_C_FLAGS_DEBUG VCPKG_CXX_FLAGS_DEBUG
        VCPKG_C_FLAGS_RELEASE VCPKG_CXX_FLAGS_RELEASE
        VCPKG_LINKER_FLAGS VCPKG_LINKER_FLAGS_RELEASE VCPKG_LINKER_FLAGS_DEBUG
    )

    if (DEFINED ENV{EMSCRIPTEN_ROOT})
        set(_KERFUR_EMSCRIPTEN_ROOT "$ENV{EMSCRIPTEN_ROOT}")
    elseif (DEFINED ENV{EMSDK})
        set(_KERFUR_EMSCRIPTEN_ROOT "$ENV{EMSDK}/upstream/emscripten")
    else ()
        find_path(_KERFUR_EMSCRIPTEN_ROOT "emcc")
    endif ()

    if (NOT EXISTS "${_KERFUR_EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake")
        message(FATAL_ERROR
            "Emscripten.cmake not found under '${_KERFUR_EMSCRIPTEN_ROOT}'; set EMSDK or EMSCRIPTEN_ROOT")
    endif ()

    include("${_KERFUR_EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake")

    string(APPEND CMAKE_C_FLAGS_INIT " ${VCPKG_C_FLAGS} ")
    string(APPEND CMAKE_CXX_FLAGS_INIT " ${VCPKG_CXX_FLAGS} ")
    string(APPEND CMAKE_C_FLAGS_DEBUG_INIT " ${VCPKG_C_FLAGS_DEBUG} ")
    string(APPEND CMAKE_CXX_FLAGS_DEBUG_INIT " ${VCPKG_CXX_FLAGS_DEBUG} ")
    string(APPEND CMAKE_C_FLAGS_RELEASE_INIT " ${VCPKG_C_FLAGS_RELEASE} ")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE_INIT " ${VCPKG_CXX_FLAGS_RELEASE} ")

    string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT " ${VCPKG_LINKER_FLAGS} ")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " ${VCPKG_LINKER_FLAGS} ")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${VCPKG_LINKER_FLAGS} ")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_DEBUG_INIT " ${VCPKG_LINKER_FLAGS_DEBUG} ")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT " ${VCPKG_LINKER_FLAGS_DEBUG} ")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT " ${VCPKG_LINKER_FLAGS_DEBUG} ")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_RELEASE_INIT " ${VCPKG_LINKER_FLAGS_RELEASE} ")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE_INIT " ${VCPKG_LINKER_FLAGS_RELEASE} ")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT " ${VCPKG_LINKER_FLAGS_RELEASE} ")
endif ()
