cmake_minimum_required(VERSION 3.1.0)

project(deepin-anything)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Install settings
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX /usr)
endif ()

# Unit tests default to Debug configurations only (developer workflow:
# autotests/run-ut.sh). Pass -DOPT_ENABLE_BUILD_UT=ON to force them on in
# any configuration. Package builds (no build type) never build or run them.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(OPT_ENABLE_BUILD_UT "Build unit tests" ON)
else()
    option(OPT_ENABLE_BUILD_UT "Build unit tests" OFF)
endif()

# Always enable testing so the `test` target exists and no-ops gracefully
# (ctest with zero tests exits 0) when no tests are configured.
enable_testing()

add_subdirectory(${PROJECT_SOURCE_DIR}/src)
add_subdirectory(${PROJECT_SOURCE_DIR}/config)

# Root-level autotests/ mirrors the dde-file-manager layout; the legacy C
# suites under src/{logger,dispatcher}/tests stay where they are.
if(OPT_ENABLE_BUILD_UT)
    add_subdirectory(${PROJECT_SOURCE_DIR}/autotests)
endif()
