# Install script for directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/brlcad/bn" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/anim.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/complex.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/defines.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/dvec.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/mat.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/msr.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/multipoly.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/noise.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/poly.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/qmath.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/rand.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/randmt.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/spm.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/tabdata.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/sobol.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/str.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/tol.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/ulp.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/version.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bn/wavelet.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/include/bn/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
