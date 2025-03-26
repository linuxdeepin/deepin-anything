cmake_minimum_required(VERSION 3.11)

project(anything)

# Version
if (NOT PROJECT_VERSION)
    set(PROJECT_VERSION "1.0.0")
endif()

if (NOT PROJECT_VERSION_MAJOR)
    set(PROJECT_VERSION_MAJOR 0)
endif()

# Setup the environment
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(LIBPCRE REQUIRED libpcre)

set(3RD_DIR "../../3rdparty")

FILE(GLOB SRC_FILES
    "src/*.c"
    "src/index/*.c"
    "src/chinese/*.c"
    "${3RD_DIR}/fsearch/*.c"
)


# Build
add_library(${PROJECT_NAME} SHARED
    ${SRC_FILES}
)

set_target_properties(
    ${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_link_libraries(
    ${PROJECT_NAME}
    ${GLIB_LIBRARIES}
    ${LIBPCRE_LIBRARIES}
)

target_include_directories(
    ${PROJECT_NAME}
PUBLIC
    ${GLIB_INCLUDE_DIRS}
    ${LIBPCRE_INCLUDE_DIRS}
    ${CMAKE_CURRENT_LIST_DIR}/inc
    ${CMAKE_CURRENT_LIST_DIR}/inc/index
    ${3RD_DIR}/fsearch
)

include(GNUInstallDirs)
install(DIRECTORY
    ${PROJECT_SOURCE_DIR}/inc/
    DESTINATION include/deepin-anything
    FILES_MATCHING PATTERN "*.h"
)

install_targets ("/${CMAKE_INSTALL_LIBDIR}" ${PROJECT_NAME})
