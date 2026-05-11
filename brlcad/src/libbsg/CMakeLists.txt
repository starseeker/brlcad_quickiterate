set(
  LIBBSG_SRCS
  action.c
  appearance.c
  draw_set.c
  field.c
  identity.c
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
  scene_graph.cpp
  selection.c
  sensor.c
  view_scope.c
  visit.c
  vlist.c
)

set(BSG_LOCAL_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

# Note - libbsg_deps is defined by ${BRLCAD_SOURCE_DIR}/src/source_dirs.cmake
brlcad_addlib(libbsg "${LIBBSG_SRCS}" "${libbsg_deps}" "" "${BSG_LOCAL_INCLUDE_DIRS}")
set_target_properties(libbsg PROPERTIES VERSION 1.0.0 SOVERSION 1)
if(HIDE_INTERNAL_SYMBOLS)
  set_property(TARGET libbsg APPEND PROPERTY COMPILE_DEFINITIONS "BSG_DLL_EXPORTS")
  if(TARGET libbsg-obj)
    set_property(TARGET libbsg-obj APPEND PROPERTY COMPILE_DEFINITIONS "BSG_DLL_EXPORTS")
  endif(TARGET libbsg-obj)
endif(HIDE_INTERNAL_SYMBOLS)

add_subdirectory(tests)

cmakefiles(
  CMakeLists.txt
  action.c
  appearance.c
  bsg_private.h
  draw_set.c
  field.c
  identity.c
  lod_node.c
  material.c
  node.c
  node_group.c
  node_root.c
  node_shape.c
  node_transform.c
  overlay.c
  payload.c
  selection.c
  sensor.c
  view_scope.c
  visit.c
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
