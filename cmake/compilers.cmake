if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Werror -Wpedantic)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Werror -Wpedantic)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
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

if (UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE QT_MULTIMEDIA_LIB)

    target_compile_definitions(${PROJECT_NAME} PRIVATE QT_FEATURE_linux_v4l=1)

    set_target_properties(
        ${PROJECT_NAME}
        PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE
                   INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib:${QT_INSTALL_DIR}/lib"
    )
endif ()
