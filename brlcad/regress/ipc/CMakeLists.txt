# Regression tests for bu_ipc + fbs_open_ipc + libtclcad IPC path.
#
# Tests the code added in the "Expand bu_ipc usage" PR:
#
#   Phase 1: fbs_open_ipc() + fbs_ipc_child_addr_env()  (libdm/fbserv.c)
#   Phase 2: BU_IPC_ADDR env-var detection               (libdm/if_remote.c)
#   Phase 4: Tcl_CreateFileHandler callback mechanism    (libtclcad/fbserv.c)
#   Phase 7: fbserv -I <addr> flag                       (src/fbserv/fbserv.c)
#
# The test is headless (no display required) and self-contained.

if(TARGET libbu AND TARGET libdm AND TARGET libpkg AND TARGET libtclcad AND TARGET fbserv)

  set(regress_ipc_libs libbu libdm libpkg libtclcad ${TCL_LIBRARY})

  brlcad_addexec(regress_ipc regress_ipc.cpp "${regress_ipc_libs}" TEST)
  if(TARGET regress_ipc)
    set_target_properties(regress_ipc PROPERTIES FOLDER "BRL-CAD Regression Tests/ipc")

    # Tell the test where to find the fbserv binary
    brlcad_add_test(
      NAME    regress-ipc
      COMMAND regress_ipc --verbose
    )
    set_tests_properties(regress-ipc PROPERTIES
      ENVIRONMENT "FBSERV_BIN=$<TARGET_FILE:fbserv>;LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/lib"
      TIMEOUT 120
    )
    brlcad_regression_test(regress-ipc "regress_ipc;fbserv;libbu;libdm;libpkg;libtclcad" STAND_ALONE TEST_DEFINED)
    distclean(${CMAKE_CURRENT_BINARY_DIR}/regress-ipc.log)
    distclean(${CMAKE_CURRENT_BINARY_DIR}/regress_ipc_fbserv.log)

  endif(TARGET regress_ipc)

endif(TARGET libbu AND TARGET libdm AND TARGET libpkg AND TARGET libtclcad AND TARGET fbserv)

cmakefiles(
  CMakeLists.txt
  regress_ipc.cpp
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
