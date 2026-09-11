# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.10)

add_executable(test_event_relay_dispatcher
    test_event_relay_dispatcher.c
    ../event-relay-dispatcher.c
)
target_link_libraries(test_event_relay_dispatcher PRIVATE
    deepin-anything-dispatcher
    ${GLIB_LIBRARIES}
)
target_include_directories(test_event_relay_dispatcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GLIB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dispatcher
)
add_test(NAME server-relay-dispatcher COMMAND test_event_relay_dispatcher)
