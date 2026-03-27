# Tests for remrt (network distributed ray tracing)
#
# Two sub-tests are executed by a single C++ orchestrator binary:
#
#   1. Token-authenticated render (512x512, -M script, -S token workers):
#      3 local rtsrv workers each authenticate with the session token
#      printed by remrt.  Output is compared pixel-for-pixel against
#      bench/ref/m35.pix to verify correct distributed assembly.
#
#   2. Token-less backward-compat render (64x64, default az/el, no token):
#      3 local rtsrv workers connect without presenting a token.  Verifies
#      that remrt still accepts unauthenticated workers (compat mode) and
#      produces a correctly-sized output.
#
# rtsrv workers are started with the -d (debug/foreground) flag so the
# orchestrator can wait for them to exit; daemonization is suppressed.
# All workers connect passively to remrt's TCP port — no SSH is used.
#
# The test uses the STAND_ALONE label so CTest does not schedule it in
# parallel with other tests that might occupy the same port range (4446+).

if(TARGET remrt AND TARGET rtsrv AND TARGET pixcmp)

  # Build the test orchestrator binary.
  brlcad_addexec(regress_remrt regress_remrt.cpp "libbu;Threads::Threads" TEST)
  if(TARGET regress_remrt)

    set_target_properties(regress_remrt PROPERTIES FOLDER "BRL-CAD Regression Tests/remrt")

    # Paths substituted at cmake configure time.
    set(M35G_PATH "${PROJECT_BINARY_DIR}/${DATA_DIR}/db/m35.g")
    set(REFPIX_PATH "${PROJECT_SOURCE_DIR}/bench/ref/m35.pix")

    brlcad_add_test(
      NAME regress-remrt
      COMMAND regress_remrt
        --remrt  $<TARGET_FILE:remrt>
        --rtsrv  $<TARGET_FILE:rtsrv>
        --pixcmp $<TARGET_FILE:pixcmp>
        --m35g   "${M35G_PATH}"
        --refpix "${REFPIX_PATH}"
    )
    # Set the working directory to this CMake binary dir so that both the
    # test orchestrator and the remrt child process find ".remrtrc" in the
    # same known location.  bu_dir(BU_DIR_CURR) in the test code and
    # bu_dir(BU_DIR_CURR) in remrt's read_rc_file() both resolve to this dir.
    set_tests_properties(regress-remrt PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )

    # STAND_ALONE: prevents parallel execution with other tests that
    # might share port 4446.  TIMEOUT 600: a 512x512 distributed render
    # plus startup overhead may take several minutes.
    brlcad_regression_test(
      regress-remrt "remrt;rtsrv;pixcmp;m35.g;regress_remrt"
      TEST_DEFINED TIMEOUT 600
    )

    distclean(
      ${CMAKE_CURRENT_BINARY_DIR}/.remrtrc
      ${CMAKE_CURRENT_BINARY_DIR}/m35_remrt_token.pix
      ${CMAKE_CURRENT_BINARY_DIR}/m35_remrt_token.pix.0
      ${CMAKE_CURRENT_BINARY_DIR}/m35_remrt_notoken.pix
      ${CMAKE_CURRENT_BINARY_DIR}/m35_remrt_notoken.pix.0
    )

  endif(TARGET regress_remrt)

endif(TARGET remrt AND TARGET rtsrv AND TARGET pixcmp)

cmakefiles(
  CMakeLists.txt
  regress_remrt.cpp
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
