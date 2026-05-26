set(
  bsg_headers
  adc.h
  defines.h
  draw_ctx.h
  draw_set.h
  events.h
  faceplate.h
  field.h
  lod.h
  lod_ops.h
  node.h
  node_group.h
  node_shape.h
  node_transform.h
  obol_node.h
  overlay.h
  payload.h
  plot3.h
  polygon.h
  scene_set.h
  sensor.h
  snap.h
  tcl_data.h
  tig.h
  util.h
  vectfont.h
  view_scope.h
  view_sets.h
  visit.h
  vlist.h
)
brlcad_manage_files(bsg_headers ${INCLUDE_DIR}/brlcad/bsg REQUIRED libbsg)

cmakefiles(CMakeLists.txt)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
