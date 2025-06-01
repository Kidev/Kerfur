set(VISION_MODULE Vision)

qt_add_library(${VISION_MODULE})

qt_add_qml_module(
    ${VISION_MODULE}
    URI "Vision"
    QML_FILES qml/CameraFeedDisplay.qml
    SOURCES src/FaceTracker.cpp src/FaceTracker.hpp
    RESOURCES opencv/haarcascade_frontalface_alt.xml
    RESOURCE_PREFIX "/"
)

target_link_libraries(${VISION_MODULE} PRIVATE ${OpenCV_LIBS} Qt6::Multimedia)
target_include_directories(${VISION_MODULE} PRIVATE src ${OpenCV_INCLUDE_DIRS})
