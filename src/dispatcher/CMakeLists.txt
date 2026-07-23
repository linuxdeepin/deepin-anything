# SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.10)

project(deepin-anything-dispatcher C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)

add_library(deepin-anything-dispatcher STATIC
    event_dispatcher.c
    event_receiver.c
)

target_include_directories(deepin-anything-dispatcher PUBLIC
    ${GLIB_INCLUDE_DIRS}
)

target_link_libraries(deepin-anything-dispatcher PUBLIC
    ${GLIB_LIBRARIES}
)

# Build tests if enabled
option(ENABLE_TESTING "Enable testing" OFF)
if(ENABLE_TESTING)
    enable_testing()
    add_subdirectory(tests)
    add_custom_target(check-dispatcher
        COMMAND ${CMAKE_CTEST_COMMAND} --verbose -R dispatcher
        COMMENT "Running dispatcher tests"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

# Build example if enabled
option(BUILD_DISPATCHER_EXAMPLE "Build dispatcher end-to-end example" OFF)
if(BUILD_DISPATCHER_EXAMPLE)
    add_subdirectory(example)
endif()
