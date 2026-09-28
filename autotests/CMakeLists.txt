# deepin-anything unit tests (root-level autotests/, mirroring the
# dde-file-manager layout).
#
# Gated by OPT_ENABLE_BUILD_UT (root CMakeLists.txt). The legacy C suites in
# src/{logger,dispatcher}/tests are registered separately by their own
# CMakeLists and are unaffected.

include(${CMAKE_SOURCE_DIR}/cmake/AnythingTestUtils.cmake)

# Enable coverage flags if requested (only via run-ut.sh --coverage)
option(ANYTHING_ENABLE_COVERAGE "Enable gcov coverage instrumentation" OFF)
if(ANYTHING_ENABLE_COVERAGE)
    message(STATUS "Anything: Coverage instrumentation enabled")
endif()

# Per-suite switches (default ON; turn off to skip a suite)
option(OPT_ENABLE_BUILD_UT_INDEX "Build index unit tests" ON)

if(OPT_ENABLE_BUILD_UT_INDEX)
    add_subdirectory(index)
endif()
