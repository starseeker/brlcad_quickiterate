# libbsg tests — migrated from libbv/tests.

set(bsg_test_srcs list.c vlist.c arb8.c)

set(BVIEW_TEST_SRC_INCLUDES)
set(BVIEW_TEST_SRC_SWITCHES)
set(BVIEW_TEST_LIST)
foreach(srcfile ${bsg_test_srcs})
  get_filename_component(NAME_ROOT ${srcfile} NAME_WE)
  set(BVIEW_TEST_SRC_INCLUDES "${BVIEW_TEST_SRC_INCLUDES}\n#include \"libbsg/tests/${srcfile}\"")
  set(
    BVIEW_TEST_SRC_SWITCHES
    "${BVIEW_TEST_SRC_SWITCHES}\n   if (BU_STR_EQUAL(av[0], \"${NAME_ROOT}\")) return ${NAME_ROOT}_main(ac,av);"
  )
  set(BVIEW_TEST_LIST "${BVIEW_TEST_LIST}\\n  ${NAME_ROOT}")
endforeach(srcfile ${bsg_test_srcs})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../..)
configure_file(bsg_test.c.in "${CMAKE_CURRENT_BINARY_DIR}/bsg_test.c" @ONLY)
distclean("${CMAKE_CURRENT_BINARY_DIR}/bsg_test.c")
brlcad_addexec(bsg_test "${CMAKE_CURRENT_BINARY_DIR}/bsg_test.c" libbsg TEST)

cmakefiles(${bsg_test_srcs})
validate_style("${bsg_test_srcs}" bsg_test)

brlcad_addexec(bsg_plot3 plot3.c "libbu;libbsg" TEST)

brlcad_add_test(NAME bsg_plot3_valid COMMAND bsg_plot3 -b "${CMAKE_CURRENT_SOURCE_DIR}/valid.plot3")
brlcad_add_test(NAME bsg_plot3_invalid COMMAND bsg_plot3 -i -b "${CMAKE_CURRENT_SOURCE_DIR}/invalid.plot3")

brlcad_add_test(NAME bsg_list_2d_null  COMMAND bsg_test list  2)
brlcad_add_test(NAME bsg_list_2d_1     COMMAND bsg_test list  2  0,0)
brlcad_add_test(NAME bsg_list_2d_2     COMMAND bsg_test list  2  0,0 1.64,1)
brlcad_add_test(NAME bsg_list_2d_3     COMMAND bsg_test list  2  3,7 1,-5)
brlcad_add_test(NAME bsg_list_2d_4     COMMAND bsg_test list  2  3,7 1,-5.321 67,0 0,100)
brlcad_add_test(NAME bsg_list_3d_null  COMMAND bsg_test list  3)
brlcad_add_test(NAME bsg_list_3d_1     COMMAND bsg_test list  3  0,0,0)
brlcad_add_test(NAME bsg_list_3d_2     COMMAND bsg_test list  3  0,0,3 1.64,1,4)
brlcad_add_test(NAME bsg_list_3d_3     COMMAND bsg_test list  3  3,7,8 1,-5,8)
brlcad_add_test(NAME bsg_list_3d_4     COMMAND bsg_test list  3  3,7,8 1,-5.321,3 67,0,67 0,100,23)

brlcad_add_test(NAME bsg_vlist_cmd_cnt_NULL COMMAND bsg_test vlist -1)
brlcad_add_test(NAME bsg_vlist_cmd_cnt_0    COMMAND bsg_test vlist 0)
brlcad_add_test(NAME bsg_vlist_cmd_cnt_1    COMMAND bsg_test vlist 5)
brlcad_add_test(NAME bsg_vlist_cmd_cnt_15   COMMAND bsg_test vlist 15)
brlcad_add_test(NAME bsg_vlist_cmd_cnt_45   COMMAND bsg_test vlist 45)
brlcad_add_test(NAME bsg_vlist_cmd_cnt_500  COMMAND bsg_test vlist 500)

brlcad_add_test(NAME bsg_vlist_arb8_cmd_cnt COMMAND bsg_test arb8)

cmakefiles(
  CMakeLists.txt
  bsg_test.c.in
  valid.plot3
  invalid.plot3
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
