set(
  LIBBSG_DM_SRCS
  dm_adapter.c
  geom.c
)

# libbsg_dm depends on libbsg (scene graph), libdm (display manager),
# and librt (for rt_db_internal / OBJ[] in geom.c).
# Note: libbsg_dm_deps is defined by ${BRLCAD_SOURCE_DIR}/src/source_dirs.cmake

brlcad_addlib(libbsg_dm "${LIBBSG_DM_SRCS}" "${libbsg_dm_deps}" "" "")
set_target_properties(libbsg_dm PROPERTIES VERSION 1.0.0 SOVERSION 1)
if(HIDE_INTERNAL_SYMBOLS)
  set_property(TARGET libbsg_dm APPEND PROPERTY COMPILE_DEFINITIONS "LIBBSG_DM_DLL_EXPORTS")
  if(TARGET libbsg_dm-obj)
    set_property(TARGET libbsg_dm-obj APPEND PROPERTY COMPILE_DEFINITIONS "LIBBSG_DM_DLL_EXPORTS")
  endif(TARGET libbsg_dm-obj)
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
