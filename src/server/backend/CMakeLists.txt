cmake_minimum_required(VERSION 3.1.0)

# Version
if (NOT PROJECT_VERSION)
    set(PROJECT_VERSION "1.0.0")
endif()

if (NOT PROJECT_VERSION_MAJOR)
    set(PROJECT_VERSION_MAJOR 0)
endif()

add_definitions(-DQT_MESSAGELOGCONTEXT)

if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
   add_definitions(-DDEFAULT_MSG_TYPE=QtDebugMsg)
#    SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast")
    add_definitions(-DDEFAULT_MSG_TYPE=QtWarningMsg)
    # SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(LFTManager_XML ${CMAKE_CURRENT_SOURCE_DIR}/dbusservice/com.deepin.anything.xml)

# Setup the environment
find_package(Qt5 COMPONENTS Core REQUIRED)
find_package(Qt5 COMPONENTS Concurrent REQUIRED)
find_package(Qt5 COMPONENTS DBus REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(mount REQUIRED mount IMPORTED_TARGET)
pkg_check_modules(GNL REQUIRED libnl-3.0 libnl-genl-3.0)

pkg_check_modules(qt-udisks2 REQUIRED udisks2-qt5 IMPORTED_TARGET)

# generate dbus interface
# DBus: LFTManager
# qt5_generate_dbus_interface(
#     lftmanager.h
#     ${DeviceManager_XML}
#     OPTIONS -M -S
# )
qt5_add_dbus_adaptor(SRCS ${LFTManager_XML}
    lftmanager.h LFTManager anything_adaptor)

# include
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../kernelmod
    ${CMAKE_CURRENT_SOURCE_DIR}/../../library/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/dbusservice
)

# public include
FILE (GLOB IO_PUBLIC_INCLUDES
    "anythingexport.h"
    "lib/lftmanager.h"
)

# private include
FILE (GLOB PRIVATE_INCLUDES
    "lib/*.h" "*.h"
)

# src
FILE (GLOB SERVER_SRCS
    "lib/*.cpp" "*.cpp"
)

# Build
add_library(${PROJECT_NAME} SHARED
    ${PRIVATE_INCLUDES}
    ${SERVER_SRCS}
    ${SRCS}
)

set_target_properties(
    ${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt5::Core
    Qt5::Concurrent
    Qt5::DBus
    PkgConfig::qt-udisks2
    PkgConfig::mount
    anything
    ${MOUNT_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GNL_LIBRARIES}
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${GLIB_INCLUDE_DIRS}
    ${GNL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_LIST_DIR}/lib
)

FILE (GLOB PUB_INCLUDES_INSTALL
    "anythingexport.h"
    "lib/lftmanager.h"
)
FILE (GLOB DBUS_INSTALL
    "dbusservice/com.deepin.anything.xml"
)
FILE (GLOB DBUS_CONFIG_INSTALL
    "dbusservice/com.deepin.anything.conf"
)

include(GNUInstallDirs)

install (FILES ${PUB_INCLUDES_INSTALL}
    DESTINATION include/${PROJECT_NAME}
)
install (FILES ${DBUS_INSTALL}
    DESTINATION /usr/share/dbus-1/interfaces
)
install (FILES ${DBUS_CONFIG_INSTALL}
    DESTINATION /etc/dbus-1/system.d
)

install_targets ("/${CMAKE_INSTALL_LIBDIR}" ${PROJECT_NAME})


configure_file ("${PROJECT_NAME}.pc.in" "${PROJECT_NAME}.pc" @ONLY)
install (FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
