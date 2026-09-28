cmake_minimum_required(VERSION 3.10)

project(deepin-anything-extractor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(GNUInstallDirs)

set(ANYTHING_EXTRACTOR_PLUGIN_DIR ${CMAKE_INSTALL_FULL_LIBDIR}/deepin-anything/extractor/plugins)
add_definitions(-DANYTHING_EXTRACTOR_PLUGIN_DIR="${ANYTHING_EXTRACTOR_PLUGIN_DIR}")

add_subdirectory(libextractor)
add_subdirectory(extractor)
add_subdirectory(plugins)

message(STATUS "deepin-anything-extractor configured (plugins: ${ANYTHING_EXTRACTOR_PLUGIN_DIR})")
