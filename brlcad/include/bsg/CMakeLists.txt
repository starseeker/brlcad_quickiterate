set(
  bsg_headers
  adc.h
  compat.h
  defines.h
  lod.h
  plot3.h
  polygon.h
  snap.h
  tig.h
  util.h
  vectfont.h
  view_sets.h
  vlist.h
)
brlcad_manage_files(bsg_headers ${INCLUDE_DIR}/brlcad/bsg REQUIRED libbsg)

cmakefiles(CMakeLists.txt)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
