cmake_minimum_required(VERSION 3.10)

project(deepin-anything-logger C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0)
pkg_check_modules(GNL REQUIRED libnl-3.0 libnl-genl-3.0)

add_executable(deepin-anything-logger
    main.c
    event_listener.c
    event_logger.c
    file_log.c
    datatype.c
    dconfig.c
    config.c
    log.c
)

# Include directories
target_include_directories(deepin-anything-logger PUBLIC
    ${GLIB_INCLUDE_DIRS}
    ${GNL_INCLUDE_DIRS}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../kernelmod>
)

# Link libraries
target_link_libraries(deepin-anything-logger PRIVATE
    ${GLIB_LIBRARIES}
    ${GNL_LIBRARIES}
)

install(TARGETS deepin-anything-logger DESTINATION libexec)

# Install dconfig meta file
install(FILES ${CMAKE_SOURCE_DIR}/assets/org.deepin.anything.logger.json DESTINATION share/dsg/configs/org.deepin.anything)

# Install systemd service file
install(FILES deepin-anything-logger.service DESTINATION lib/systemd/system)

# Build tests if enabled
option(ENABLE_TESTING "Enable testing" OFF)

if(ENABLE_TESTING)
    enable_testing()
    
    # Add tests subdirectory
    add_subdirectory(tests)
    
    # Convenience targets for running tests
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --verbose
        COMMENT "Running all tests"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    
    add_custom_target(check-verbose
        COMMAND ${CMAKE_CTEST_COMMAND} --verbose --output-on-failure
        COMMENT "Running all tests with verbose output"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    
    # Coverage report target (requires debug build)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/tests --target coverage
            COMMENT "Generating test coverage report"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
    endif()

endif()
