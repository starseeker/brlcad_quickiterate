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

cmakefiles(
  CMakeLists.txt
  test_arb8.c
  test_bsg_node.c
  test_camera.c
  test_identity.c
  test_material.c
  test_payload.c
  test_action.c
  test_lod_node.c
  test_phase6.c
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
