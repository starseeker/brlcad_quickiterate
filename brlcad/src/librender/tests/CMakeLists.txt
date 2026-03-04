# src/librender/tests/CMakeLists.txt
#
# Unit tests for librtrender (render_run synchronous path).

brlcad_addexec(test_render_run
  test_render_run.cpp
  "librtrender;librt;libbu"
  TEST
  FOLDER Test
)

# The test needs a valid .g database.  Use the build-time-generated moss.g
# which is placed in share/db/ by the BRL-CAD build system.
add_test(
  NAME librtrender-render_run
  COMMAND test_render_run
    "$<TARGET_FILE_DIR:mged>/../share/db/moss.g"
)
set_tests_properties(librtrender-render_run PROPERTIES
  LABELS "librender"
  TIMEOUT 120
  DEPENDS "moss.g"
)

cmakefiles(
  CMakeLists.txt
  test_render_run.cpp
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
