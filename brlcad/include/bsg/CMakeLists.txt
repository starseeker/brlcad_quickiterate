set(
  bsg_headers
  compat.h
  defines.h
  lod.h
  util.h
)
brlcad_manage_files(bsg_headers ${INCLUDE_DIR}/brlcad/bsg REQUIRED libbv)

cmakefiles(CMakeLists.txt)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
