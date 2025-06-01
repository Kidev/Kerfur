set(TOOLS_MODULE Tools)

qt_add_library(${TOOLS_MODULE})

qt_add_qml_module(
    ${TOOLS_MODULE}
    URI "Tools"
    QML_FILES qml/ControlPanel.qml qml/KSettings.qml qml/KSlider.qml
)
