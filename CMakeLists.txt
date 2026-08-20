cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake")

project(
    kerfur
    VERSION ${PROJECT_VERSION}
    LANGUAGES CXX
)

find_package(
    Qt6 REQUIRED
    COMPONENTS Core
               Gui
               Quick
               Qml
               Concurrent
               ShaderTools
               Test
               Multimedia
               MultimediaWidgets
               QuickControls2
)

qt_standard_project_setup()
if (QT_KNOWN_POLICY_QTP0001)
    qt_policy(SET QTP0001 NEW)
endif ()
if (QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif ()
if (QT_KNOWN_POLICY_QTP0005)
    qt_policy(SET QTP0005 NEW)
endif ()

qt_add_executable(
    ${PROJECT_NAME}
    ${SOURCES_CPP}
    ${SOURCES_EXTRA}
    ${MISC_EXTRA_FILES}
)

qt_add_shaders(
    ${PROJECT_NAME} ${PROJECT_NAME}_shaders
    FILES ${SOURCES_SHADERS}
    BATCHABLE PRECOMPILE OPTIMIZED
    PREFIX "/"
)

add_subdirectory(modules)

include(CTest)
if (BUILD_TESTING AND NOT EMSCRIPTEN)
    add_subdirectory(tests)
endif ()

qt_add_qml_module(
    ${PROJECT_NAME}
    URI "qml"
    VERSION 1.0
    QML_FILES ${SOURCES_QML} ${SOURCES_QML_SINGLETON}
    DEPENDENCIES TARGET
                 Core
                 Vision
                 Tools
                 Display
                 Hearing
                 Skills
    RESOURCES ${RESOURCES_FILE}
)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC Qt6::Core
           Qt6::Gui
           Qt6::Quick
           Qt6::Qml
           Qt6::Concurrent
           Qt6::Multimedia
           Qt6::MultimediaWidgets
           Qt6::QuickControls2
           Core
           Vision
           Tools
           Display
           Hearing
           Skills
)

# WebAssembly has no filesystem to provision models into, so they are baked
# into the Emscripten virtual one at link time and ModelCatalog looks there.
# Absent at configure time means a build with no perception rather than a build
# failure, which is the state a fresh clone that has not run `make models` is in.
if (EMSCRIPTEN)
    if (EXISTS "${CMAKE_SOURCE_DIR}/models")
        target_link_options(
            ${PROJECT_NAME}
            PRIVATE "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/models@/models"
        )
        message(STATUS "Preloading models from ${CMAKE_SOURCE_DIR}/models")
    else ()
        message(WARNING "No models directory; the web build will start without perception")
    endif ()
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/compilers.cmake")

install(
    TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_qml_app_script(
    TARGET ${PROJECT_NAME}
    OUTPUT_SCRIPT deploy_script
    MACOS_BUNDLE_POST_BUILD
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
    NO_TRANSLATIONS
)

install(SCRIPT ${deploy_script})
