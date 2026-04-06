# Install script for directory: /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/tclscripts/mged" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/accel.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/adc.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/apply.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/asc2g.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/attr_edit.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/bindings.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/bot_face_select.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/bot_vertex_fuse_all.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/bots.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/botedit.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/build_region.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/calipers.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/callbacks.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/clear.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/collaborate.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/color.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/color_scheme.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/comb.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/combmenu.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/cycle.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/dbfindtree.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/dbupgrade.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/e_id.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/edit_menu.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/edit_solid.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/edit_solid_int.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/editmenu.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/editobj.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/eobjmenu.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/expand_comb.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/extract.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/facetize_all_regions.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/font.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/g2asc.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/get_regions.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/grid.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/grouper.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/help.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/helpdevel.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/icreate.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/illum.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/lc.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/list.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/lodconfig.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/make_solid.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/man.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/menu.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/mged.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/mgedrc.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/mike.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/mouse.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/mview.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/openw.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/plot.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/points.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/prj_add.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/ps.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/qray.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/ray.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/raypick.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/re_procs.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/remap_mater.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/rt.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/rt_script.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/sample.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/shaders.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/skt_ed.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/solclick.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/solcreate.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/solid.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/text.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/titles.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/tree.tcl"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/xclone.tcl"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/tclscripts/mged" TYPE FILE FILES
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v0_s0.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v0_s1.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v0_s2.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v0_s3.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v0_s4.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v0_s5.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v0_s6.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v0_s7.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v0_s8.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v0_s9.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v1_s0.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v1_s1.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v1_s2.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v1_s3.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v1_s4.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v1_s5.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v1_s6.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v1_s7.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v1_s8.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i0_v1_s9.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v0_s0.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v0_s1.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v0_s2.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v0_s3.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v0_s4.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v0_s5.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v0_s6.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v0_s7.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v0_s8.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v0_s9.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v1_s0.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v1_s1.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v1_s2.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v1_s3.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v1_s4.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v1_s5.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v1_s6.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v1_s7.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v1_s8.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/l_i1_v1_s9.gif"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/mike-dedication.txt"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/mike-tux.ppm"
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/tclscripts/mged/mike-tux.png"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/src/tclscripts/mged/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
