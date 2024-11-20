cmake_minimum_required(VERSION 3.1.0)

project(vfs_monitor)

# Version
if (NOT PROJECT_VERSION)
    set(PROJECT_VERSION "0.0")
endif()

set(package_name "deepin-anything-${PROJECT_VERSION}")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall -O3")

 # Set the source files
# set(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*)

FILE(GLOB SOURCES
    "*.h"
    "*.c"
    )

FILE(GLOB SRC_FILES
    "*.h"
    "*.c"
    "Makefile"
    "CMakeLists.txt"
    )

 # Set the install directory
set(INSTALL_DIR /usr/src/${package_name})

 # Create the module target
# add_library(${PROJECT_NAME} SHARED ${SOURCES})

 # Set the install target
# install(TARGETS ${PROJECT_NAME} DESTINATION ${INSTALL_DIR})

# file(COPY kernelmod/* DESTINATION ${CMAKE_INSTALL_LIBDIR}/usr/src/deepin-anything-${VERSION})

 # Create the DKMS configuration file
configure_file(dkms.conf.in dkms.conf @ONLY)

 # Install the DKMS configuration file
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dkms.conf DESTINATION ${INSTALL_DIR})

 # Create the DKMS deb package install file
configure_file(
    deepin-anything-dkms.dkms.in
    deepin-anything-dkms.dkms
    @ONLY
)

message("CMAKE_CURRENT_BINARY_DIR:" ${CMAKE_CURRENT_BINARY_DIR})

 # Install the DKMS file into debian dir
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/deepin-anything-dkms.dkms DESTINATION ../..)

 # Install all source files
install(FILES ${SRC_FILES} DESTINATION ${INSTALL_DIR})

  # Install the module conf file
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/anything.conf DESTINATION /usr/lib/modules-load.d)