project(deepin-anything-server-lib)

cmake_minimum_required(VERSION 3.1.0)

# Setup the environment
SET(CMAKE_CXX_STANDARD 17)

# Must set automoc, or some unused symbols will be excluded.
SET(CMAKE_AUTOMOC ON)

SET(CMAKE_INCLUDE_CURRENT_DIR ON)

# set default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif(NOT CMAKE_BUILD_TYPE)

add_subdirectory("backend")
add_subdirectory("app")
