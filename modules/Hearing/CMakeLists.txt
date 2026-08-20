# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

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
            src/voiceactivity.h
            src/voiceactivity.cpp
            src/transcriptfilter.h
            src/transcriptfilter.cpp
            src/filterbank.h
            src/filterbank.cpp
            src/audiofile.h
            src/audiofile.cpp
            src/speakerembedder.h
            src/speakerembedder.cpp
            src/voicegallery.h
            src/voicegallery.cpp
            src/voiceidentifier.h
            src/voiceidentifier.cpp
)

find_package(whisper CONFIG REQUIRED)
find_package(ggml CONFIG REQUIRED)

# The speaker network runs through OpenCV's DNN module, which this module
# already needs nothing else from. dnn and core only: no image decoding, no
# video, because the only tensors here came from a microphone.
find_package(OpenCV 4.8 REQUIRED COMPONENTS core dnn)

target_link_libraries(
    ${HEARING_MODULE}
    PRIVATE Qt6::Multimedia
            Qt6::Core
            Core
            ${OpenCV_LIBS}
            whisper
            ggml::all
            ggml::ggml
            ggml::ggml-base
)

target_include_directories(${HEARING_MODULE} PRIVATE src ${OpenCV_INCLUDE_DIRS})
