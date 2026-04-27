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

# Phase 2: rotate / translate / tra / scale / checkpoint / revert / reset / mat
brlcad_addexec(ged_test_edit_phase2
  test_edit_phase2.cpp
  "libged;libwdb"
  TEST
)
if(TARGET ged_test_edit_phase2)
  add_dependencies(ged_test_edit_phase2 ged_plugins)
  if(TARGET ged-edit)
    add_dependencies(ged_test_edit_phase2 ged-edit)
  endif()
endif()
brlcad_add_test(NAME ged_test_edit_phase2 COMMAND ged_test_edit_phase2)

# Phase A: DbiState null-safety — edit command must work without DbiState
brlcad_addexec(ged_test_edit_phase_a
  test_edit_phase_a.cpp
  "libged;libwdb"
  TEST
)
if(TARGET ged_test_edit_phase_a)
  add_dependencies(ged_test_edit_phase_a ged_plugins)
  if(TARGET ged-edit)
    add_dependencies(ged_test_edit_phase_a ged-edit)
  endif()
endif()
brlcad_add_test(NAME ged_test_edit_phase_a COMMAND ged_test_edit_phase_a)

# Phase C: rotate axis-mode, rotate -c center, scale -c center, per-axis scale
brlcad_addexec(ged_test_edit_phase_c
  test_edit_phase_c.cpp
  "libged;libwdb"
  TEST
)
if(TARGET ged_test_edit_phase_c)
  add_dependencies(ged_test_edit_phase_c ged_plugins)
  if(TARGET ged-edit)
    add_dependencies(ged_test_edit_phase_c ged-edit)
  endif()
endif()
brlcad_add_test(NAME ged_test_edit_phase_c COMMAND ged_test_edit_phase_c)

cmakefiles(
  CMakeLists.txt
  test_edit.cpp
  test_edit_phase1.cpp
  test_edit_phase2.cpp
  test_edit_phase_a.cpp
  test_edit_phase_c.cpp
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
