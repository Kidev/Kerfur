set(DISPLAY_MODULE Display)

qt_add_library(${DISPLAY_MODULE})

qt_add_qml_module(
    ${DISPLAY_MODULE}
    URI "Display"
    QML_FILES qml/BlurHelper.qml
              qml/Glow.qml
              qml/LedScreen.qml
              qml/Face.qml
    DEPENDENCIES TARGET Tools Vision
)
