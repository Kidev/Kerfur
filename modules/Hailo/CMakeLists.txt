# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

# The Hailo accelerator plugin. Kerfur::Accelerator opens it at run time, so
# the application links nothing of HailoRT and starts where it is absent.
# The CI workflow reads the version from the line below and checks the package
# against its own sha256, which changes with it.
set(KERFUR_HAILORT_VERSION 5.1.1)

if (EMSCRIPTEN OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif ()

find_package(HailoRT ${KERFUR_HAILORT_VERSION} EXACT QUIET)
if (NOT HailoRT_FOUND)
    message(STATUS "Hailo: HailoRT ${KERFUR_HAILORT_VERSION} not found, accelerator plugin not built")
    return()
endif ()

add_library(kerfur-hailo MODULE src/hailodevice.cpp)

# accelerator.h only, which is standard C++ and shared with Core by design.
target_include_directories(kerfur-hailo PRIVATE ${CMAKE_SOURCE_DIR}/modules/Core/src)
target_link_libraries(kerfur-hailo PRIVATE HailoRT::libhailort)

# Beside the executable in the build tree, which is the first place the loader
# looks.
set_target_properties(
    kerfur-hailo
    PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN ON
)

install(TARGETS kerfur-hailo LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

message(STATUS "Hailo: accelerator plugin against HailoRT ${HailoRT_VERSION}")
