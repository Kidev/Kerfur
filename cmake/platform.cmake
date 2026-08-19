# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

# Resolves which Kerfur this build is, and which inference backend it carries.
#
# Platform and backend are separate questions on purpose. The platform decides
# what the machine is; the backend decides what runs the models on it. A Pi
# without its accelerator plugged in is still the `pi` platform and still has to
# build, it just falls back to the CPU backend like every other machine.
if (NOT DEFINED KERFUR_PLATFORM OR KERFUR_PLATFORM STREQUAL "auto")
    if (EMSCRIPTEN)
        set(KERFUR_PLATFORM "wasm")
    else ()
        find_library(KERFUR_HAILORT_LIBRARY hailort)
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$" AND KERFUR_HAILORT_LIBRARY)
            set(KERFUR_PLATFORM "pi")
        else ()
            set(KERFUR_PLATFORM "desktop")
        endif ()
    endif ()
endif ()

set(KERFUR_VALID_PLATFORMS pi desktop wasm)
if (NOT KERFUR_PLATFORM IN_LIST KERFUR_VALID_PLATFORMS)
    message(
        FATAL_ERROR
        "KERFUR_PLATFORM is '${KERFUR_PLATFORM}'; expected one of ${KERFUR_VALID_PLATFORMS} or auto"
    )
endif ()

# `none` exists so that a build can be reduced to the face and the control panel
# without touching the module list by hand, which is what the soak harness and
# any headless CI job want.
#
# Automatic resolution never selects `hailo`, even on a Pi with the accelerator
# present, because that backend lands with P2. Until then the Pi runs the same
# CPU graph as everything else, which is slower than the hardware it is sitting
# on and is still a working robot.
if (NOT DEFINED KERFUR_PERCEPTION_BACKEND OR KERFUR_PERCEPTION_BACKEND STREQUAL "auto")
    set(KERFUR_PERCEPTION_BACKEND "cpu")
endif ()

set(KERFUR_VALID_BACKENDS hailo cpu none)
if (NOT KERFUR_PERCEPTION_BACKEND IN_LIST KERFUR_VALID_BACKENDS)
    message(
        FATAL_ERROR
        "KERFUR_PERCEPTION_BACKEND is '${KERFUR_PERCEPTION_BACKEND}'; expected one of ${KERFUR_VALID_BACKENDS} or auto"
    )
endif ()

# Only reachable when asked for explicitly. Refusing rather than stubbing: a
# backend that configures, links and then never produces a detection is a worse
# outcome than a configure error naming the sub-project that has not landed.
if (KERFUR_PERCEPTION_BACKEND STREQUAL "hailo")
    message(
        FATAL_ERROR
        "KERFUR_PERCEPTION_BACKEND=hailo lands with P2. Configure with "
        "-DKERFUR_PERCEPTION_BACKEND=cpu until then."
    )
endif ()

set(KERFUR_HAS_PERCEPTION OFF)
if (NOT KERFUR_PERCEPTION_BACKEND STREQUAL "none")
    set(KERFUR_HAS_PERCEPTION ON)
endif ()

# A definition rather than a generated header: main.cpp is the only file that
# has to know, and it needs to know at the preprocessor level to decide whether
# the producer exists to be instantiated.
if (KERFUR_PERCEPTION_BACKEND STREQUAL "cpu")
    add_compile_definitions(KERFUR_PERCEPTION_CPU)
endif ()

message(STATUS "KERFUR_PLATFORM=${KERFUR_PLATFORM}")
message(STATUS "KERFUR_PERCEPTION_BACKEND=${KERFUR_PERCEPTION_BACKEND}")
message(STATUS "KERFUR_HAS_PERCEPTION=${KERFUR_HAS_PERCEPTION}")
