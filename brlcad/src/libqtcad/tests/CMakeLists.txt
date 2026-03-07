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
  add_dependencies(qgmodel ged_plugins)
  # The exploration section of qgmodel modifies the database in-place, so we
  # must pass a writable copy rather than the read-only source file.
  # A setup fixture copies ged_tests.g to the binary dir before each run;
  # distclean removes the copy afterwards.
  set(_qgmodel_work_g "${CMAKE_CURRENT_BINARY_DIR}/qtcad_qgmodel_work.g")
  add_test(NAME qtcad_qgmodel_setup
    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_CURRENT_SOURCE_DIR}/../../../libged/tests/ged_tests.g"
      "${_qgmodel_work_g}")
  set_tests_properties(qtcad_qgmodel_setup PROPERTIES
    FIXTURES_SETUP qtcad_qgmodel_fixture)
  brlcad_add_test(NAME qtcad_qgmodel COMMAND qgmodel "${_qgmodel_work_g}")
  set_tests_properties(qtcad_qgmodel PROPERTIES
    FIXTURES_REQUIRED qtcad_qgmodel_fixture
    ENVIRONMENT BRLCAD_ROOT=${CMAKE_BINARY_DIR})
  distclean("${_qgmodel_work_g}")
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

cmakefiles(
  CMakeLists.txt
  ${QGMODEL_SRCS}
  ${QGVIEW_SRCS}
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
