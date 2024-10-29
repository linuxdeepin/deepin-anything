cmake_minimum_required(VERSION 3.1.0)
project(examples VERSION 1.0)

find_package(deepin-anything-server 1.0 CONFIG REQUIRED)

message(STATUS "deepin-anything-server Found: ${deepin-anything-server_FOUND}")

file(GLOB SOURCE_FILES src/*.cpp)
list(LENGTH SOURCE_FILES SRC_FILES_SIZE)
message(STATUS "Found ${SRC_FILES_SIZE} source files of examples")

foreach(source_file ${SOURCE_FILES})
    get_filename_component(file_name ${source_file} NAME)
    string(REPLACE ".cpp" "" name ${file_name})
    message(STATUS " ${name}")
    add_executable(${name} "src/${name}.cpp")
    target_link_libraries(${name} PRIVATE deepin-anything-server::deepin-anything-server)
    target_compile_options(${name} PRIVATE -Wall -g)
endforeach()