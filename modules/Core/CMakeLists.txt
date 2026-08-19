# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

set(CORE_MODULE Core)

qt_add_library(${CORE_MODULE})

qt_add_qml_module(
    ${CORE_MODULE}
    URI "Core"
    VERSION 1.0
    SOURCES src/queuestatssource.cpp
            src/queuestatssource.h
            src/boundedqueue.h
            src/perceptiontypes.cpp
            src/perceptiontypes.h
            src/peoplemodel.cpp
            src/peoplemodel.h
)

target_link_libraries(${CORE_MODULE} PUBLIC Qt6::Core Qt6::Qml)

# PUBLIC so that consumers and the test tree can include these headers by their
# bare name, which is how every file in this module refers to its neighbours.
target_include_directories(${CORE_MODULE} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
