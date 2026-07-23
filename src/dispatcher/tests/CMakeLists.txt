# SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.10)

add_executable(test_dispatcher test_dispatcher.c)
target_link_libraries(test_dispatcher PRIVATE
    deepin-anything-dispatcher
    ${GLIB_LIBRARIES}
)
target_include_directories(test_dispatcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GLIB_INCLUDE_DIRS}
)
add_test(NAME dispatcher COMMAND test_dispatcher)
