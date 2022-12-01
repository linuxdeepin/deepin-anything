cmake_minimum_required(VERSION 3.1)

project(deepin-anything-monitor LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(DtkDeclarative REQUIRED)

# Application
add_subdirectory(src)
