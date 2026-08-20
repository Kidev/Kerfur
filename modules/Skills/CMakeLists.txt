# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

set(SKILLS_MODULE Skills)

qt_add_library(${SKILLS_MODULE})

# No OpenCV, no multimedia, no camera and no microphone. This module turns a
# sentence somebody already transcribed into a decision, and the narrowness of
# that dependency list is what lets a skill file be reloaded at run time without
# any of the perception stack noticing.
qt_add_qml_module(
    ${SKILLS_MODULE}
    URI "Skills"
    VERSION 1.0
    QML_FILES qml/SkillsDisplay.qml
    SOURCES src/intent.h
            src/intent.cpp
            src/languagebackend.h
            src/languagebackend.cpp
            src/blackboard.h
            src/blackboard.cpp
            src/skill.h
            src/skill.cpp
            src/skillregistry.h
            src/skillregistry.cpp
)

target_link_libraries(${SKILLS_MODULE} PUBLIC Core Qt6::Core Qt6::Qml)

target_include_directories(${SKILLS_MODULE} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
