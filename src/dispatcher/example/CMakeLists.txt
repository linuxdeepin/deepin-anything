# SPDX-FileCopyrightText: 2025 Uniontech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(example_dispatch example_dispatch.c)
target_link_libraries(example_dispatch PRIVATE
    deepin-anything-dispatcher
    ${GLIB_LIBRARIES}
)
target_include_directories(example_dispatch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GLIB_INCLUDE_DIRS}
)
