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

# The CPU perception suites. They exercise the decode arithmetic, the tracker
# and the orientation recovery, none of which need a camera, a model file or an
# accelerator, which is what makes them runnable in CI.
if (KERFUR_PERCEPTION_BACKEND STREQUAL "cpu")
    kerfur_add_test(visiondecode Vision)
    kerfur_add_test(persontracker Vision)
    kerfur_add_test(orientation Vision)
    kerfur_add_test(modelcatalog Vision)
    kerfur_add_test(identitystore Vision)

    # Vision keeps OpenCV private, so neither its headers nor its libraries
    # reach a consumer. The suites build cv::Mat inputs of their own, so they
    # need both, and say so here rather than the module widening its interface
    # for the benefit of its tests. dnn as well as core, because FacePipeline
    # holds cv::dnn::Net members by value and tst_identitystore builds one, so
    # the inline destructor lands in the suite rather than in libVision.
    find_package(OpenCV 4.8 REQUIRED COMPONENTS core dnn)
    foreach (SUITE visiondecode persontracker orientation modelcatalog identitystore)
        target_include_directories(tst_${SUITE} PRIVATE ${OpenCV_INCLUDE_DIRS})
        target_link_libraries(tst_${SUITE} PRIVATE ${OpenCV_LIBS})
    endforeach ()

    target_compile_definitions(
        tst_modelcatalog PRIVATE KERFUR_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
endif ()
