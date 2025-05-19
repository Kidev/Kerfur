if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Werror -Wpedantic)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE TRUE)
    target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Werror -Wpedantic)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_options(${PROJECT_NAME} PUBLIC /W4 /WX)
else ()
    message(FATAL_ERROR "Unknown compiler")
endif ()

if (EMSCRIPTEN)
    set_target_properties(${PROJECT_NAME} PROPERTIES QT_WASM_INITIAL_MEMORY "128MB")
    target_compile_options(${PROJECT_NAME} PUBLIC -Os -DNDEBUG)
    target_link_options(
        ${PROJECT_NAME}
        PUBLIC
        -sASYNCIFY
        -Os
    )
    target_link_options(
        ${PROJECT_NAME}
        PRIVATE
        -s
        FULL_ES3=1
    )
endif ()
