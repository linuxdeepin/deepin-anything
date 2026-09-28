# AnythingTestUtils.cmake - Test utilities for deepin-anything C++ tests.
# Mirrors the old dfm_add_test helper: gtest + vendored cpp-stub/stub-ext
# (white-box tests recompile the target's own sources with
# -fno-access-control).

cmake_minimum_required(VERSION 3.10)

function(anything_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LINK_LIBRARIES;INCLUDE_DIRS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "anything_add_test(${name}): SOURCES is required")
    endif()

    find_package(GTest REQUIRED)
    find_package(Qt6 COMPONENTS Test REQUIRED)

    # testutils stub sources (stub-ext) and shadowing stub headers (cpp-stub)
    set(_stub_root "${CMAKE_SOURCE_DIR}/3rdparty/testutils")
    if(NOT EXISTS "${_stub_root}")
        message(FATAL_ERROR "Anything: testutils not found at ${_stub_root}")
    endif()

    set(_all_sources ${ARG_SOURCES})
    file(GLOB _stub_src "${_stub_root}/stub-ext/*.cpp")
    list(APPEND _all_sources ${_stub_src})

    add_executable(${name} ${_all_sources})
    # -fno-access-control: white-box testing (stub-ext private access).
    # -Wno-pmf-conversions: vendored 3rdparty/testutils/stub-ext relies on
    # member-function-pointer punning.
    target_compile_options(${name} PRIVATE
        -fno-access-control
        -Wno-pmf-conversions
    )
    target_compile_definitions(${name} PRIVATE DEBUG_STUB_INVOKE)
    target_include_directories(${name} PRIVATE
        ${_stub_root}
        ${_stub_root}/cpp-stub
        ${_stub_root}/stub-ext
    )

    if(ARG_INCLUDE_DIRS)
        target_include_directories(${name} PRIVATE ${ARG_INCLUDE_DIRS})
    endif()

    target_link_libraries(${name} PRIVATE
        GTest::gtest
        GTest::gtest_main
        pthread
        Qt6::Test
    )
    if(ARG_LINK_LIBRARIES)
        target_link_libraries(${name} PRIVATE ${ARG_LINK_LIBRARIES})
    endif()

    # Enable coverage flags if requested (only via run-ut.sh --coverage)
    if(ANYTHING_ENABLE_COVERAGE)
        target_compile_options(${name} PRIVATE --coverage -O0 -fno-inline)
        target_link_libraries(${name} PRIVATE gcov)
    endif()

    add_test(NAME ${name} COMMAND ${name})
    message(STATUS "Anything: Added test ${name}")
endfunction()

message(STATUS "Anything: Test utilities module loaded")
