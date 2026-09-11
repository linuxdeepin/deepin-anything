# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.10)

# Unit tests for the daemon trust client. The production source is compiled
# directly into the test binary (same pattern as src/server/tests); the test
# framework is GLib g_test only. Qt/GLIB/GIO variables and QT_VERSION_MAJOR
# are inherited from src/daemon/CMakeLists.txt.
add_executable(test_trust_client
    test_trust_client.cpp
    ../src/core/trust_client.cpp
)

target_include_directories(test_trust_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${GLIB_INCLUDE_DIRS}
    ${GIO_INCLUDE_DIRS}
)

target_link_libraries(test_trust_client PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    spdlog::spdlog
    pthread
)

add_test(NAME trust-client COMMAND test_trust_client)
