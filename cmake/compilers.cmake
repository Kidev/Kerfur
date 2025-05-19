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
    # target_link_options( ${PROJECT_NAME} PRIVATE -sUSE_WEBGL2=1 -sMAX_WEBGL_VERSION=2
    # -sMIN_WEBGL_VERSION=2 -sUSE_PTHREADS=1 -sALLOW_MEMORY_GROWTH=1 -sOFFSCREENCANVAS_SUPPORT=1
    # -sUSE_GLFW=3 -sFULL_ES2=1 -sASYNCIFY -Os -sLIBRARY_DEBUG=1 -sSYSCALL_DEBUG=1 # -sFS_LOG=1
    # -sSOCKET_DEBUG )
endif ()
