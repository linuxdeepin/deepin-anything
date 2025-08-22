cmake_minimum_required(VERSION 3.1)

project(deepin-anything-server)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(GIO_UNIX REQUIRED gio-unix-2.0)
pkg_check_modules(MOUNT REQUIRED mount IMPORTED_TARGET)

add_executable(${PROJECT_NAME} main.c)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${GLIB_INCLUDE_DIRS}
    ${GIO_INCLUDE_DIRS}
    ${GIO_UNIX_INCLUDE_DIRS}
    ${MOUNT_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    ${GIO_UNIX_LIBRARIES}
    ${MOUNT_LIBRARIES}
)

install(TARGETS ${PROJECT_NAME} DESTINATION libexec)
# installed by debhelper
# install(FILES deepin-anything-server.service DESTINATION lib/systemd/system)
