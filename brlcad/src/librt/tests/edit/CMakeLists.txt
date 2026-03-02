brlcad_addexec(rt_edit_test_tor tor.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_ell ell.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_tgc tgc.cpp "librt" TEST)

set(
  distcheck_files
  CMakeLists.txt
  tor.cpp
  ell.cpp
  tgc.cpp
)

cmakefiles(${distcheck_files})

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
