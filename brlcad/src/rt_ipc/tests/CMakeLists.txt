# src/rt_ipc/tests/CMakeLists.txt
#
# End-to-end protocol tests for the rt_ipc executable.

brlcad_addexec(test_rt_ipc_proto
  test_rt_ipc_proto.cpp
  "librtrender;libbu"
  TEST
  FOLDER Test
)

add_test(
  NAME rt_ipc-protocol
  COMMAND test_rt_ipc_proto
    "$<TARGET_FILE:rt_ipc>"
    "$<TARGET_FILE_DIR:mged>/../share/db/moss.g"
)
set_tests_properties(rt_ipc-protocol PROPERTIES
  LABELS "rt_ipc"
  TIMEOUT 120
  DEPENDS "rt_ipc;moss.g"
)

cmakefiles(
  CMakeLists.txt
  test_rt_ipc_proto.cpp
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
