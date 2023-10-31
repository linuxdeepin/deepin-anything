cmake_minimum_required(VERSION 3.10)

project(deepin-anything-server)

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
)

# binary
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
