set(
  libbsg_dm_headers
  libbsg_dm.h
)
brlcad_manage_files(libbsg_dm_headers ${INCLUDE_DIR}/brlcad/libbsg_dm REQUIRED libbsg_dm)

cmakefiles(CMakeLists.txt)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
