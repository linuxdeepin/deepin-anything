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

# Found all source files
file(GLOB_RECURSE SOURCE_FILES "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.c")
list(LENGTH SOURCE_FILES SRC_FILES_SIZE)
message(STATUS "Found ${SRC_FILES_SIZE} source files of deepin-server")
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
install(FILES deepin-anything-server.service DESTINATION lib/systemd/system)
