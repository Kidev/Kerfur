set(HEARING_MODULE Hearing)

qt_add_library(${HEARING_MODULE})

qt_add_qml_module(
    ${HEARING_MODULE}
    URI "Hearing"
    QML_FILES qml/SpeechDisplay.qml
    SOURCES src/SpeechRecognizer.cpp
            src/SpeechRecognizer.hpp
            src/WhisperEngine.cpp
            src/WhisperEngine.hpp
)

find_package(whisper CONFIG REQUIRED)
find_package(ggml CONFIG REQUIRED)

target_link_libraries(${HEARING_MODULE}
    PRIVATE
        Qt6::Multimedia
        Qt6::Core
        whisper
        ggml::all ggml::ggml ggml::ggml-base
)

target_include_directories(${HEARING_MODULE}
    PRIVATE src
)

if(whisper_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/models/ggml-tiny.en.bin")
    file(COPY "${CMAKE_SOURCE_DIR}/models/ggml-tiny.en.bin"
         DESTINATION "${CMAKE_BINARY_DIR}/models/")
endif()
