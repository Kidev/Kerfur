set(DISPLAY_MODULE Display)

qt_add_library(${DISPLAY_MODULE} STATIC)

qt_add_qml_module(
    ${DISPLAY_MODULE}
    URI "Display"
    QML_FILES qml/BlurHelper.qml
              qml/Glow.qml
              qml/LedScreen.qml
              qml/Face.qml
    SOURCES src/texturewatch.cpp
            src/texturewatch.h
    DEPENDENCIES TARGET KerfurCore Tools Vision Skills
    # The application's own module, `qml`, which holds KSettings.
    IMPORT_PATH ${CMAKE_BINARY_DIR}
)

# Qt6::Quick is PUBLIC because texturewatch.h names QQuickItem in its interface.
target_link_libraries(${DISPLAY_MODULE} PUBLIC Qt6::Quick PRIVATE KerfurCore Skills)
target_include_directories(${DISPLAY_MODULE} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
