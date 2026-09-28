cmake_minimum_required(VERSION 3.10)

project(ocr-extractor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(OCREXTRACTOR_FILES
    ocrextractor.cpp
    ocrextractor.h
    ocrextractorplugin.cpp
    ocrextractorplugin.h
    ocrextractor.json
)

set(BIN_NAME ${PROJECT_NAME})

set(OCREXTRACTOR_COMMON_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../abstractextractorplugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../extractor_plugin_global.h
)

find_package(Qt6 COMPONENTS Core Gui REQUIRED)
find_package(Dtk6 COMPONENTS Core REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DtkOcr REQUIRED IMPORTED_TARGET dtk6ocr)

add_library(${BIN_NAME}
    SHARED
    ${OCREXTRACTOR_FILES}
    ${OCREXTRACTOR_COMMON_HEADERS}
)

target_link_libraries(${BIN_NAME}
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Dtk6::Core
        PkgConfig::DtkOcr
)

target_include_directories(${BIN_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_options(${BIN_NAME} PRIVATE -Wall -Wextra)

install(TARGETS ${BIN_NAME}
    LIBRARY
    DESTINATION ${ANYTHING_EXTRACTOR_PLUGIN_DIR}
)

install(FILES ocrextractor.json
    DESTINATION ${ANYTHING_EXTRACTOR_PLUGIN_DIR}
)

message(STATUS "ocr-extractor plugin configured")
