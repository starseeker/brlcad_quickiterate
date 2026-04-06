# Install script for directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/brlcad/bg" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/aabb_ray.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/ballpivot.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/chull.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/clip.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/defines.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/lseg.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/obr.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/pca.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/plane.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/polygon.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/polygon_types.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/sat.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/spsr.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/tri_pt.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/tri_ray.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/tri_tri.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/trimesh.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bg/vert_tree.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/include/bg/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
