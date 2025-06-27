cmake_minimum_required(VERSION 3.1)

# ---------------------------------------------------------------------------------------
# Start deepin-anything-daemon project
# ---------------------------------------------------------------------------------------
project(
    deepin-anything-daemon
    VERSION 1.0.0
    DESCRIPTION "A file search engine"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------------------
# Parse version from debian/changelog
# ---------------------------------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/debian/changelog")
    file(READ "${CMAKE_SOURCE_DIR}/debian/changelog" CHANGELOG_CONTENT)
    string(REGEX MATCH "deepin-anything \\(([^)]+)\\)" VERSION_MATCH "${CHANGELOG_CONTENT}")
    if(VERSION_MATCH)
        string(REGEX REPLACE "deepin-anything \\(([^)]+)\\)" "\\1" DEEPIN_ANYTHING_VERSION "${VERSION_MATCH}")
        message(STATUS "Found version from debian/changelog: ${DEEPIN_ANYTHING_VERSION}")
        # Define the version macro
        add_compile_definitions(DEEPIN_ANYTHING_VERSION="${DEEPIN_ANYTHING_VERSION}")
    else()
        message(WARNING "Failed to parse version from debian/changelog")
    endif()
else()
    message(WARNING "debian/changelog not found, version macro will not be defined")
endif()

# ---------------------------------------------------------------------------------------
# Get git commit hash
# ---------------------------------------------------------------------------------------
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE DEEPIN_ANYTHING_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(DEEPIN_ANYTHING_COMMIT_HASH)
        message(STATUS "Found git commit hash: ${DEEPIN_ANYTHING_COMMIT_HASH}")
        # Define the commit hash macro
        add_compile_definitions(DEEPIN_ANYTHING_COMMIT_HASH="${DEEPIN_ANYTHING_COMMIT_HASH}")
    else()
        message(WARNING "Failed to get git commit hash")
    endif()
else()
    if(NOT GIT_FOUND)
        message(WARNING "Git not found, commit hash macro will not be defined")
    else()
        message(WARNING ".git directory not found, commit hash macro will not be defined")
    endif()
endif()

# Find the dependencies
find_package(QT NAMES Qt6 Qt5 CONFIG REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} CONFIG REQUIRED Core)
find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(spdlog REQUIRED)
pkg_check_modules(GNL REQUIRED libnl-3.0 libnl-genl-3.0)
pkg_check_modules(MOUNT REQUIRED mount IMPORTED_TARGET)
pkg_check_modules(LUCENE REQUIRED liblucene++)
pkg_check_modules(LUCENE_CONTRIB REQUIRED liblucene++-contrib)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(GMODULE REQUIRED gmodule-2.0)

# Found all source files
file(GLOB_RECURSE SOURCE_FILES "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.c")
list(LENGTH SOURCE_FILES SRC_FILES_SIZE)
message(STATUS "Found ${SRC_FILES_SIZE} source files of deepin-anything-daemon")
foreach(source_file ${SOURCE_FILES})
    message(STATUS "  ${source_file}")
endforeach()

# Define a shared library
add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../kernelmod>
    # $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    ${LUCENE_INCLUDE_DIRS}
    ${GNL_INCLUDE_DIRS}
    ${MOUNT_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${GIO_INCLUDE_DIRS}
    ${GMODULE_INCLUDE_DIRS}
    ${Qt${QT_VERSION_MAJOR}Core_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    ${LUCENE_LIBRARIES}
    ${LUCENE_CONTRIB_LIBRARIES}
    ${GNL_LIBRARIES}
    ${MOUNT_LIBRARIES}
    ${Boost_LIBRARIES}
    pthread
    stdc++fs
    spdlog::spdlog
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    ${GMODULE_LIBRARIES}
    Qt${QT_VERSION_MAJOR}::Core
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Werror -Wextra)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

configure_file ("${PROJECT_NAME}.pc.in" "${PROJECT_NAME}.pc" @ONLY)
install(TARGETS ${PROJECT_NAME}
    COMPONENT linapp
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    DESTINATION lib
)

install(PROGRAMS deepin-anything-server DESTINATION bin)
install(FILES deepin-anything-daemon.service DESTINATION lib/systemd/user)
# installed by debhelper
# install(FILES deepin-anything-server.service DESTINATION lib/systemd/system)

# Install dconfig meta file
install(FILES ${CMAKE_SOURCE_DIR}/assets/org.deepin.anything.json DESTINATION share/dsg/configs/org.deepin.anything)
