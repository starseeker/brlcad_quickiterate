set(
  LIBBSG_SRCS
  # BSG scene-graph sources (native)
  action.c
  appearance.c
  camera.c
  draw_set.c
  field.c
  identity.c
  light.c
  lod.cpp
  lod_node.c
  material.c
  node.c
  node_group.c
  node_root.c
  node_shape.c
  node_transform.c
  overlay.c
  payload.c
  render.c
  scene_graph.cpp
  selection.c
  sensor.c
  util.c
  view_scope.c
  visit.c
  vlist.c
  # Former libbv sources merged in (bv_ prefixed names avoid collision)
  adc.c
  diff.c
  font.c
  hash.c
  knobs.cpp
  bv_lod.cpp
  polygon.c
  polygon_fill.cpp
  polygon_op.cpp
  snap.c
  tig/axis.c
  tig/list.c
  tig/marker.c
  tig/scale.c
  tig/symbol.c
  tig/tplot.c
  tig/vectfont.c
  tig/vector.c
  bv_util.cpp
  bv_vlist.c
  view_sets.cpp
)

set(BSG_LOCAL_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

# Note - libbsg_deps is defined by ${BRLCAD_SOURCE_DIR}/src/source_dirs.cmake
# bv_vlist.c defines PLOT3_IMPLEMENTATION before including bsg/plot3.h, so it
# must be excluded from unity builds to ensure the plot3_* function bodies are
# compiled exactly once.
brlcad_addlib(libbsg "${LIBBSG_SRCS}" "" "${BSG_LOCAL_INCLUDE_DIRS}"
  PUBLIC_LIBS ${libbsg_deps}
  UNITY_BUILD_SKIP bv_vlist.c
)
set_target_properties(libbsg PROPERTIES VERSION 1.0.0 SOVERSION 1)
if(HIDE_INTERNAL_SYMBOLS)
  set_property(TARGET libbsg APPEND PROPERTY COMPILE_DEFINITIONS "BSG_DLL_EXPORTS")
  set_property(TARGET libbsg APPEND PROPERTY COMPILE_DEFINITIONS "BV_DLL_EXPORTS")
  set_property(TARGET libbsg APPEND PROPERTY COMPILE_DEFINITIONS "PLOT3_DLL_EXPORTS")
  if(TARGET libbsg-obj)
    set_property(TARGET libbsg-obj APPEND PROPERTY COMPILE_DEFINITIONS "BSG_DLL_EXPORTS")
    set_property(TARGET libbsg-obj APPEND PROPERTY COMPILE_DEFINITIONS "BV_DLL_EXPORTS")
    set_property(TARGET libbsg-obj APPEND PROPERTY COMPILE_DEFINITIONS "PLOT3_DLL_EXPORTS")
  endif(TARGET libbsg-obj)
endif(HIDE_INTERNAL_SYMBOLS)

add_subdirectory(tests)

cmakefiles(
  CMakeLists.txt
  bsg_private.h
  action.c
  appearance.c
  camera.c
  draw_set.c
  field.c
  identity.c
  light.c
  lod_node.c
  material.c
  node.c
  node_group.c
  node_root.c
  node_shape.c
  node_transform.c
  overlay.c
  payload.c
  render.c
  selection.c
  sensor.c
  util.c
  view_scope.c
  visit.c
  adc.c
  diff.c
  font.c
  hash.c
  bv_vlist.c
  bv_util.cpp
  bv_lod.cpp
  polygon.c
  polygon_fill.cpp
  polygon_op.cpp
  snap.c
  view_sets.cpp
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
