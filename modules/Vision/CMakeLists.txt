# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

set(VISION_MODULE Vision)

qt_add_library(${VISION_MODULE} STATIC)

# The perception stages: the model wrappers themselves. Always built, because
# the camera view in the control panel runs them directly and is not part of the
# headless pipeline below. Model rationale is in
# docs/superpowers/specs/2026-08-19-cpu-perception-backends.md.
set(
    VISION_STAGE_SOURCES
    src/visiontypes.h
    src/visiontypes.cpp
    src/visionconfig.h
    src/modelcatalog.h
    src/modelcatalog.cpp
    src/posedetector.h
    src/posedetector.cpp
    src/objectdetector.h
    src/objectdetector.cpp
    src/handlandmarker.h
    src/handlandmarker.cpp
    src/facepipeline.h
    src/facepipeline.cpp
    src/identitystore.h
    src/identitystore.cpp
    src/headpose.h
    src/headpose.cpp
    src/faceanalysis.h
    src/faceanalysis.cpp
    src/faceanalysisworker.h
    src/faceanalysisworker.cpp
)

# The headless graph that feeds WorldModel, on its own capture thread. Built
# wherever the backend is `cpu`, which is every platform that is not the Pi,
# plus the Pi itself when its accelerator is not present.
set(VISION_BACKEND_SOURCES "")
if (KERFUR_PERCEPTION_BACKEND STREQUAL "cpu")
    set(
        VISION_BACKEND_SOURCES
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

# FaceTracker is the camera the control panel and Face.qml still drive. It runs
# the stages above on its own worker thread; the producer below runs the same
# stages headless. Only one of the two may be enabled at a time: both open a
# camera.
qt_add_qml_module(
    ${VISION_MODULE}
    URI "Vision"
    VERSION 1.0
    QML_FILES qml/CameraFeedDisplay.qml
    SOURCES src/FaceTracker.cpp
            src/FaceTracker.hpp
            src/cameraframeview.cpp
            src/cameraframeview.h
            ${VISION_STAGE_SOURCES}
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

# Qt6::Gui is PUBLIC because faceanalysisworker.h is: it sits in the PUBLIC
# include directory below and names QImage in its interface, so anything that
# includes it needs Gui too. It used to arrive by accident through Multimedia,
# which is PRIVATE, so the module compiled and its consumers did not.
# Qt6::Quick for the same reason Qt6::Gui is PUBLIC: cameraframeview.h sits in
# the PUBLIC include directory below and names QQuickItem in its interface.
target_link_libraries(
    ${VISION_MODULE}
    PUBLIC KerfurCore Qt6::Gui Qt6::Quick
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
