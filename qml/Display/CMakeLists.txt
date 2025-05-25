set(DISPLAY_MODULE Display)

qt_add_library(${DISPLAY_MODULE})

qt_add_qml_module(
    ${DISPLAY_MODULE}
    URI "qml.Display"
    QML_FILES BlurHelper.qml Glow.qml LedScreen.qml
    DEPENDENCIES TARGET
)
