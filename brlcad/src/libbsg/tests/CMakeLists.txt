# Tests for libbsg

brlcad_addexec(test_bsg_vlist test_vlist.c "libbsg;libbu" TEST)
brlcad_addexec(test_bsg_arb8  test_arb8.c  "libbsg;libbu" TEST)

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

#
# Phase 4: scene-graph lifecycle (create/sync/destroy/find/sensor_fire)
#
brlcad_addexec(test_bsg_scene_graph test_scene_graph.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_scene_graph COMMAND test_bsg_scene_graph)

#
# Phase 6: field accessors, sensors, node taxonomy, payload types
#
brlcad_addexec(test_bsg_phase6 test_phase6.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_phase6 COMMAND test_bsg_phase6)

#
# Phase L0: BSG_NODE_LOD lifecycle (create/set_ops/attach_level/cursor)
#
brlcad_addexec(test_bsg_lod_node test_lod_node.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_lod_node COMMAND test_bsg_lod_node)

#
# Phase 2: identity/revision model API
#
brlcad_addexec(test_bsg_identity test_identity.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_identity COMMAND test_bsg_identity)

#
# Phase 3: material vs appearance split
#
brlcad_addexec(test_bsg_material_appearance test_material_appearance.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_material_appearance COMMAND test_bsg_material_appearance)

#
# Phase V1: BSG_NODE_VIEW_SCOPE lifecycle (create/visible/destroy)
#
brlcad_addexec(test_bsg_view_scope test_view_scope.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_view_scope COMMAND test_bsg_view_scope)

#
# Phase 1: generic node API (lifecycle/kind/name/child/transform/bounds/revision)
#
brlcad_addexec(test_bsg_node test_node.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_node COMMAND test_bsg_node)

#
# Phase 5: action framework (BBOX/COLLECT/EXPORT)
#
brlcad_addexec(test_bsg_action test_action.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_action COMMAND test_bsg_action)

#
# Phase 6: first-class selection model
#
brlcad_addexec(test_bsg_selection test_selection.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_selection COMMAND test_bsg_selection)

#
# Phase 7: camera snapshot / apply
#
brlcad_addexec(test_bsg_camera test_camera.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_camera COMMAND test_bsg_camera)

#
# Phase 7: renderer-neutral light model
#
brlcad_addexec(test_bsg_light test_light.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_light COMMAND test_bsg_light)

#
# Phase 8: render-request pre-render traversal
#
brlcad_addexec(test_bsg_render test_render.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_render COMMAND test_bsg_render)

#
# Phase D3: typed pick records and pick actions
#
brlcad_addexec(test_bsg_pick test_pick.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_pick COMMAND test_bsg_pick)
brlcad_addexec(test_bsg_snap_action test_snap_action.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_snap_action COMMAND test_bsg_snap_action)
brlcad_addexec(test_bsg_measure test_measure.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_measure COMMAND test_bsg_measure)

# Phase D4: HUD root and overlay metadata
#
brlcad_addexec(test_bsg_hud test_hud.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_hud COMMAND test_bsg_hud)
brlcad_addexec(test_bsg_overlay test_overlay.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_overlay COMMAND test_bsg_overlay)

# Phase D5: render items and backend adapter
#
brlcad_addexec(test_bsg_render_item test_render_item.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_render_item COMMAND test_bsg_render_item)

#
# Phase D5: resolved appearance (appearance_action)
#
brlcad_addexec(test_bsg_appearance_resolve test_appearance_resolve.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_appearance_resolve COMMAND test_bsg_appearance_resolve)

#
# Phase D5: backend adapter invalidation reason mask
#
brlcad_addexec(test_bsg_backend_adapter test_backend_adapter.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_backend_adapter COMMAND test_bsg_backend_adapter)

# Phase D1: typed payload coverage
#
brlcad_addexec(test_bsg_payload_typed test_payload_typed.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_payload_typed COMMAND test_bsg_payload_typed)

# Phase D6: generic live-source (BSG_PL_LIVE) payload contract
#
brlcad_addexec(test_bsg_payload_typed_live_source test_payload_typed_live_source.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_payload_typed_live_source COMMAND test_bsg_payload_typed_live_source)

# Phase D2: draw-intent action API coverage
#
brlcad_addexec(test_bsg_draw_intent_actions test_draw_intent_actions.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_draw_intent_actions COMMAND test_bsg_draw_intent_actions)

cmakefiles(
  CMakeLists.txt
  test_action.c
  test_appearance_resolve.c
  test_arb8.c
  test_backend_adapter.c
  test_camera.c
  test_draw_intent_actions.c
  test_identity.c
  test_light.c
  test_lod_node.c
  test_material_appearance.c
  test_measure.c
  test_node.c
  test_payload_typed.c
  test_payload_typed_live_source.c
  test_phase6.c
  test_pick.c
  test_hud.c
  test_overlay.c
  test_render.c
  test_render_item.c
  test_scene_graph.c
  test_selection.c
  test_snap_action.c
  test_view_scope.c
  test_vlist.c
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
