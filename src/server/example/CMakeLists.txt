# SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(example_receiver example_receiver.c)

target_include_directories(example_receiver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dispatcher
    ${CMAKE_CURRENT_SOURCE_DIR}/../../kernelmod
    ${GLIB_INCLUDE_DIRS}
)

target_link_libraries(example_receiver PRIVATE
    deepin-anything-dispatcher
    ${GLIB_LIBRARIES}
)
