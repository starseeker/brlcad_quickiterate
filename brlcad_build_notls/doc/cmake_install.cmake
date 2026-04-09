# Install script for directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc

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
  include("/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/doc/asciidoc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/doc/html/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/doc/legal/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/BRL-CAD.bib"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/GITHUB"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/IDEAS"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/PROJECTS"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/README.Linux"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/README.MacOSX"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/README.Windows"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/README.BSD"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/README.Solaris"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/README.VAX"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/checklist.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/description.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/TODO.BREP"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/TODO.shaded_displays"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/apitrace.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/brep.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/bu_opt_design_notes.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/cvs.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/debugging_example.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/debugging_notes.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/editors.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/history.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/hypot.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/mater.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/matrix.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/pre_BRL-CAD.bib"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc/mged_old" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/a.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/adc.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/all.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/axis-3525.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/b.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/buttonmenu.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/c.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/coord-axes.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/crod-close.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/crod.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/d.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/doit"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/e.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-arbrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-bgrp.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-bgrp311.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-cgrp.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-cgrp321.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-ellg.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-ellg2x.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-ellgxyz.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-gredit.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-grpath.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-scale.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-spread.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-stacked.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-start.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-tor111.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-xymove.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/eo-xyzmove.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es5-edge1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es5-edge2.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es5-edge3.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es5-edge4.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es5-rot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es5-scale.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es5-sed.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es5-top.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es5-tr.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es5-xrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-edge1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-edge2.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-edge3.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-ex1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-ex2.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-rot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-scale.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-sed.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-top.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-tr0.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-xrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-yrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/es8-zrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-mh.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-mhrt.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-rot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-sa.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-sb.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-sc.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-scale.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-sd.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-sed.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-sh.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-top.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-tr.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-xrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-yrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/esc-zrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ese-sa.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ese-sb.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ese-sc.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ese-scale.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ese-sed.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ese-top.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ese-tr.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ese-xrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ese-yrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ese-zrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/est-scale.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/est-sed.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/est-sr1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/est-sr2.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/est-top.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/est-tr.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/est-xrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/est-yrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/est-zrot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ex.arb4.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ex.arb8.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ex.box.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ex.ellg.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ex.raw.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ex.rcc.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ex.rpp.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ex.sph.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ex.tor.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ex.trc.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/f.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/faceplate.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/faceplate1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/fig-sgi-buttons.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/fig-sgi-knobs.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/fig-sgi.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/fig-vg-buttons.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/fig-vg-knobs.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/g.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/h.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/j.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/k.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/l.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/m.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/menu-arb-ctl.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/menu-arb4-edge.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/menu-arb4-face.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/menu-arb4-rot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/menu-arb8-edge.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/menu-arb8-face.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/menu-arb8-rot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/obj-edit.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/obj-path.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/obj-pick.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ped-ell.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ped-tgc.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/ped-tor.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/plane-35a.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/plane-35b.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/plane-bot1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/plane-bot2.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/plane-front1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/plane-front2.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/plane-right1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/plane-right2.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/plane-top1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/plane-top2.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/rmit-3525.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/robot.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/sol-2pick.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/sol-ed.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/sol-pick.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/t1-2s-pk.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/t1-obj-ed.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/t1-obj-ph.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/t1-obj-pk.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/t1-rot-vw.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/t1-sol-ed.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/t1-sol-pk.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/t1-top-vw.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/t1.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/test.tex"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/v-arb8-side.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/v-arb8-top.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-arm1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-arm2.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-body.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-collar.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-final1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-hat-E.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-hat1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-hat2.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-hat3.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-head.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-leg1.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-prims.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/mged/wm-tube.ps"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/ecosystem.dot"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/regions.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/rounding.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/notes/tool_categories.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/doc/old-mged.tr"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc" TYPE FILE FILES "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/doc/pad_file.xml")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/doc/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
