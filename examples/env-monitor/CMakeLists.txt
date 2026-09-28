cmake_minimum_required(VERSION 3.10)

project(env-monitor)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 COMPONENTS Core Widgets DBus REQUIRED)
find_package(Dtk6 COMPONENTS Core Widget REQUIRED)
find_package(dfm6-search REQUIRED)
find_package(KF6IdleTime REQUIRED)

set(DEMO_FILES
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

# The env module (EnvDetector/PowerMonitor/IdleMonitor/LoadMonitor) and
# TextIndexConfig are compiled in from the main source tree; this project is
# not part of the main build tree.
set(INDEX_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/index)
set(ENV_SOURCES
    ${INDEX_SRC_DIR}/env/envdetector.cpp
    ${INDEX_SRC_DIR}/env/idlemonitor.cpp
    ${INDEX_SRC_DIR}/env/loadmonitor.cpp
    ${INDEX_SRC_DIR}/env/powermonitor.cpp
    ${INDEX_SRC_DIR}/utils/textindexconfig.cpp
)

add_executable(${PROJECT_NAME}
    ${DEMO_FILES}
    ${ENV_SOURCES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::DBus
    Dtk6::Core
    Dtk6::Widget
    dfm6-search
    KF6::IdleTime
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${INDEX_SRC_DIR}
)

message(STATUS "env-monitor configured (standalone project, not part of the main build tree)")
