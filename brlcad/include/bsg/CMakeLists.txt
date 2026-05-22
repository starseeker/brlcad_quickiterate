set(
  bsg_headers
  action.h
  appearance.h
  camera.h
  compat.h
  defines.h
  draw_ctx.h
  draw_set.h
  field.h
  hud.h
  identity.h
  light.h
  lod.h
  lod_ops.h
  material.h
  node.h
  node_core.h
  node_group.h
  node_type.h
  node_shape.h
  node_transform.h
  obol_node.h
  overlay.h
  payload.h
  render.h
  scene_set.h
  selection.h
  sensor.h
  settings.h
  settings_types.h
  util.h
  view_scope.h
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
