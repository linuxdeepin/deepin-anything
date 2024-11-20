# Install script for directory: /home/dxnu/dxnu-github/deepin-anything/src/kernelmod

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "None")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/src/deepin-anything-0.0/dkms.conf")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/src/deepin-anything-0.0" TYPE FILE FILES "/home/dxnu/dxnu-github/deepin-anything/obj-x86_64-linux-gnu/src/kernelmod/dkms.conf")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/../.." TYPE FILE FILES "/home/dxnu/dxnu-github/deepin-anything/obj-x86_64-linux-gnu/src/kernelmod/deepin-anything-dkms.dkms")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/src/deepin-anything-0.0/CMakeLists.txt;/usr/src/deepin-anything-0.0/Makefile;/usr/src/deepin-anything-0.0/arg_extractor.c;/usr/src/deepin-anything-0.0/arg_extractor.h;/usr/src/deepin-anything-0.0/event.c;/usr/src/deepin-anything-0.0/event.h;/usr/src/deepin-anything-0.0/event_merge.c;/usr/src/deepin-anything-0.0/event_merge.h;/usr/src/deepin-anything-0.0/module.c;/usr/src/deepin-anything-0.0/vfs_change_consts.h;/usr/src/deepin-anything-0.0/vfs_fsnotify.c;/usr/src/deepin-anything-0.0/vfs_fsnotify.h;/usr/src/deepin-anything-0.0/vfs_genl.c;/usr/src/deepin-anything-0.0/vfs_genl.h;/usr/src/deepin-anything-0.0/vfs_kgenl.h;/usr/src/deepin-anything-0.0/vfs_kretprobes.c;/usr/src/deepin-anything-0.0/vfs_kretprobes.h;/usr/src/deepin-anything-0.0/vfs_log.h;/usr/src/deepin-anything-0.0/vfs_sysfs.c;/usr/src/deepin-anything-0.0/vfs_sysfs.h")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/src/deepin-anything-0.0" TYPE FILE FILES
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/CMakeLists.txt"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/Makefile"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/arg_extractor.c"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/arg_extractor.h"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/event.c"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/event.h"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/event_merge.c"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/event_merge.h"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/module.c"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/vfs_change_consts.h"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/vfs_fsnotify.c"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/vfs_fsnotify.h"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/vfs_genl.c"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/vfs_genl.h"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/vfs_kgenl.h"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/vfs_kretprobes.c"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/vfs_kretprobes.h"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/vfs_log.h"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/vfs_sysfs.c"
    "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/vfs_sysfs.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/lib/modules-load.d/anything.conf")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/lib/modules-load.d" TYPE FILE FILES "/home/dxnu/dxnu-github/deepin-anything/src/kernelmod/anything.conf")
endif()

