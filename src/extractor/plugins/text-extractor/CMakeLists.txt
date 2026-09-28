cmake_minimum_required(VERSION 3.10)

project(text-extractor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(TEXTEXTRACTOR_FILES
    textextractor.cpp
    textextractor.h
    textextractorplugin.cpp
    textextractorplugin.h
    textextractor.json
)

set(BIN_NAME ${PROJECT_NAME})

set(TEXTEXTRACTOR_COMMON_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../abstractextractorplugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../extractor_plugin_global.h
)

find_package(Qt6 COMPONENTS Core Gui REQUIRED)
find_package(Dtk6 COMPONENTS Core REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(Docparser REQUIRED IMPORTED_TARGET docparser)

add_library(${BIN_NAME}
    SHARED
    ${TEXTEXTRACTOR_FILES}
    ${TEXTEXTRACTOR_COMMON_HEADERS}
)

target_link_libraries(${BIN_NAME}
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Dtk6::Core
        PkgConfig::Docparser
)

target_include_directories(${BIN_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_options(${BIN_NAME} PRIVATE -Wall -Wextra)

# Install the plugin
install(TARGETS ${BIN_NAME}
    LIBRARY
    DESTINATION ${ANYTHING_EXTRACTOR_PLUGIN_DIR}
)

# Install metadata file
install(FILES textextractor.json
    DESTINATION ${ANYTHING_EXTRACTOR_PLUGIN_DIR}
)

message(STATUS "text-extractor plugin configured")
