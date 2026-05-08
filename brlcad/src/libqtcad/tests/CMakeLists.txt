include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

set(QGMODEL_SRCS qgmodel.cpp)
set(QGVIEW_SRCS qgview.cpp)
set(QSKETCH_SRCS qsketch.cpp)

if(TARGET Qt6::Test)
  add_executable(qgmodel qgmodel.cpp)
  target_link_libraries(
    qgmodel
    libqtcad
    libged
    librt
    libbu
    Qt6::Widgets
    Qt6::Test
  )
endif(TARGET Qt6::Test)
if(TARGET Qt5::Test)
  add_executable(qgmodel ${QGMODEL_SRCS})
  target_link_libraries(
    qgmodel
    libqtcad
    libged
    librt
    libbu
    Qt5::Widgets
    Qt5::Test
  )
endif(TARGET Qt5::Test)
if(TARGET qgmodel)
  target_compile_definitions(qgmodel PRIVATE BRLCADBUILD HAVE_CONFIG_H)
  install(TARGETS qgmodel RUNTIME DESTINATION ${BIN_DIR} LIBRARY DESTINATION ${LIB_DIR} ARCHIVE DESTINATION ${LIB_DIR})
endif(TARGET qgmodel)

if(TARGET Qt6::Widgets)
  add_executable(qgview ${QGVIEW_SRCS})
  target_link_libraries(
    qgview
    libqtcad
    libged
    librt
    libbu
    Qt6::Widgets
  )
endif(TARGET Qt6::Widgets)
if(TARGET Qt5::Widgets)
  add_executable(qgview ${QGVIEW_SRCS})
  target_link_libraries(
    qgview
    libqtcad
    libged
    librt
    libbu
    Qt5::Widgets
  )
endif(TARGET Qt5::Widgets)
if(TARGET qgview)
  target_compile_definitions(qgview PRIVATE BRLCADBUILD HAVE_CONFIG_H)
  install(TARGETS qgview RUNTIME DESTINATION ${BIN_DIR} LIBRARY DESTINATION ${LIB_DIR} ARCHIVE DESTINATION ${LIB_DIR})
endif(TARGET qgview)

# qsketch - standalone .g sketch editing demo
# qsketch.cpp defines QSketchEditWindow inline and uses #include "qsketch.moc"
# at the bottom, so we need AUTOMOC ON rather than qt6_wrap_cpp.
if(TARGET Qt6::Widgets)
  add_executable(qsketch ${QSKETCH_SRCS})
  set_target_properties(qsketch PROPERTIES AUTOMOC ON)
  target_link_libraries(
    qsketch
    libqtcad
    librt
    libbu
    Qt6::Widgets
  )
endif(TARGET Qt6::Widgets)
if(TARGET Qt5::Widgets AND NOT TARGET Qt6::Widgets)
  add_executable(qsketch ${QSKETCH_SRCS})
  set_target_properties(qsketch PROPERTIES AUTOMOC ON)
  target_link_libraries(
    qsketch
    libqtcad
    librt
    libbu
    Qt5::Widgets
  )
endif(TARGET Qt5::Widgets AND NOT TARGET Qt6::Widgets)
if(TARGET qsketch)
  target_compile_definitions(qsketch PRIVATE BRLCADBUILD HAVE_CONFIG_H)
  install(TARGETS qsketch RUNTIME DESTINATION ${BIN_DIR} LIBRARY DESTINATION ${LIB_DIR} ARCHIVE DESTINATION ${LIB_DIR})
endif(TARGET qsketch)

# Phase B (drawing_stack_modernization): dm-swrast vs dm-qtgl backend benchmark.
# Reports per-frame timing and fps for each backend.  The dm-qtgl section
# requires hardware OpenGL and is skipped gracefully in headless environments.
# Exit status is always 0 — this is a benchmark, not a pass/fail test.
if(TARGET Qt6::Widgets)
  set(GED_TEST_DM_BACKEND_BENCH_DATA_DIR "${CMAKE_SOURCE_DIR}/src/libged/tests/draw")
  add_executable(ged_test_dm_backend_bench ged_test_dm_backend_bench.cpp)
  target_include_directories(ged_test_dm_backend_bench PRIVATE
    "${CMAKE_SOURCE_DIR}/src/libged"
  )
  set(_bench_libs libqtcad libged libdm librt libbu Qt6::Widgets)
  # Note: dm-swrast and dm-qtgl are loaded at runtime; they do not need to
  # be listed here as link targets.  Linking them would pull in pre-existing
  # build issues in dm-gl_lod.cpp on systems with GL3-only headers.
  target_link_libraries(ged_test_dm_backend_bench ${_bench_libs})
  target_compile_definitions(ged_test_dm_backend_bench PRIVATE BRLCADBUILD HAVE_CONFIG_H)
  brlcad_add_test(NAME ged_test_dm_backend_bench
    COMMAND ged_test_dm_backend_bench "${GED_TEST_DM_BACKEND_BENCH_DATA_DIR}")
  set_property(TEST ged_test_dm_backend_bench
    APPEND PROPERTY ENVIRONMENT BRLCAD_ROOT=${CMAKE_BINARY_DIR})
  set_property(TEST ged_test_dm_backend_bench
    APPEND PROPERTY ENVIRONMENT QT_QPA_PLATFORM=offscreen)
  distclean(${CMAKE_CURRENT_BINARY_DIR}/dmbench_swrast_tmp.g)
  distclean(${CMAKE_CURRENT_BINARY_DIR}/dmbench_qtgl_tmp.g)
  distclean(${CMAKE_CURRENT_BINARY_DIR}/dmbench_cache)
endif(TARGET Qt6::Widgets)

cmakefiles(
  CMakeLists.txt
  ged_test_dm_backend_bench.cpp
  ${QGMODEL_SRCS}
  ${QGVIEW_SRCS}
  ${QSKETCH_SRCS}
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8

