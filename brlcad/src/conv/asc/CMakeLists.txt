add_definitions(-D_CONSOLE -DBRLCAD_DLL)

brlcad_addexec(asc2dsp asc2dsp.c libbu FOLDER Conv)

brlcad_addexec(pix2asc pix2asc.c libbu FOLDER Conv)

if(TARGET libtclcad)
  brlcad_addexec(asc2g asc2g.c "libtclcad;libged;libwdb;librt;libnmg;libbu;${WINSOCK_LIB};${M_LIBRARY}" FOLDER Conv)
  brlcad_addexec(g2asc g2asc.c "libtclcad;librt;libnmg;libbu;${WINSOCK_LIB}" FOLDER Conv)
  add_dependencies(asc2g TclIndexBld)
  add_dependencies(g2asc TclIndexBld)
endif(TARGET libtclcad)

cmakefiles(
  CMakeLists.txt
  asc2g.c
  g2asc.c
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
