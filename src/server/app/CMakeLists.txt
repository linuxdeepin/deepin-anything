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
find_package(Qt${QT_DESIRED_VERSION} COMPONENTS Core REQUIRED)

add_executable(${PROJECT_NAME} ${SRCS})

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt${QT_DESIRED_VERSION}::Core
    deepin-anything-server-lib
)

# binary
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

install (FILES deepin-anything-server.service DESTINATION lib/systemd/system)
