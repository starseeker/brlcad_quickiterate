# Install script for directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/tclscripts/rtwizard/lib" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/DbPage.itk"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/ExamplePage.itk"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/FbPage.itk"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/FeedbackDialog.itk"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/FullColorPage.itk"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/GhostPage.itk"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/HelpPage.itk"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/HighlightedPage.itk"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/IntroPage.itk"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/LinePage.itk"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/MGEDpage.itk"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/PictureTypeA.itcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/PictureTypeB.itcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/PictureTypeBase.itcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/PictureTypeC.itcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/PictureTypeD.itcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/PictureTypeE.itcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/PictureTypeF.itcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/rtwizard/lib/Wizard.itk"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/src/tclscripts/rtwizard/lib/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
