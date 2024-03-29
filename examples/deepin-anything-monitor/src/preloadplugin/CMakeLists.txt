# SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.1)

set(PRELOAD_LIB_NAME dtkqml-${CMAKE_PROJECT_NAME}-Preload)

###! 请确保 preloadplugin 下仅依赖最少的三方内容，
###! 且不做额外的复杂操作，将第三方库和自定义类型放在
###! maincomponentplugin 中，保证程序能够快速启动。
# Set sources
set(PRELOAD_SRC
    preloadplugin.cpp
    )

# Find the Qt5Quick library
find_package(Qt5Quick CONFIG REQUIRED)
find_package(Qt5QuickCompiler REQUIRED)

qtquick_compiler_add_resources(PRELOAD_RCC_SOURCES preload.qrc)

# 保证 preloadplugin 目录下头文件全局可见
include_directories(src/preloadplugin)

# Add LIB
add_library(${PRELOAD_LIB_NAME} SHARED
    ${PRELOAD_SRC}
    ${PRELOAD_RCC_SOURCES}
    )

# 由于 LIB 程序为本项目的功能实现部分，需要用到的第三方库都需要在此处添加。
target_link_libraries(${PRELOAD_LIB_NAME}
    Qt5::Quick
    ${DtkDeclarative_LIBRARIES}
    )

# Add install rule
install(TARGETS ${PRELOAD_LIB_NAME} DESTINATION ${DTK_QML_APP_PLUGIN_PATH})
