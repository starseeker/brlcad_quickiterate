# The edit test needs libged + libwdb (to populate the fixture database)
# plus the ged-edit plugin and DbiState (from the ged internals).
brlcad_addexec(ged_test_edit_phase0
  test_edit.cpp
  "libged;libwdb"
  TEST
)
if(TARGET ged_test_edit_phase0)
  add_dependencies(ged_test_edit_phase0 ged_plugins)
  # ged-edit plugin must be present for `edit` command registration
  if(TARGET ged-edit)
    add_dependencies(ged_test_edit_phase0 ged-edit)
  endif()
endif()
brlcad_add_test(NAME ged_test_edit_phase0 COMMAND ged_test_edit_phase0)

# Phase 1: parser foundation, URI, selection, conflict arbiter, edit buffer
brlcad_addexec(ged_test_edit_phase1
  test_edit_phase1.cpp
  "libged;libwdb"
  TEST
)
if(TARGET ged_test_edit_phase1)
  add_dependencies(ged_test_edit_phase1 ged_plugins)
  if(TARGET ged-edit)
    add_dependencies(ged_test_edit_phase1 ged-edit)
  endif()
endif()
brlcad_add_test(NAME ged_test_edit_phase1 COMMAND ged_test_edit_phase1)

cmakefiles(
  CMakeLists.txt
  test_edit.cpp
  test_edit_phase1.cpp
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
