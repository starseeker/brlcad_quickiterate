if(BRLCAD_ENABLE_OBOL)
  # Obol path: libdm-free pixel-buffer server.
  # pixbuf_server.c replaces server.c; no dm/fb symbols are used.
  brlcad_addexec(fbserv "fbserv.c;pixbuf_server.c" "libbu;libpkg")
  target_compile_definitions(fbserv PRIVATE BRLCAD_ENABLE_OBOL)
else()
  include_directories(${DM_INCLUDE_DIRS})
  brlcad_addexec(fbserv "fbserv.c;server.c" libdm)
  add_target_deps(fbserv dm_plugins)
endif()

cmakefiles(CMakeLists.txt)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
