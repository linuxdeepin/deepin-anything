cmake_minimum_required(VERSION 3.1.0)

# ---------------------------------------------------------------------------------------
# Start deepin-anything-server project
# ---------------------------------------------------------------------------------------
project(
    deepin-anything-server
    VERSION 1.0.0
    DESCRIPTION "A file search engine"
    LANGUAGES CXX
)

# Find the dependencies
find_library(LUCENEPP_LIB lucene++)
find_library(LUCENEPP_CONTRIB_LIB lucene++-contrib)
find_path(LUCENEPP_INCLUDE_DIR lucene++)
find_package(fmt REQUIRED)
find_package(Qt5 CONFIG REQUIRED Core DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GNL REQUIRED libnl-3.0 libnl-genl-3.0)
pkg_check_modules(MOUNT REQUIRED mount IMPORTED_TARGET)

# Print dependencies
message(STATUS "Found Lucene++ library: " ${LUCENEPP_INCLUDE_DIR})
message(STATUS "Found fmtlib: ${fmt_FOUND}")

# Found all source files
file(GLOB_RECURSE SOURCE_FILES "${PROJECT_SOURCE_DIR}/src/*.cpp")
list(LENGTH SOURCE_FILES SRC_FILES_SIZE)
message(STATUS "Found ${SRC_FILES_SIZE} source files of okec")
foreach(source_file ${SOURCE_FILES})
    message(STATUS "  ${source_file}")
endforeach()

# Define a shared library target named `deepin-anything-server`
add_library(${PROJECT_NAME} SHARED)

target_sources(${PROJECT_NAME} PRIVATE ${SOURCE_FILES})


target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../kernelmod>
    ${LUCENEPP_INCLUDE_DIR}
    ${GNL_INCLUDE_DIRS}
    ${MOUNT_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    ${LUCENEPP_LIB}
    ${LUCENEPP_CONTRIB_LIB}
    ${GNL_LIBRARIES}
    ${MOUNT_LIBRARIES}
    fmt::fmt
    Qt5::DBus
    pthread
    stdc++fs
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Werror -Wextra)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
        EXPORT "${PROJECT_NAME}-targets"
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT "${PROJECT_NAME}-targets"
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/cmake
)

include(CMakePackageConfigHelpers)
set(LIB_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME})
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/cmake
    PATH_VARS LIB_INSTALL_DIR
)

file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake" "include(CMakeFindDependencyMacro)\nfind_dependency(fmt)\nfind_dependency(Qt5 REQUIRED Core DBus)")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/cmake
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${PROJECT_NAME}-config-version.cmake"
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/cmake
)