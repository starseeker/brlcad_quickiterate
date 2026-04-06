# Install script for directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/db

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
  include("/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/db/chess/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/db/comgeom/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/db/nist/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/db/faa/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/bldg391.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/m35.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/moss.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/sphflake.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/star.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/world.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/aet.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/annual_gift_man.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/axis.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/bearing.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/boolean-ops.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/castle.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/cornell.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/cornell-kunigami.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/cray.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/crod.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/cube.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/demo.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/die.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/galileo.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/goliath.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/havoc.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/kman.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/ktank.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/lgt-test.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/operators.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/pic.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/pinewood.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/primitives.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/prim.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/radialgrid.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/rounds.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/shipping_container.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/tank_car.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/terra.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/toyjeep.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/traffic_light.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/truck.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/wave.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/woodsman.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/xmp.g")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/db/1647260.dsp")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/db/ebm_logo.bw")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/db/terra.bin")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/db" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/db/vol_data.bw")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/db/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
