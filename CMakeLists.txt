cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake")

project(
    kerfur
    VERSION ${PROJECT_VERSION}
    LANGUAGES CXX
)

find_package(
    Qt6 6.12 REQUIRED
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

# At the top so that the executables, which link the static modules, see the
# imported targets those modules link. Each module still finds its own subset.
find_package(
    OpenCV 4.8 REQUIRED
    COMPONENTS calib3d
               core
               dnn
               imgproc
               objdetect
)
find_package(whisper CONFIG REQUIRED)
find_package(ggml CONFIG REQUIRED)

# Applies to the project's own targets; the vcpkg ports keep their own level.
if (EMSCRIPTEN)
    add_compile_options("$<$<CONFIG:MinSizeRel>:-Oz>" "$<$<CONFIG:MinSizeRel>:-flto>")
    add_link_options("$<$<CONFIG:MinSizeRel>:-flto>")
endif ()

qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)
qt_policy(SET QTP0005 NEW)

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
                 KerfurCore
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
           KerfurCore
           Vision
           Tools
           Display
           Hearing
           Skills
)

# The modules are static libraries, so each QML plugin is linked in too; its
# init object imports the plugin, which registers the module's types.
target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE KerfurCoreplugin
            Visionplugin
            Toolsplugin
            Displayplugin
            Hearingplugin
            Skillsplugin
)

# The web models are packed by cmake/pack-web-models.sh and unpacked into
# /models by the page, which skips them for a light session.
if (EMSCRIPTEN)
    # Skill files are read at run time from the virtual file system.
    target_link_options(
        ${PROJECT_NAME}
        PRIVATE "SHELL:--embed-file ${CMAKE_SOURCE_DIR}/skills@/skills"
    )

    # IndexedDB backs the directory people enrolled at run time are kept in.
    target_link_options(${PROJECT_NAME} PRIVATE -lidbfs.js)
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
