set(
  LIBBSG_SRCS
  diff.c
  hash.c
  scene_graph.cpp
)

# libbsg_deps is defined by ${BRLCAD_SOURCE_DIR}/src/source_dirs.cmake.
# libbsg depends on libbv (Phase 1: bsg_* functions delegate to bv_*) and needs
# LMDB (used by bv_lod internals).
brlcad_find_package(LMDB REQUIRED)

set(BSG_LOCAL_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}
  # TODO: libbv source dir is included here for bv_private.h (Phase 1 compatibility
  # only).  When bsg_shape becomes an independent struct (Phase 2), remove this entry
  # and update bsg_private.h to define bsg_scene_obj_internal independently.
  ${BRLCAD_SOURCE_DIR}/src/libbv
  ${LMDB_INCLUDE_DIR}
)

# Note: libbsg_deps is defined by source_dirs.cmake
brlcad_addlib(libbsg "${LIBBSG_SRCS}" "${libbsg_deps};${LMDB_LIBRARIES}" "" "${BSG_LOCAL_INCLUDE_DIRS}")
set_target_properties(libbsg PROPERTIES VERSION 1.0.0 SOVERSION 1)
if(HIDE_INTERNAL_SYMBOLS)
  set_property(TARGET libbsg APPEND PROPERTY COMPILE_DEFINITIONS "BSG_DLL_EXPORTS")
  if(TARGET libbsg-obj)
    set_property(TARGET libbsg-obj APPEND PROPERTY COMPILE_DEFINITIONS "BSG_DLL_EXPORTS")
  endif(TARGET libbsg-obj)
endif(HIDE_INTERNAL_SYMBOLS)

cmakefiles(
  CMakeLists.txt
  bsg_private.h
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
