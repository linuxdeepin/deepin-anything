cmake_minimum_required(VERSION 3.10)

project(deepin-anything-extractor-lib CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 COMPONENTS Core REQUIRED)

include(GNUInstallDirs)

set(LIB_NAME deepin-anything-extractor-lib)

set(EXTRACTOR_LIB_FILES
    controllerpipe.cpp
    workerpipe.cpp
    controllerpipe.h
    extractortypes.h
    extractor_global.h
    workerpipe.h
)

add_library(${LIB_NAME}
    SHARED
    ${EXTRACTOR_LIB_FILES}
)

set_target_properties(${LIB_NAME} PROPERTIES
    OUTPUT_NAME deepin-anything-extractor
    VERSION 1.0.0
    SOVERSION 1
)

target_link_libraries(${LIB_NAME}
    PUBLIC
        Qt6::Core
)

target_include_directories(${LIB_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(${LIB_NAME} PRIVATE -Wall -Wextra)

# Install the communication library into the standard libdir.
install(TARGETS ${LIB_NAME}
    LIBRARY
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

message(STATUS "deepin-anything-extractor library configured")
