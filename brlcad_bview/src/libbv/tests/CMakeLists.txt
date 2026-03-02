# To minimize the number of build targets and binaries that are created, we
# combine most (not all) of the unit tests into a single program.

set(bview_test_srcs list.c scene.c vlist.c)

# Generate and assemble the necessary per-test-type source code
set(BVIEW_TEST_SRC_INCLUDES)
set(BVIEW_TEST_SRC_SWITCHES)
set(BVIEW_TEST_LIST)
foreach(srcfile ${bview_test_srcs})
  get_filename_component(NAME_ROOT ${srcfile} NAME_WE)
  set(BVIEW_TEST_SRC_INCLUDES "${BVIEW_TEST_SRC_INCLUDES}\n#include \"libbv/tests/${srcfile}\"")
  set(
    BVIEW_TEST_SRC_SWITCHES
    "${BVIEW_TEST_SRC_SWITCHES}\n   if (BU_STR_EQUAL(av[0], \"${NAME_ROOT}\")) return ${NAME_ROOT}_main(ac,av);"
  )
  set(BVIEW_TEST_LIST "${BVIEW_TEST_LIST}\\n  ${NAME_ROOT}")
endforeach(srcfile ${bview_test_srcs})

# Generate bview_test.c file and build it
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../..)
configure_file(bview_test.c.in "${CMAKE_CURRENT_BINARY_DIR}/bview_test.c" @ONLY)
distclean("${CMAKE_CURRENT_BINARY_DIR}/bview_test.c")
brlcad_addexec(bview_test "${CMAKE_CURRENT_BINARY_DIR}/bview_test.c" libbv TEST)

# Housekeeping for included source files
cmakefiles(${bview_test_srcs})
validate_style("${bview_test_srcs}" bview_test)

# This test program is being kept separate because the plot3 logic really
# should move, and when it does it will be easier to move the test if it
# is not tied into the bview_test target
brlcad_addexec(bview_plot3 plot3.c "libbu;libbv" TEST)

#
#  *************** plot3.c ***************
#
#  Tests to detect validity/invalidity of plot3 files
#
brlcad_add_test(NAME bview_plot3_valid COMMAND bview_plot3 -b "${CMAKE_CURRENT_SOURCE_DIR}/valid.plot3")
brlcad_add_test(NAME bview_plot3_invalid COMMAND bview_plot3 -i -b "${CMAKE_CURRENT_SOURCE_DIR}/invalid.plot3")

#
#  ************ list.c tests *************
#
# Format is:
#  COMMAND bview_test list  <2 or 3 (the dimension)>  X0,Y0[,Z0] X1,Y1[,Z1] Xn,Yn[,Zn]
brlcad_add_test(NAME bview_list_2d_null                COMMAND bview_test list  2) # NULL list of points
brlcad_add_test(NAME bview_list_2d_1                   COMMAND bview_test list  2  0,0)
brlcad_add_test(NAME bview_list_2d_2                   COMMAND bview_test list  2  0,0 1.64,1)
brlcad_add_test(NAME bview_list_2d_3                   COMMAND bview_test list  2  3,7 1,-5)
brlcad_add_test(NAME bview_list_2d_4                   COMMAND bview_test list  2  3,7 1,-5.321 67,0 0,100)

brlcad_add_test(NAME bview_list_3d_null                COMMAND bview_test list  3) # NULL list of points
brlcad_add_test(NAME bview_list_3d_1                   COMMAND bview_test list  3  0,0,0)
brlcad_add_test(NAME bview_list_3d_2                   COMMAND bview_test list  3  0,0,3 1.64,1,4)
brlcad_add_test(NAME bview_list_3d_3                   COMMAND bview_test list  3  3,7,8 1,-5,8)
brlcad_add_test(NAME bview_list_3d_4                   COMMAND bview_test list  3  3,7,8 1,-5.321,3 67,0,67 0,100,23)

#
#  *************** vlist.c ***************
#
# For vlist_cnt, input is as follows:
# vlist_cnt <args>

# the <args> format is as follows:
# expected_result
#
brlcad_add_test(NAME bview_vlist_cmd_cnt_NULL COMMAND bview_test vlist -1)
brlcad_add_test(NAME bview_vlist_cmd_cnt_0 COMMAND bview_test vlist 0)
brlcad_add_test(NAME bview_vlist_cmd_cnt_1 COMMAND bview_test vlist 5)
brlcad_add_test(NAME bview_vlist_cmd_cnt_15 COMMAND bview_test vlist 15)
brlcad_add_test(NAME bview_vlist_cmd_cnt_45 COMMAND bview_test vlist 45)
brlcad_add_test(NAME bview_vlist_cmd_cnt_500 COMMAND bview_test vlist 500)

cmakefiles(
  CMakeLists.txt
  bview_test.c.in
  valid.plot3
  invalid.plot3
)

#
#  *************** scene.c ***************
#
#  Tests for the new bv_node / bv_scene / bview_new API.
#  Each test name matches the second argument passed to scene_main().
#
set(bview_scene_tests
  node_create_destroy
  node_types
  node_null_safety
  node_transform
  node_visibility
  node_user_data
  node_hierarchy
  node_world_transform
  node_traverse
  scene_create_destroy
  scene_null_safety
  scene_add_remove
  scene_add_child
  scene_find_node
  scene_traverse
  scene_default_camera
  view_create_destroy
  view_null_safety
  view_camera
  view_viewport
  view_appearance
  view_overlay
  view_redraw_callback
  view_scene_assoc
  view_camera_node
  view_migration
  settings_apply
  settings_apply_null
  settings_apply_idempotent
  obj_to_node_null
  obj_to_node_basic
  obj_to_node_children
  scene_from_view_null
  scene_from_view_empty
  scene_from_view_objs
  scene_from_vset_null
  scene_from_vset_empty
  to_old_appearance
  to_old_overlay
  to_old_null
  node_bbox_null
  node_bbox_no_geom
  scene_bbox_null
  scene_bbox_empty
  scene_bbox_invisible
  scene_bbox_visible
  autoview_null
  autoview_empty
  autoview_single_obj
  autoview_scale_factor
  lod_node_update_null
  lod_node_update_non_geom
  lod_node_update_no_obj
  lod_node_update_stale
  node_geometry_get
  node_selected
  scene_lod_update_null
  scene_lod_update_empty
  scene_lod_update_counts
  selected_nodes_null
  selected_nodes_empty
  selected_nodes_count
  deselect_all
  select_node
)
foreach(tname ${bview_scene_tests})
  brlcad_add_test(NAME bview_scene_${tname} COMMAND bview_test scene ${tname})
endforeach(tname ${bview_scene_tests})

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
