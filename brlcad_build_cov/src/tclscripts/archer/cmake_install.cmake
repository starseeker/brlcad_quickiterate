# Install script for directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer

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
  include("/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/src/tclscripts/archer/images/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/tclscripts/archer" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/Arb4EditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/Arb5EditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/Arb6EditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/Arb7EditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/Arb8EditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/Archer.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/ArcherCore.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/AttrGroupsDisplayUtility.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/BotEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/BotUtility.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/BrepEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/CombEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/DataUtils.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/EhyEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/EllEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/EpaEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/EtoEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/ExtrudeEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/GeometryEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/GripEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/HalfEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/HypEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/JointEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/LoadArcherLibs.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/LODUtility.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/MetaballEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/PartEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/PipeEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/Plugin.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/RhcEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/RpcEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/ShaderEdit.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/SketchEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/SphereEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/SuperellEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/TgcEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/TorusEditFrame.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/Utility.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/Wizard.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/bgerror.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/itk_redefines.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/archer/tabwindow.itk"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/src/tclscripts/archer/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
