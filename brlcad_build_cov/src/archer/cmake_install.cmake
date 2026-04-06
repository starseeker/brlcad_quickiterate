# Install script for directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/tclscripts/archer/init" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/archer_launch.tcl")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/archer_ack.txt")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/plugins/archer/Utility" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Utility/attrGroupsDisplayUtilityP.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Utility/botUtilityP.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Utility/lodUtilityP.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Utility/README"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/plugins/archer/Wizards" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Wizards/humanwizard.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Wizards/tankwizard.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Wizards/tirewizard.tcl"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/plugins/archer/Command" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Commands/README")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/plugins/archer/Core" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Core/README")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/plugins/archer/Utility/attrGroupsDisplayUtilityP" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Utility/attrGroupsDisplayUtilityP/AttrGroupsDisplayUtilityP.tcl")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/plugins/archer/Utility/botUtilityP" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Utility/botUtilityP/BotUtilityP.tcl")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/plugins/archer/Utility/lodUtilityP" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Utility/lodUtilityP/LODUtilityP.tcl")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/plugins/archer/Wizards/humanwizard" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Wizards/humanwizard/HumanWizard.tcl")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/plugins/archer/Wizards/tankwizard" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Wizards/tankwizard/TankWizard.tcl")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/plugins/archer/Wizards/tankwizardIA/images" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Wizards/tankwizard/images/tank.png")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/plugins/archer/Wizards/tirewizard" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/archer/plugins/Wizards/tirewizard/TireWizard.tcl")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/src/archer/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
