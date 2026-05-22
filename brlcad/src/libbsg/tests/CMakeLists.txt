# Tests for libbsg

brlcad_addexec(test_bsg_vlist test_vlist.c "libbsg;libbv;libbu" TEST)
brlcad_addexec(test_bsg_arb8  test_arb8.c  "libbsg;libbv;libbu" TEST)
brlcad_addexec(test_bsg_node test_bsg_node.c "libbsg;libbv;libbu" TEST)
brlcad_addexec(test_bsg_identity test_identity.c "libbsg;libbv;libbu" TEST)
brlcad_addexec(test_bsg_material test_material.c "libbsg;libbv;libbu" TEST)
brlcad_addexec(test_bsg_payload test_payload.c "libbsg;libbv;libbu" TEST)
brlcad_addexec(test_bsg_action test_action.c "libbsg;libbv;libbu" TEST)
brlcad_addexec(test_bsg_selection test_selection.c "libbsg;libbv;libbu" TEST)
brlcad_addexec(test_bsg_camera test_camera.c "libbsg;libbv;libbu" TEST)
brlcad_addexec(test_bsg_render test_render.c "libbsg;libbv;libbu" TEST)
brlcad_addexec(test_bsg_settings test_settings.c "libbsg;libbv;libbu" TEST)

#
# bsg_vlist_cmd_cnt tests
#
brlcad_add_test(NAME libbsg_vlist_null  COMMAND test_bsg_vlist -1)
brlcad_add_test(NAME libbsg_vlist_0    COMMAND test_bsg_vlist  0)
brlcad_add_test(NAME libbsg_vlist_1    COMMAND test_bsg_vlist  1)
brlcad_add_test(NAME libbsg_vlist_5    COMMAND test_bsg_vlist  5)
brlcad_add_test(NAME libbsg_vlist_18   COMMAND test_bsg_vlist 18)
brlcad_add_test(NAME libbsg_vlist_100  COMMAND test_bsg_vlist 100)

#
# bsg_vlist_arb8 — must produce exactly 18 commands
#
brlcad_add_test(NAME libbsg_arb8 COMMAND test_bsg_arb8)
brlcad_add_test(NAME libbsg_node COMMAND test_bsg_node)
brlcad_add_test(NAME libbsg_identity COMMAND test_bsg_identity)
brlcad_add_test(NAME libbsg_material COMMAND test_bsg_material)
brlcad_add_test(NAME libbsg_payload COMMAND test_bsg_payload)
brlcad_add_test(NAME libbsg_action COMMAND test_bsg_action)
brlcad_add_test(NAME libbsg_selection COMMAND test_bsg_selection)
brlcad_add_test(NAME libbsg_camera COMMAND test_bsg_camera)
brlcad_add_test(NAME libbsg_render COMMAND test_bsg_render)
brlcad_add_test(NAME libbsg_settings COMMAND test_bsg_settings)

#
# Phase 4: scene-graph lifecycle (create/sync/destroy/find/sensor_fire)
#
brlcad_addexec(test_bsg_scene_graph test_scene_graph.c "libbsg;libbv;libbu" TEST)
brlcad_add_test(NAME libbsg_scene_graph COMMAND test_bsg_scene_graph)

#
# Phase 6: field accessors, sensors, node taxonomy, payload types
#
brlcad_addexec(test_bsg_phase6 test_phase6.c "libbsg;libbv;libbu" TEST)
brlcad_add_test(NAME libbsg_phase6 COMMAND test_bsg_phase6)

#
# Phase L0: BSG_NODE_LOD lifecycle (create/set_ops/attach_level/cursor)
#
brlcad_addexec(test_bsg_lod_node test_lod_node.c "libbsg;libbv;libbu" TEST)
brlcad_add_test(NAME libbsg_lod_node COMMAND test_bsg_lod_node)

#
# Phase V1: BSG_NODE_VIEW_SCOPE lifecycle (create/visible/destroy)
#
brlcad_addexec(test_bsg_view_scope test_view_scope.c "libbsg;libbv;libbu" TEST)
brlcad_add_test(NAME libbsg_view_scope COMMAND test_bsg_view_scope)

brlcad_addexec(test_bsg_hud test_hud.c "libbsg;libbv;libbu" TEST)
brlcad_add_test(NAME libbsg_hud COMMAND test_bsg_hud)

#
# Phase 10: inline bsg_node_core (kind/parent/identity/revisions/sidecars)
#
brlcad_addexec(test_bsg_node_core test_node_core.c "libbsg;libbv;libbu" TEST)
brlcad_add_test(NAME libbsg_node_core COMMAND test_bsg_node_core)

#
# Slice 5: source identity inline storage (db_dir / source_path / ged_data)
#
brlcad_addexec(test_bsg_source_identity test_source_identity.c "libbsg;libbv;libbu" TEST)
brlcad_add_test(NAME libbsg_source_identity COMMAND test_bsg_source_identity)

#
# Slice 6: owned vlist payloads, text/label payload, axes payload,
#          bsg_vlist_3string / bsg_vlist_2string wrappers
#
brlcad_addexec(test_bsg_slice6 test_bsg_slice6.c "libbsg;libbv;libbu" TEST)
brlcad_add_test(NAME libbsg_slice6 COMMAND test_bsg_slice6)

#
# Slice 7: polygon overlay payload, fill segments, accessor round-trips
#
brlcad_addexec(test_bsg_slice7 test_bsg_slice7.c "libbsg;libbv;libbg;libbu" TEST)
brlcad_add_test(NAME libbsg_slice7 COMMAND test_bsg_slice7)

#
# Slice 8: bounds/transform/camera-query (bsg_node_compute_bound,
#          bsg_view_compute_bounds, bsg_view_select, bsg_view_rect_select)
#
brlcad_addexec(test_bsg_slice8 test_bsg_slice8.c "libbsg;libbv;libbg;libbu" TEST)
brlcad_add_test(NAME libbsg_slice8 COMMAND test_bsg_slice8)

#
# Slice 9: material/appearance/settings migration
#          (bv_view_obj_set_color, bv_obj_sync, bv_view_obj_labels_sync)
#
brlcad_addexec(test_bsg_slice9 test_bsg_slice9.c "libbsg;libbv;libbu" TEST)
brlcad_add_test(NAME libbsg_slice9 COMMAND test_bsg_slice9)

#
# Slice 10: BSG snap API (bsg_snap_params, bsg_snap_lines_3d/2d, bsg_snap_grid_2d)
#
brlcad_addexec(test_bsg_slice10 test_bsg_slice10.c "libbsg;libbv;libbg;libbu" TEST)
brlcad_add_test(NAME libbsg_slice10 COMMAND test_bsg_slice10)

#
# Slice 11: BSG renderer attachment (bsg_renderer_attach, bsg_node_backend_*,
#           bsg_node_stale, BSG_BACKEND_* constants)
#
brlcad_addexec(test_bsg_slice11 test_bsg_slice11.c "libbsg;libbv;libbu" TEST)
brlcad_add_test(NAME libbsg_slice11 COMMAND test_bsg_slice11)

cmakefiles(
  CMakeLists.txt
  test_arb8.c
  test_bsg_node.c
  test_bsg_slice6.c
  test_bsg_slice7.c
  test_bsg_slice8.c
  test_bsg_slice9.c
  test_bsg_slice10.c
  test_bsg_slice11.c
  test_camera.c
  test_hud.c
  test_identity.c
  test_material.c
  test_node_core.c
  test_payload.c
  test_action.c
  test_lod_node.c
  test_phase6.c
  test_render.c
  test_scene_graph.c
  test_selection.c
  test_source_identity.c
  test_view_scope.c
  test_settings.c
  test_vlist.c
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
