cmake_minimum_required(VERSION 3.10)

project(deepin-anything-common CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core REQUIRED)

add_library(common STATIC
    processprioritymanager.cpp
    processprioritymanager.h
)

target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(common PUBLIC Qt6::Core)

# Qt headers are pulled in via QtGlobal; keep the warning policy light for C++.
target_compile_options(common PRIVATE -Wall -Wextra)
