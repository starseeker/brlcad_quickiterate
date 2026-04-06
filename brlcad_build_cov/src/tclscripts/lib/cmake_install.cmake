# Install script for directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/tclscripts/lib" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Accordion.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/CellPlot.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/ColorEntry.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/ComboBox.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Command.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Database.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Db.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Display.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Dm.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Drawable.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Ged.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/GeometryIO.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Help.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Legend.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Mged.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/ModelAxesControl.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/QuadDisplay.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/RtControl.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/RtImage.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Splash.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/Table.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/TableView.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/TkTable.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/View.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/ViewAxesControl.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/cursor.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/apply_mat.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/gui_conversion.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/lib/pattern_gui.tcl"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/src/tclscripts/lib/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
