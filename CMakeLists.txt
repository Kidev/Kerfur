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
               Multimedia
               MultimediaWidgets
               QuickControls2
)

qt_standard_project_setup()
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

qt_add_qml_module(
    ${PROJECT_NAME}
    URI "qml"
    VERSION 1.0
    QML_FILES ${SOURCES_QML} ${SOURCES_QML_SINGLETON}
    DEPENDENCIES TARGET
                 Vision
                 Tools
                 Display
                 Hearing
    RESOURCES ${RESOURCES_FILE}
)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC Qt::Core
           Qt::Gui
           Qt::Quick
           Qt::Qml
           Qt::Concurrent
           Qt::Multimedia
           Qt::MultimediaWidgets
           Qt::QuickControls2
           ${OpenCV_LIBS}
           Vision
           Tools
           Display
           Hearing
)

target_include_directories(${PROJECT_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})

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
