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
               QuickControls2
)

qt_standard_project_setup(REQUIRES 6.5)
if (QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif ()
if (QT_KNOWN_POLICY_QTP0005)
    qt_policy(SET QTP0005 NEW)
endif ()

qt_add_executable(${PROJECT_NAME} ${SOURCES_CPP} ${SOURCES_EXTRA})

qt_add_shaders(
    ${PROJECT_NAME} ${PROJECT_NAME}_shaders
    FILES ${SOURCES_SHADERS}
    BATCHABLE PRECOMPILE OPTIMIZED
    PREFIX "/"
)

add_subdirectory(qml)

qt_add_qml_module(
    ${PROJECT_NAME}
    URI "qml"
    QML_FILES qml/Main.qml
    DEPENDENCIES TARGET Tools Display
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
           Qt6::QuickControls2
           Tools
           Display
)

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
