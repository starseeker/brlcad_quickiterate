set(
  libbsg_headers
  libbsg.h
)
brlcad_manage_files(libbsg_headers ${INCLUDE_DIR}/brlcad/libbsg REQUIRED libbsg)

cmakefiles(CMakeLists.txt)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
