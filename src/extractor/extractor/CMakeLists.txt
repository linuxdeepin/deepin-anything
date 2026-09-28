cmake_minimum_required(VERSION 3.10)

set(BIN_NAME "deepin-anything-extractor")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 COMPONENTS Core Gui REQUIRED)

set(EXTRACTOR_FILES
    extractorapp.cpp
    extractorapp.h
    main.cpp
    pluginloader.cpp
    pluginloader.h
)

add_executable(${BIN_NAME}
    ${EXTRACTOR_FILES}
)

target_link_libraries(${BIN_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    deepin-anything-extractor-lib
    common
)

target_include_directories(${BIN_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../libextractor
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins
)

# Enable position-independent executables for improved security
target_link_options(${BIN_NAME} PRIVATE -pie)

target_compile_options(${BIN_NAME} PRIVATE -Wall -Wextra)

install(TARGETS ${BIN_NAME} DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

message(STATUS "deepin-anything-extractor configured")
