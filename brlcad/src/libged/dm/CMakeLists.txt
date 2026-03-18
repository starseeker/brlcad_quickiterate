set(DM_SRCS
  dm.c
  ert.cpp
  screengrab.c
)

if(BRLCAD_ENABLE_OBOL)
  # In Obol builds the dm_* symbols (dm_open, dm_get_fb, etc.) are absent.
  # dm.c and screengrab.c are compiled with BRLCAD_ENABLE_OBOL defined so
  # their Obol code paths are active; they link against libged+libbu only.
  set(PLUGIN_LIBS libged libbu)
  set(GEDPL_DEFINITIONS BRLCAD_ENABLE_OBOL)
else()
  set(PLUGIN_LIBS libged libdm libbu)
  set(GEDPL_DEFINITIONS "")
endif()

ged_plugin_library(ged-dm
  ${DM_SRCS}
  LIBS ${PLUGIN_LIBS}
)

cmakefiles(
  CMakeLists.txt
  ${DM_SRCS}
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
