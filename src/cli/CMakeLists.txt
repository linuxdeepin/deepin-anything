cmake_minimum_required(VERSION 3.1.0)

project(anything_cli)

# include
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../library/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../library/inc/index
    ${CMAKE_CURRENT_SOURCE_DIR}/../kernelmod
)

FILE(GLOB SRC_FILES
    "src/*.c"
)


# Build
add_executable(${PROJECT_NAME} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_link_libraries(
    ${PROJECT_NAME}
    anything
)

# binary
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
