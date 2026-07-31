cmake_minimum_required(VERSION 3.1)

project(deepin-anything-server)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(GIO_UNIX REQUIRED gio-unix-2.0)
pkg_check_modules(MOUNT REQUIRED mount IMPORTED_TARGET)
pkg_check_modules(LIBNL REQUIRED libnl-3.0 libnl-genl-3.0)

add_executable(${PROJECT_NAME}
    main.c
    vfs-unnamed-device.c
    mount-monitor.c
    event-listener.c
    event-listener-genl.c
    event-listener-mmap-ring.c
    event-dispatcher.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/src/core/mount_info.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/src/utils/tools.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${GLIB_INCLUDE_DIRS}
    ${GIO_INCLUDE_DIRS}
    ${GIO_UNIX_INCLUDE_DIRS}
    ${MOUNT_INCLUDE_DIRS}
    ${LIBNL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../kernelmod
    ${CMAKE_CURRENT_SOURCE_DIR}/../dispatcher
    ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    ${GIO_UNIX_LIBRARIES}
    ${MOUNT_LIBRARIES}
    ${LIBNL_LIBRARIES}
    deepin-anything-dispatcher
)

target_link_options(${PROJECT_NAME} PRIVATE -pie)

install(TARGETS ${PROJECT_NAME} DESTINATION libexec)
# installed by debhelper
# install(FILES deepin-anything-server.service DESTINATION lib/systemd/system)

# Build the receiver-side example (off by default).
option(BUILD_SERVER_EXAMPLE "Build the server receiver example" OFF)
if(BUILD_SERVER_EXAMPLE)
    add_subdirectory(example)
endif()
