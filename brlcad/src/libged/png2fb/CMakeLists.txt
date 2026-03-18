if(BRLCAD_ENABLE_OBOL)
  # png2fb Obol path uses icv to decode PNG; no libdm needed.
  set(PLUGIN_LIBS libged libicv libbu)
  set(GEDPL_DEFINITIONS BRLCAD_ENABLE_OBOL)
else()
  set(PLUGIN_LIBS libged libdm libbu)
  set(GEDPL_DEFINITIONS "")
endif()

ged_plugin_library(ged-png2fb
  png2fb.c
  LIBS ${PLUGIN_LIBS}
)

cmakefiles(
  CMakeLists.txt
  png2fb.c
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
