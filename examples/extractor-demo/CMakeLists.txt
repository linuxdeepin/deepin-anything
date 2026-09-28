cmake_minimum_required(VERSION 3.10)

project(extractor-demo)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Widgets REQUIRED)
find_package(Dtk6 COMPONENTS Core Widget REQUIRED)

# Standalone demo: pull in the extractor communication library from the main
# source tree (this project is not part of the main build tree).
set(EXTRACTOR_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/extractor/libextractor)
add_subdirectory(${EXTRACTOR_LIB_DIR} ${CMAKE_CURRENT_BINARY_DIR}/libextractor)

set(DEMO_FILES
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

add_executable(${PROJECT_NAME}
    ${DEMO_FILES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Dtk6::Core
    Dtk6::Widget
    deepin-anything-extractor-lib
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ANYTHING_EXTRACTOR_TOOL="/usr/libexec/deepin-anything-extractor"
)

message(STATUS "extractor-demo configured (standalone project, not part of the main build tree)")
