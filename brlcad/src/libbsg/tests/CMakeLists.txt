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

# Phase D4: HUD root and overlay metadata
#
brlcad_addexec(test_bsg_hud test_hud.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_hud COMMAND test_bsg_hud)

# Phase D5: render items and backend adapter
#
brlcad_addexec(test_bsg_render_item test_render_item.c "libbsg;libbu" TEST)
brlcad_add_test(NAME libbsg_render_item COMMAND test_bsg_render_item)

cmakefiles(
  CMakeLists.txt
  test_action.c
  test_arb8.c
  test_camera.c
  test_identity.c
  test_light.c
  test_lod_node.c
  test_material_appearance.c
  test_node.c
  test_phase6.c
  test_pick.c
  test_hud.c
  test_render.c
  test_render_item.c
  test_scene_graph.c
  test_selection.c
  test_view_scope.c
  test_vlist.c
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
