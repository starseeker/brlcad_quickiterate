# Install script for directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt

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

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/include/rt/primitives/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/brlcad/rt" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/anim.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/application.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/binunif.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/boolweave.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/calc.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/cmd.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/comb.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/conv.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/db4.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/db5.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/db_attr.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/db_diff.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/db_fullpath.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/db_instance.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/db_internal.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/db_io.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/debug.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/defines.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/directory.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/dspline.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/edit.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/func.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/functab.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/geom.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/global.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/hit.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/htbl.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/mater.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/misc.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/mem.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/nmg_conv.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/nongeom.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/op.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/overlap.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/pattern.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/piece.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/prep.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/private.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/ray_partition.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/region.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/resource.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/rt_instance.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/search.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/seg.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/shoot.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/soltab.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/space_partition.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/tie.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/timer.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/tol.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/tree.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/uv.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/version.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/view.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/vlist.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/wdb.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/rt/xray.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/include/rt/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
