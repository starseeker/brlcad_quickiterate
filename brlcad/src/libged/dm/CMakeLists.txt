set(PLUGIN_LIBS libged libdm librtrender libbu)

set(DM_SRCS
  dm.c
  dm2.cpp
  ert.cpp
  ert2.cpp
  ert3.cpp
  screengrab.c
)

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
