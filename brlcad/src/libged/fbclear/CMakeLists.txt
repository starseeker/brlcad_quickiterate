if(BRLCAD_ENABLE_OBOL)
  set(PLUGIN_LIBS libged libbu)
  set(GEDPL_DEFINITIONS BRLCAD_ENABLE_OBOL)
else()
  set(PLUGIN_LIBS libged libdm libbu)
  set(GEDPL_DEFINITIONS "")
endif()

ged_plugin_library(ged-fbclear
  fbclear.c
  LIBS ${PLUGIN_LIBS}
)

cmakefiles(
  CMakeLists.txt
  fbclear.c
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
