brlcad_find_package(OSMESA REQUIRED)

set(SWRAST_SRCS dm-swrast.cpp fb-swrast.cpp ../dm-gl.c ../dm-gl_lod.cpp)

# Qt is used for the stand-alone FB window, but other than that should not be
# used by swrast code.  If we are building without Qt, swrast can still be used
# in a "headless" configuration, with screengrab able to retrieve and save
# images of the drawn scene.
# When Obol is the rendering backend, QgSW is no longer compiled into libqtcad,
# so swrastwin.cpp (which uses QgSW) must also be excluded.
if(BRLCAD_ENABLE_QT AND NOT BRLCAD_ENABLE_OBOL)
  find_package_qt(REQUIRED)

  add_definitions(-DSWRAST_QT)

  set(SWRAST_SRCS ${SWRAST_SRCS} swrastwin.cpp)
  if(Qt6Widgets_FOUND)
    qt6_wrap_cpp(swrast_moc_srcs swrastwin.h)
  else()
    qt5_wrap_cpp(swrast_moc_srcs swrastwin.h)
  endif(Qt6Widgets_FOUND)

  distclean(moc_swrastwin.cpp_parameters)
elseif(BRLCAD_ENABLE_QT)
  # Qt available but Obol is the rendering backend; find Qt for any Qt deps
  # in the non-swrastwin sources.
  find_package_qt(REQUIRED)
endif(BRLCAD_ENABLE_QT AND NOT BRLCAD_ENABLE_OBOL)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${BRLCAD_BINARY_DIR}/include
  ${BRLCAD_SOURCE_DIR}/include
  ${BU_INCLUDE_DIRS}
  ${OSMESA_INCLUDE_DIR}
)

add_definitions(-DDM_PLUGIN)

# Use the Mesa mechanism for mangling names so we don't collide with any
# system OpenGL libs
add_definitions(-DUSE_MGL_NAMESPACE)

set(SWRAST_SRCS ${SWRAST_SRCS} ${swrast_moc_srcs})

dm_plugin_library(dm-swrast SHARED ${SWRAST_SRCS})
if(BRLCAD_ENABLE_QT AND NOT BRLCAD_ENABLE_OBOL)
  # libqtcad is needed for QgSW / swrastwin; skip when Obol is active.
  if(Qt6Widgets_FOUND)
    target_link_libraries(
      dm-swrast
      libqtcad
      libdm
      libbu
      ${OSMESA_LIBRARY}
      Qt6::Core
      Qt6::Widgets
    )
  else()
    target_link_libraries(
      dm-swrast
      libqtcad
      libdm
      libbu
      ${OSMESA_LIBRARY}
      Qt5::Core
      Qt5::Widgets
    )
  endif(Qt6Widgets_FOUND)
elseif(BRLCAD_ENABLE_QT)
  # Obol build: no swrastwin, no libqtcad dep needed.
  if(Qt6Widgets_FOUND)
    target_link_libraries(dm-swrast libdm libbu ${OSMESA_LIBRARY} Qt6::Core Qt6::Widgets)
  else()
    target_link_libraries(dm-swrast libdm libbu ${OSMESA_LIBRARY} Qt5::Core Qt5::Widgets)
  endif(Qt6Widgets_FOUND)
else(BRLCAD_ENABLE_QT)
  target_link_libraries(dm-swrast libdm libbu ${OSMESA_LIBRARY})
endif(BRLCAD_ENABLE_QT AND NOT BRLCAD_ENABLE_OBOL)
if(BRLCAD_ENABLE_OBOL)
  set_property(TARGET dm-swrast APPEND PROPERTY COMPILE_DEFINITIONS BRLCAD_ENABLE_OBOL)
endif(BRLCAD_ENABLE_OBOL)
set_property(TARGET dm-swrast APPEND PROPERTY COMPILE_DEFINITIONS BRLCADBUILD HAVE_CONFIG_H OSMESA)
validate_style(dm-swrast "${SWRAST_SRCS}")

plugin_setup(dm-swrast dm)

cmakefiles(
  CMakeLists.txt
  ${SWRAST_SRCS}
  swrastwin.cpp
  dm-swrast.h
  swrastwin.h
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
