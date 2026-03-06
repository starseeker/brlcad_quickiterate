set(
  LIBBSG_SRCS
  nodes.c
  traverse.c
  scene.c
)

# libbsg depends only on libbn and libbu — no libbv dependency.
# Note: libbsg_deps is defined by ${BRLCAD_SOURCE_DIR}/src/source_dirs.cmake

brlcad_addlib(libbsg "${LIBBSG_SRCS}" "${libbsg_deps}" "" "")
set_target_properties(libbsg PROPERTIES VERSION 1.0.0 SOVERSION 1)
if(HIDE_INTERNAL_SYMBOLS)
  set_property(TARGET libbsg APPEND PROPERTY COMPILE_DEFINITIONS "LIBBSG_DLL_EXPORTS")
  if(TARGET libbsg-obj)
    set_property(TARGET libbsg-obj APPEND PROPERTY COMPILE_DEFINITIONS "LIBBSG_DLL_EXPORTS")
  endif(TARGET libbsg-obj)
endif(HIDE_INTERNAL_SYMBOLS)

cmakefiles(
  CMakeLists.txt
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
