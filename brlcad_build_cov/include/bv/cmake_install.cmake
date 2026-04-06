# Install script for directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/brlcad/rel-7.43.0")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
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

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/brlcad/bv" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/adc.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/defines.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/events.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/faceplate.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/lod.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/plot3.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/polygon.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/snap.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/tcl_data.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/tig.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/util.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/vectfont.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/vlist.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bv/view_sets.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/include/bv/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
