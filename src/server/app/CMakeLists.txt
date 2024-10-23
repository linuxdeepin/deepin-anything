cmake_minimum_required(VERSION 3.10)

project(deepin-anything-server)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Version
if (NOT PROJECT_VERSION)
    set(PROJECT_VERSION "1.0.0")
endif()

if (NOT PROJECT_VERSION_MAJOR)
    set(PROJECT_VERSION_MAJOR 0)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../backend
)

set(SRCS
    main.cpp
)

find_package(PkgConfig REQUIRED)
find_package(Qt5 COMPONENTS Core REQUIRED)

add_executable(${PROJECT_NAME} ${SRCS})

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt5::Core
    deepin-anything-server-lib
    stdc++fs
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message("src/server/app no builtin strlen")
    target_compile_options(${PROJECT_NAME} PRIVATE -fno-builtin-strlen)
endif()

# binary
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
