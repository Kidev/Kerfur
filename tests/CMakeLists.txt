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
