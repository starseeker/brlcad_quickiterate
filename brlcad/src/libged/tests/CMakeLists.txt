brlcad_addexec(ged_test_tops test_tops.c libged TEST)
if(TARGET ged_test_tops)
  add_dependencies(ged_test_tops ged_plugins)
endif(TARGET ged_test_tops)
brlcad_add_test(NAME ged_test_tops_moss COMMAND ged_test_tops "${CMAKE_CURRENT_SOURCE_DIR}/ged_tests.g")

brlcad_addexec(ged_test_rm ged_test_rm.c "libged;libwdb;librt;libbu" TEST)
if(TARGET ged_test_rm)
  add_dependencies(ged_test_rm ged_plugins)
endif(TARGET ged_test_rm)
brlcad_add_test(NAME ged_test_rm COMMAND ged_test_rm)
if(TARGET ged_test_rm)
  set_property(TEST ged_test_rm APPEND PROPERTY ENVIRONMENT BRLCAD_ROOT=${CMAKE_BINARY_DIR})
endif(TARGET ged_test_rm)
distclean(${CMAKE_CURRENT_BINARY_DIR}/ged_test_rm.g)

brlcad_addexec(ged_test_list test_list.c libged TEST)
if(TARGET ged_test_list)
  add_dependencies(ged_test_list ged_plugins)
endif(TARGET ged_test_list)
brlcad_add_test(NAME ged_test_list COMMAND ged_test_list 2)

brlcad_addexec(ged_test_gqa test_gqa.c libged TEST)
if(TARGET ged_test_gqa)
  add_dependencies(ged_test_gqa ged_plugins)
endif(TARGET ged_test_gqa)

brlcad_addexec(ged_test_plugins plugins.cpp libged TEST)
if(TARGET ged_test_plugins)
  add_dependencies(ged_test_plugins ged_plugins)
endif(TARGET ged_test_plugins)

brlcad_addexec(ged_test_cmd_schemas test_cmd_schemas.cpp libged TEST)
if(TARGET ged_test_cmd_schemas)
  add_dependencies(ged_test_cmd_schemas ged_plugins)
endif(TARGET ged_test_cmd_schemas)
brlcad_add_test(NAME ged_test_cmd_schemas COMMAND ged_test_cmd_schemas "${CMAKE_CURRENT_SOURCE_DIR}/ged_tests.g")
if(TARGET ged_test_cmd_schemas)
  set_property(TEST ged_test_cmd_schemas APPEND PROPERTY ENVIRONMENT BRLCAD_ROOT=${CMAKE_BINARY_DIR})
endif(TARGET ged_test_cmd_schemas)

brlcad_addexec(ged_test_material test_material.c libged TEST)
if(TARGET ged_test_material)
  add_dependencies(ged_test_material ged_plugins)
endif(TARGET ged_test_material)
brlcad_add_test(NAME ged_test_material COMMAND ged_test_material "${CMAKE_CURRENT_BINARY_DIR}/ged_test_material.g")
if(TARGET ged_test_material)
  set_property(TEST ged_test_material APPEND PROPERTY ENVIRONMENT BRLCAD_ROOT=${CMAKE_BINARY_DIR})
endif(TARGET ged_test_material)
distclean(${CMAKE_CURRENT_BINARY_DIR}/ged_test_material.g)

brlcad_addexec(ged_test_select test_select.cpp libged TEST)
if(TARGET ged_test_select)
  add_dependencies(ged_test_select ged_plugins)
endif(TARGET ged_test_select)

# Phase 0-C: DbiState unit test (drawing_stack_modernization.txt)
# Tests DbiState construction, bu_cache round-trip, get_bbox,
# BViewState::add_path / redraw / count_drawn_paths.
# Uses moss.g from tests/draw/ as the known .g fixture.
brlcad_addexec(ged_test_dbi_cpp test_dbi_cpp.cpp libged TEST)
if(TARGET ged_test_dbi_cpp)
  add_dependencies(ged_test_dbi_cpp ged_plugins)
endif(TARGET ged_test_dbi_cpp)
brlcad_add_test(NAME ged_test_dbi_cpp COMMAND ged_test_dbi_cpp "${CMAKE_CURRENT_SOURCE_DIR}/draw")
if(TARGET ged_test_dbi_cpp)
  set_property(TEST ged_test_dbi_cpp APPEND PROPERTY ENVIRONMENT BRLCAD_ROOT=${CMAKE_BINARY_DIR})
endif(TARGET ged_test_dbi_cpp)
distclean(${CMAKE_CURRENT_BINARY_DIR}/moss_dbi_tmp.g)
distclean(${CMAKE_CURRENT_BINARY_DIR}/ged_dbi_cpp_test_cache)

brlcad_addexec(ged_test_search test_search.c libged TEST)
if(TARGET ged_test_search)
  add_dependencies(ged_test_search ged_plugins)
endif(TARGET ged_test_search)
brlcad_add_test(NAME ged_test_search COMMAND ged_test_search "${CMAKE_CURRENT_SOURCE_DIR}/search_tests.g")

# db_search testing
# TODO - tests the C api, but uses libged - either need to limit our dependencies
# to librt, or rename test.
brlcad_addexec(rt_search_test search_tests.cpp "librt;libbu;libged" TEST)
brlcad_add_test(NAME rt_search_tests COMMAND rt_search_test "${CMAKE_CURRENT_SOURCE_DIR}/rt_search_tests.g")

brlcad_addexec(ged_check_prim_cmds check_prim_cmds.cpp libged TEST)
if(TARGET ged_check_prim_cmds)
  add_dependencies(ged_check_prim_cmds ged_plugins)
endif(TARGET ged_check_prim_cmds)
brlcad_add_test(NAME ged_test_prim_cmds_exist COMMAND ged_check_prim_cmds)

brlcad_addexec(ged_test_mann_docs check_mann_docs.cpp "" TEST)
brlcad_add_test(NAME ged_test_mann_docs COMMAND ged_test_mann_docs "${CMAKE_SOURCE_DIR}")

add_subdirectory(bot)
add_subdirectory(draw)
add_subdirectory(edit)

# Phase 3.5 DrawPipeline concurrent stress test.
# Exercises lock-free queue workers under rapid multi-batch push load.
brlcad_addexec(ged_test_draw_pipeline_stress draw_pipeline_stress.cpp "libged;libdm" TEST)
if(TARGET ged_test_draw_pipeline_stress)
  add_dependencies(ged_test_draw_pipeline_stress ged_plugins dm-swrast)
  target_include_directories(ged_test_draw_pipeline_stress PRIVATE
    "${CMAKE_SOURCE_DIR}/src/libged"
  )
endif(TARGET ged_test_draw_pipeline_stress)
brlcad_add_test(NAME ged_test_draw_pipeline_stress
  COMMAND ged_test_draw_pipeline_stress
    -c "${CMAKE_CURRENT_SOURCE_DIR}/draw/moss.g")
if(TARGET ged_test_draw_pipeline_stress)
  set_property(TEST ged_test_draw_pipeline_stress
    APPEND PROPERTY ENVIRONMENT BRLCAD_ROOT=${CMAKE_BINARY_DIR})
endif(TARGET ged_test_draw_pipeline_stress)
distclean(${CMAKE_CURRENT_BINARY_DIR}/dp_stress_cache)

# Note: it is particularly important that the lint tests in particular are kept
# in a separate file, since its bad input examples stand an excellent chance of
# breaking commands.
cmakefiles(
  lint_tests.g
  rt_search_tests.g
  search_tests.g
  obb_tests.g
  ged_tests.g
  test_cmd_schemas.cpp
  pnts_in_out.g
)

cmakefiles(CMakeLists.txt)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
