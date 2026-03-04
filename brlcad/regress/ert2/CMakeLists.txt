# regress/ert2/CMakeLists.txt
#
# Regression tests for the ert2 and ert3 GED commands.
#
# These tests exercise the commands end-to-end through the GED/mged interface
# to verify that they are properly wired in, report the right errors when
# the framebuffer infrastructure is absent (batch mode), and reject invalid
# arguments correctly.

cmake_minimum_required(VERSION 3.12)

# The mged executable is available as MGED in the BRL-CAD build system.
if(NOT TARGET mged)
  return()
endif()

set(TNAME ert2)
set(LOG_FILE "${CMAKE_CURRENT_BINARY_DIR}/regress_ert2.log")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/regress-ert2.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/regress_ert2.cmake"
  @ONLY
)

add_test(
  NAME regress-ert2
  COMMAND "${CMAKE_COMMAND}"
    -DEXEC=$<TARGET_FILE:mged>
    -P "${CMAKE_CURRENT_BINARY_DIR}/regress_ert2.cmake"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
set_tests_properties(regress-ert2 PROPERTIES
  LABELS "regression"
  TIMEOUT 120
)

cmakefiles(
  CMakeLists.txt
  regress-ert2.cmake.in
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
