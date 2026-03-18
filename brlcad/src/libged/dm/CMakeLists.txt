if(BRLCAD_ENABLE_OBOL)
  # In Obol builds the display-manager command (dm.c) and screengrab (screengrab.c)
  # both call dm_open / dm_* which have no Obol replacement.  Only ert.cpp is
  # needed: its Obol path is already guarded and provides embedded raytracing.
  set(DM_SRCS ert.cpp)
  set(PLUGIN_LIBS libged libbu)
else()
  set(DM_SRCS
    dm.c
    ert.cpp
    screengrab.c
  )
  set(PLUGIN_LIBS libged libdm libbu)
endif()

ged_plugin_library(ged-dm
  ${DM_SRCS}
  LIBS ${PLUGIN_LIBS}
)

cmakefiles(
  CMakeLists.txt
  dm.c
  ert.cpp
  screengrab.c
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
