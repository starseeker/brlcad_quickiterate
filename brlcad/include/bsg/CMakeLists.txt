set(
  bsg_headers
  defines.h
  lod.h
  obol_node.h
  scene_set.h
  util.h
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
