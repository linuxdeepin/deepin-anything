cmake_minimum_required(VERSION 3.1.0)

project(deepin-anything)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Install settings
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX /usr)
endif ()
# Top-level testing switch: child components (e.g. src/dispatcher) declare
# their own `option(ENABLE_TESTING ... OFF)`, but the cache variable set here
# takes precedence, so a single -DENABLE_TESTING=ON at configure time
# propagates to every component's test subdirectory.
option(ENABLE_TESTING "Enable building tests" OFF)

# Let `ctest` from the top-level build dir traverse into component test
# dirs (each component registers its tests via its own ENABLE_TESTING).
# Must precede add_subdirectory: testing support is only wired into
# subdirectories added after this call.
enable_testing()
add_subdirectory(${PROJECT_SOURCE_DIR}/src)
add_subdirectory(${PROJECT_SOURCE_DIR}/config)
