set(VISION_MODULE Vision)

qt_add_library(${VISION_MODULE})

qt_add_qml_module(
    ${VISION_MODULE}
    URI "Vision"
    SOURCES src/FaceTracker.cpp src/FaceTracker.hpp
    DEPENDENCIES TARGET Tools
)
