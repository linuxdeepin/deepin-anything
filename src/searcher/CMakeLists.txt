cmake_minimum_required(VERSION 3.10)
project(deepin-anything-searcher)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(LUCENE REQUIRED liblucene++)
pkg_check_modules(LUCENE_CONTRIB REQUIRED liblucene++-contrib)
find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# Add executable
add_executable(deepin-anything-searcher
    main.cpp
    searcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/src/analyzers/chineseanalyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/src/analyzers/chinesetokenizer.cpp
)

# Include directories
target_include_directories(deepin-anything-searcher PRIVATE
    ${GLIB_INCLUDE_DIRS}
    ${LUCENE_INCLUDE_DIRS}
    ${LUCENE_CONTRIB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/include
)

# Link libraries
target_link_libraries(deepin-anything-searcher
    ${GLIB_LIBRARIES}
    ${LUCENE_LIBRARIES}
    ${LUCENE_CONTRIB_LIBRARIES}
    Threads::Threads
    ${Boost_LIBRARIES}
)

# Install
install(TARGETS deepin-anything-searcher
    RUNTIME DESTINATION bin
) 