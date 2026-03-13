if(BRLCAD_ENABLE_QT)
  find_package_qt(REQUIRED)
endif(BRLCAD_ENABLE_QT)

# Find Obol from the bext dependency tree.
# Obol_DIR tells CMake where to look for obol-config.cmake; the
# brlcad_find_package macro sets the search root to CMAKE_BINARY_DIR
# which is populated from BRLCAD_EXT_DIR/install by brlcad_bext_process().
if(BRLCAD_ENABLE_QT)
  brlcad_find_package(Obol)
  if(TARGET Obol::Obol)
    set(BRLCAD_ENABLE_OBOL ON CACHE BOOL "Obol scene-graph library available" FORCE)
    message(STATUS "Obol found — Obol-based rendering enabled in qged")
  else()
    set(BRLCAD_ENABLE_OBOL OFF CACHE BOOL "Obol scene-graph library available" FORCE)
    message(STATUS "Obol NOT found — qged will use libdm rendering")
  endif()
endif(BRLCAD_ENABLE_QT)

# TODO - need this now for code in lower level directories,
# need to do some cleanup.
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

# NOTE: We can't use the CMake global values for automoc et. al.
# because most of BRL-CAD's targets are not Qt targets.  We don't
# want to run the automatic moc logic for all of them.

set(
  qged_srcs
  QgEdMainWindow.cpp
  QgEdApp.cpp
  QgEdFilter.cpp
  fbserv.cpp
  QgEdPalette.cpp
)

set(
  qth_names
  QgEdApp
  QgEdFilter
  fbserv
  QgEdMainWindow
  QgEdPalette
)
foreach(qh ${qth_names})
  set(moc_headers ${moc_headers} ${qh}.h)
endforeach(qh ${qth_names})

if(BRLCAD_ENABLE_QT)
  # Let the plugin lookup know what the platform's extension is so it knows
  # what plugin filenames to look for
  set_property(
    SOURCE QgEdPalette.cpp
    APPEND
    PROPERTY COMPILE_DEFINITIONS "QGED_PLUGIN_SUFFIX=\"${CMAKE_SHARED_LIBRARY_SUFFIX}\""
  )

  if(Qt6Widgets_FOUND)
    qt6_wrap_cpp(moc_srcs ${moc_headers})
  else()
    qt5_wrap_cpp(moc_srcs ${moc_headers})
  endif(Qt6Widgets_FOUND)

  # When Obol is available, also MOC-process the QgObolView header so that
  # its Q_OBJECT signals and slots are wired up.
  if(BRLCAD_ENABLE_OBOL AND TARGET Obol::Obol)
    if(Qt6Widgets_FOUND)
      qt6_wrap_cpp(obol_moc_srcs QgObolView.h OPTIONS --no-notes -DBRLCAD_ENABLE_OBOL)
    else()
      qt5_wrap_cpp(obol_moc_srcs QgObolView.h OPTIONS --no-notes -DBRLCAD_ENABLE_OBOL)
    endif()
    list(APPEND moc_srcs ${obol_moc_srcs})
  endif()

  # https://github.com/Alexhuszagh/BreezeStyleSheets
  if(Qt6Widgets_FOUND)
    qt6_add_resources(theme_qrc theme/breeze.qrc)
  else()
    qt5_add_resources(theme_qrc theme/breeze.qrc)
  endif(Qt6Widgets_FOUND)

  add_executable(qged WIN32 main.cpp ${qged_srcs} ${moc_srcs} ${theme_qrc})
  # Note - Network is needed for async embedded raytracing display
  if(Qt6Widgets_FOUND)
    target_link_libraries(
      qged
      libqtcad
      libged
      librt
      libbu
      Qt6::Widgets
      Qt6::Network
    )
  else()
    target_link_libraries(
      qged
      libqtcad
      libged
      librt
      libbu
      Qt5::Widgets
      Qt5::Network
    )
  endif(Qt6Widgets_FOUND)
  if(BRLCAD_ENABLE_OBOL AND TARGET Obol::Obol)
    target_link_libraries(qged Obol::Obol)
    # QgObolView.h uses QOpenGLWidget from Qt6::OpenGLWidgets
    if(Qt6Widgets_FOUND)
      find_package(Qt6 COMPONENTS OpenGLWidgets QUIET)
      if(TARGET Qt6::OpenGLWidgets)
	target_link_libraries(qged Qt6::OpenGLWidgets)
      endif()
    endif()
    target_include_directories(qged PRIVATE
      ${Obol_INCLUDE_DIR}
      # obol_scene.h lives in src/libged — needed by QgObolView.h
      ${CMAKE_SOURCE_DIR}/src/libged)
    target_compile_definitions(qged PRIVATE BRLCAD_ENABLE_OBOL)
  endif()
  target_compile_definitions(qged PRIVATE BRLCADBUILD HAVE_CONFIG_H)
  install(TARGETS qged RUNTIME DESTINATION ${BIN_DIR} LIBRARY DESTINATION ${LIB_DIR} ARCHIVE DESTINATION ${LIB_DIR})

  # Make sure all dm and ged plugins are accounted for when we build qged
  add_dependencies(qged dm_plugins ged_plugins)

  # Automated qged draw validation test (swrast, moss.g)
  if(Qt6Widgets_FOUND)
    qt6_wrap_cpp(qged_test_moc_srcs qged_test_runner.h OPTIONS --no-notes)
  else()
    qt5_wrap_cpp(qged_test_moc_srcs qged_test_runner.h OPTIONS --no-notes)
  endif()
  add_executable(qged_test qged_test.cpp ${qged_test_moc_srcs} ${qged_srcs} ${moc_srcs} ${theme_qrc})
  target_compile_definitions(qged_test PRIVATE BRLCADBUILD HAVE_CONFIG_H)
  if(Qt6Widgets_FOUND)
    target_link_libraries(qged_test libqtcad libged librt libbu Qt6::Widgets Qt6::Network)
  else()
    target_link_libraries(qged_test libqtcad libged librt libbu Qt5::Widgets Qt5::Network)
  endif()
  add_dependencies(qged_test dm_plugins ged_plugins draw_test_moss_g)
  set_target_properties(qged_test PROPERTIES EXCLUDE_FROM_ALL TRUE)

  # AABB/OBB/LoD pipeline validation test (swrast, GenericTwin.g)
  if(Qt6Widgets_FOUND)
    qt6_wrap_cpp(qged_pipeline_moc_srcs qged_pipeline_runner.h OPTIONS --no-notes)
  else()
    qt5_wrap_cpp(qged_pipeline_moc_srcs qged_pipeline_runner.h OPTIONS --no-notes)
  endif()
  add_executable(qged_pipeline_test
    qged_pipeline_test.cpp
    ${qged_pipeline_moc_srcs}
    ${qged_srcs} ${moc_srcs} ${theme_qrc}
  )
  target_compile_definitions(qged_pipeline_test PRIVATE BRLCADBUILD HAVE_CONFIG_H)
  if(Qt6Widgets_FOUND)
    target_link_libraries(qged_pipeline_test
      libqtcad libged librt libbu Qt6::Widgets Qt6::Network)
  else()
    target_link_libraries(qged_pipeline_test
      libqtcad libged librt libbu Qt5::Widgets Qt5::Network)
  endif()
  add_dependencies(qged_pipeline_test dm_plugins ged_plugins Generic_Twin.g)
  set_target_properties(qged_pipeline_test PROPERTIES EXCLUDE_FROM_ALL TRUE)

  foreach(qh ${qth_names})
    distclean(moc_${qh}.cpp_parameters)
  endforeach(qh ${qth_names})
  distclean(${CMAKE_CURRENT_BINARY_DIR}/theme/breeze.qrc.depends)
endif(BRLCAD_ENABLE_QT)

# Core application is defined, now handle plugins
add_subdirectory(plugins)

# Theme subdirectory handles file bookkeeping for CMake
add_subdirectory(theme)

set(
  qged_ignore_srcs
  ${qged_srcs}
  ${moc_headers}
  BUILDING_QT
  CMakeLists.txt
  TODO
  fonts/Inconsolata-LICENSE.txt
  fonts/Inconsolata.otf
  main.cpp
  qged_signals.eps
  qged_signals.dia
  view_update_cycle.odg
  view_update_cycle.pdf
  qged_test.cpp
  qged_test_runner.h
  qged_pipeline_test.cpp
  qged_pipeline_runner.h
  QgObolView.h
)

cmakefiles(${qged_ignore_srcs})

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
