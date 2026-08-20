# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

# Turns tests/tst_<name>.cpp into a CTest case linked against the given
# libraries. Kept as a function so adding a suite is one line and nobody
# reinvents the target boilerplate per file.
function (kerfur_add_test NAME)
    qt_add_executable(tst_${NAME} tst_${NAME}.cpp)
    target_link_libraries(tst_${NAME} PRIVATE Qt6::Test ${ARGN})
    add_test(NAME ${NAME} COMMAND tst_${NAME})
    set_tests_properties(${NAME} PROPERTIES TIMEOUT 60)
endfunction ()

kerfur_add_test(harness)
kerfur_add_test(boundedqueue Core)
kerfur_add_test(perceptiontypes Core)
kerfur_add_test(peoplemodel Core)
kerfur_add_test(worldmodel Core)
kerfur_add_test(perceptionpipe Core)
kerfur_add_test(resourcemonitor Core)
kerfur_add_test(modeldirectory Core)
kerfur_add_test(settingspath Core)
kerfur_add_test(identitydirectory Core)

# Vision and Hearing both keep OpenCV private, so neither its headers nor its
# libraries reach a consumer. These suites build cv::Mat inputs of their own, so
# they need both, and say so here rather than the modules widening their
# interfaces for the benefit of their tests. dnn as well as core, because
# FacePipeline and SpeakerEmbedder hold cv::dnn::Net members by value, so their
# inline destructors land in the suite rather than in the library.
find_package(OpenCV 4.8 REQUIRED COMPONENTS core dnn)

# The perception stage suites. They exercise the decode arithmetic, the pose
# geometry, the gallery and the frontend, none of which need a camera, a
# microphone, a model file or an accelerator, which is what makes them runnable
# in CI.
set(VISION_SUITES visiondecode modelcatalog identitystore headpose)
foreach (SUITE ${VISION_SUITES})
    kerfur_add_test(${SUITE} Vision)
endforeach ()

# The Hearing suites reach into the module's own headers for the same reason,
# and Hearing declares that directory PRIVATE because nothing else in the
# application has any business in it.
set(HEARING_SUITES voiceactivity transcriptfilter filterbank)
foreach (SUITE ${HEARING_SUITES})
    kerfur_add_test(${SUITE} Hearing)
    target_include_directories(
        tst_${SUITE} PRIVATE ${CMAKE_SOURCE_DIR}/modules/Hearing/src
    )
endforeach ()

foreach (SUITE ${VISION_SUITES} ${HEARING_SUITES})
    target_include_directories(tst_${SUITE} PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(tst_${SUITE} PRIVATE ${OpenCV_LIBS})
endforeach ()

target_compile_definitions(tst_modelcatalog PRIVATE KERFUR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# The headless pipeline suites, which only exist where that pipeline is built.
if (KERFUR_PERCEPTION_BACKEND STREQUAL "cpu")
    kerfur_add_test(persontracker Vision)
    kerfur_add_test(orientation Vision)
    foreach (SUITE persontracker orientation)
        target_include_directories(tst_${SUITE} PRIVATE ${OpenCV_INCLUDE_DIRS})
        target_link_libraries(tst_${SUITE} PRIVATE ${OpenCV_LIBS})
    endforeach ()
endif ()
