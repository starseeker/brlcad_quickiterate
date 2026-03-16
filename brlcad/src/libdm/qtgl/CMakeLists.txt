set(QTGL_SRCS dm-qtgl.cpp fb-qtgl.cpp)

# qtglwin.cpp creates a stand-alone Qt window backed by QgGL.  When Obol is
# the rendering backend, QgGL is no longer compiled into libqtcad, so we must
# exclude qtglwin.cpp as well.  fb-qtgl.cpp guards the QgGLWin code with
# BRLCAD_ENABLE_OBOL already.
if(NOT BRLCAD_ENABLE_OBOL)
  list(APPEND QTGL_SRCS qtglwin.cpp)
endif()

if(BRLCAD_ENABLE_QT AND BRLCAD_ENABLE_OPENGL)
  find_package_qt(REQUIRED)

  include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${BRLCAD_BINARY_DIR}/include
    ${BRLCAD_SOURCE_DIR}/include
    ${BU_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
  )

  set_property(SOURCE dm-qtgl.c APPEND PROPERTY COMPILE_DEFINITIONS FB_USE_INTERNAL_API)
  set_property(SOURCE fb-qtgl.c APPEND PROPERTY COMPILE_DEFINITIONS FB_USE_INTERNAL_API)

  add_definitions(-DDM_PLUGIN)

  if(OPENGL_opengl_LIBRARY)
    set(OPENGL_LIBS ${OPENGL_opengl_LIBRARY})
  endif(OPENGL_opengl_LIBRARY)
  if(OPENGL_gl_LIBRARY)
    set(OPENGL_LIBS ${OPENGL_gl_LIBRARY})
  endif(OPENGL_gl_LIBRARY)

  if(NOT BRLCAD_ENABLE_OBOL)
    if(Qt6Widgets_FOUND)
      qt6_wrap_cpp(qtgl_moc_srcs qtglwin.h)
    else()
      qt5_wrap_cpp(qtgl_moc_srcs qtglwin.h)
    endif(Qt6Widgets_FOUND)
    list(APPEND QTGL_SRCS ${qtgl_moc_srcs})
  endif(NOT BRLCAD_ENABLE_OBOL)

  dm_plugin_library(dm-qtgl SHARED ${QTGL_SRCS})
  if(Qt6Widgets_FOUND)
    set(_qtgl_libs libdmgl libdm libbu Qt6::Core Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets ${OPENGL_LIBS})
  else()
    set(_qtgl_libs libdmgl libdm libbu Qt5::Core Qt5::Widgets Qt5::OpenGL ${OPENGL_LIBS})
  endif(Qt6Widgets_FOUND)
  if(NOT BRLCAD_ENABLE_OBOL)
    # qtglwin uses QgGL from libqtcad; only needed in non-Obol builds.
    list(INSERT _qtgl_libs 0 libqtcad)
  endif(NOT BRLCAD_ENABLE_OBOL)
  target_link_libraries(dm-qtgl ${_qtgl_libs})
  if(BRLCAD_ENABLE_OBOL)
    set_property(TARGET dm-qtgl APPEND PROPERTY COMPILE_DEFINITIONS BRLCAD_ENABLE_OBOL)
  endif(BRLCAD_ENABLE_OBOL)
  set_property(TARGET dm-qtgl APPEND PROPERTY COMPILE_DEFINITIONS BRLCADBUILD HAVE_CONFIG_H)
  validate_style(dm-qtgl "${QTGL_SRCS}")

  plugin_setup(dm-qtgl dm)

  distclean(moc_qtglwin.cpp_parameters)
endif(BRLCAD_ENABLE_QT AND BRLCAD_ENABLE_OPENGL)

cmakefiles(
  CMakeLists.txt
  ${QTGL_SRCS}
  dm-qtgl.h
  qtglwin.h
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
