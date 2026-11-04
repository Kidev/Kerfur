# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

# Qt's resource helpers special-case a target named Core as QtCore and skip the
# link to its init objects, so the target keeps a prefix the URI does not.
set(CORE_MODULE KerfurCore)

qt_add_library(${CORE_MODULE} STATIC)

qt_add_qml_module(
    ${CORE_MODULE}
    URI "Core"
    VERSION 1.0
    SOURCES src/queuestatssource.cpp
            src/queuestatssource.h
            src/modeldirectory.cpp
            src/modeldirectory.h
            src/identitydirectory.cpp
            src/identitydirectory.h
            src/identityregistry.cpp
            src/identityregistry.h
            src/identitylibrary.cpp
            src/identitylibrary.h
            src/wavfile.cpp
            src/wavfile.h
            src/assetsearch.cpp
            src/assetsearch.h
            src/capturepermission.cpp
            src/capturepermission.h
            src/settingspath.cpp
            src/settingspath.h
            src/boundedqueue.h
            src/perceptiontypes.cpp
            src/perceptiontypes.h
            src/peoplemodel.cpp
            src/peoplemodel.h
            src/worldmodel.cpp
            src/worldmodel.h
            src/perceptionpipe.cpp
            src/perceptionpipe.h
            src/resourcemonitor.cpp
            src/resourcemonitor.h
)

target_link_libraries(${CORE_MODULE} PUBLIC Qt6::Core Qt6::Gui Qt6::Qml)

# The browser's file picker is reached through QFileDialog::getOpenFileContent,
# which on the web build needs no QApplication.
if (EMSCRIPTEN)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    target_link_libraries(${CORE_MODULE} PRIVATE Qt6::Widgets)
endif ()

# PUBLIC so that consumers and the test tree can include these headers by their
# bare name, which is how every file in this module refers to its neighbours.
target_include_directories(${CORE_MODULE} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
