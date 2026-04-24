include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

set(QGMODEL_SRCS qgmodel.cpp)
set(QGVIEW_SRCS qgview.cpp)

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

# Qt / QgSW headless integration test (Phase 4 BSG + dm-swrast via Qt widget path).
# Uses Qt's built-in offscreen platform — no X11 display required.
if(TARGET Qt6::Widgets)
  set(GED_TEST_QGED_SWRAST_DATA_DIR "${CMAKE_SOURCE_DIR}/src/libged/tests/draw")
  add_executable(ged_test_qged_swrast ged_test_qged_swrast.cpp)
  target_include_directories(ged_test_qged_swrast PRIVATE
    "${CMAKE_SOURCE_DIR}/src/libged"
  )
  target_link_libraries(
    ged_test_qged_swrast
    libqtcad
    libged
    libdm
    libbsg
    librt
    libbu
    Qt6::Widgets
  )
  target_compile_definitions(ged_test_qged_swrast PRIVATE BRLCADBUILD HAVE_CONFIG_H)
  if(TARGET dm-swrast)
    add_dependencies(ged_test_qged_swrast dm-swrast)
  endif()
  brlcad_add_test(NAME ged_test_qged_swrast
    COMMAND ged_test_qged_swrast -c "${GED_TEST_QGED_SWRAST_DATA_DIR}")
  set_property(TEST ged_test_qged_swrast
    APPEND PROPERTY ENVIRONMENT BRLCAD_ROOT=${CMAKE_BINARY_DIR})
  set_property(TEST ged_test_qged_swrast
    APPEND PROPERTY ENVIRONMENT QT_QPA_PLATFORM=offscreen)
  distclean(${CMAKE_CURRENT_BINARY_DIR}/moss_qgswrast_tmp.g)
  distclean(${CMAKE_CURRENT_BINARY_DIR}/qgswrast_cache)
endif(TARGET Qt6::Widgets)

cmakefiles(
  CMakeLists.txt
  ged_test_qged_swrast.cpp
  ${QGMODEL_SRCS}
  ${QGVIEW_SRCS}
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
