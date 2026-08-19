# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

set(VISION_MODULE Vision)

qt_add_library(${VISION_MODULE})

# The CPU perception graph. Built wherever the backend is `cpu`, which is every
# platform that is not the Pi, plus the Pi itself when its accelerator is not
# present. Model rationale is in
# docs/superpowers/specs/2026-08-19-cpu-perception-backends.md.
set(VISION_BACKEND_SOURCES "")
if (KERFUR_PERCEPTION_BACKEND STREQUAL "cpu")
    set(
        VISION_BACKEND_SOURCES
        src/visiontypes.h
        src/visiontypes.cpp
        src/visionconfig.h
        src/modelcatalog.h
        src/modelcatalog.cpp
        src/posedetector.h
        src/posedetector.cpp
        src/objectdetector.h
        src/objectdetector.cpp
        src/facepipeline.h
        src/facepipeline.cpp
        src/identitystore.h
        src/identitystore.cpp
        src/persontracker.h
        src/persontracker.cpp
        src/orientationresolver.h
        src/orientationresolver.cpp
        src/cpuinferencebackend.h
        src/cpuinferencebackend.cpp
        src/cameraframesource.h
        src/cameraframesource.cpp
        src/visioninferenceworker.h
        src/visioninferenceworker.cpp
        src/visionproducer.h
        src/visionproducer.cpp
    )
endif ()

# FaceTracker and AIFaceDetector are the stack this backend replaces. They stay
# until the QML rewiring in P1 tasks 11 and 12 lands, because Face.qml and
# ControlPanel.qml still instantiate them. Only one of the two stacks may be
# enabled at a time: both open a camera.
qt_add_qml_module(
    ${VISION_MODULE}
    URI "Vision"
    VERSION 1.0
    QML_FILES qml/CameraFeedDisplay.qml
    SOURCES src/FaceTracker.cpp
            src/FaceTracker.hpp
            src/AIFaceDetector.cpp
            src/AIFaceDetector.hpp
            ${VISION_BACKEND_SOURCES}
)

find_package(
    OpenCV 4.8 REQUIRED
    COMPONENTS calib3d
               core
               dnn
               imgcodecs
               imgproc
               objdetect
)

target_link_libraries(
    ${VISION_MODULE}
    PUBLIC Core
    PRIVATE ${OpenCV_LIBS} Qt6::Multimedia
)

# PUBLIC because main.cpp instantiates the producer: it is the application root
# that owns it, and no other module may reach it.
target_include_directories(${VISION_MODULE} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# PRIVATE, and it must stay that way: OpenCV is this module's business alone.
# The price is that visionproducer.h, the one header the application root
# includes, may not name a cv:: type. It holds its frame queue behind a pointer
# for exactly that reason.
target_include_directories(${VISION_MODULE} PRIVATE ${OpenCV_INCLUDE_DIRS})
