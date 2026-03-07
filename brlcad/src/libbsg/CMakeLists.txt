set(
  LIBBSG_SRCS
  adc.c
  diff.c
  font.c
  hash.c
  knobs.cpp
  lod.cpp
  polygon.c
  polygon_op.cpp
  polygon_fill.cpp
  scene_graph.cpp
  snap.c
  tig/axis.c
  tig/list.c
  tig/marker.c
  tig/scale.c
  tig/symbol.c
  tig/tplot.c
  tig/vectfont.c
  tig/vector.c
  util.cpp
  view_sets.cpp
  vlist.c
)

# libbsg_deps is defined by ${BRLCAD_SOURCE_DIR}/src/source_dirs.cmake.
# libbsg is independent of libbv; it needs LMDB for LoD mesh caching.
brlcad_find_package(LMDB REQUIRED)

set(BSG_LOCAL_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${LMDB_INCLUDE_DIR}
)

# Note: libbsg_deps is defined by source_dirs.cmake
# vlist.c defines PLOT3_IMPLEMENTATION before including bsg/plot3.h, which
# causes the plot3_* function bodies to be compiled into this translation
# unit.  Keep it out of unity builds to prevent header-guard races.
brlcad_addlib(libbsg "${LIBBSG_SRCS}" "${libbsg_deps};${LMDB_LIBRARIES}" "" "${BSG_LOCAL_INCLUDE_DIRS}"
  UNITY_BUILD_SKIP vlist.c
)
set_target_properties(libbsg PROPERTIES VERSION 1.0.0 SOVERSION 1)
if(HIDE_INTERNAL_SYMBOLS)
  set_property(TARGET libbsg APPEND PROPERTY COMPILE_DEFINITIONS "BSG_DLL_EXPORTS;BV_DLL_EXPORTS;PLOT3_DLL_EXPORTS")
  if(TARGET libbsg-obj)
    set_property(TARGET libbsg-obj APPEND PROPERTY COMPILE_DEFINITIONS "BSG_DLL_EXPORTS;BV_DLL_EXPORTS;PLOT3_DLL_EXPORTS")
  endif(TARGET libbsg-obj)
endif(HIDE_INTERNAL_SYMBOLS)

cmakefiles(
  CMakeLists.txt
  bsg_private.h
  tig/axis.c
  tig/list.c
  tig/marker.c
  tig/scale.c
  tig/symbol.c
  tig/tplot.c
  tig/vectfont.c
  tig/vector.c
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
