# Install script for directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/brlcad/bu" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/app.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/assert.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/avs.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/bitv.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/cache.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/cmd.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/color.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/cv.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/debug.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/defines.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/dylib.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/endian.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/env.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/exit.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/file.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/getopt.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/glob.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/hash.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/hist.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/hook.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/interrupt.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/list.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/log.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/magic.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/malloc.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/mapped_file.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/mime.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/observer.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/opt.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/parallel.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/parse.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/path.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/process.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/ptbl.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/simd.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/snooze.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/sort.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/str.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/tc.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/time.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/units.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/user.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/uuid.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/version.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/vfont.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/vlb.h"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/include/bu/vls.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/include/bu/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
