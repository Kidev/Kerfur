add_subdirectory(Tools)
add_subdirectory(Vision)
add_subdirectory(Display)

target_include_directories(${PROJECT_NAME} PRIVATE Vision/src)
