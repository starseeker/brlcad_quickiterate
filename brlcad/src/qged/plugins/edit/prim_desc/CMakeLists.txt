# Generic descriptor-driven primitive edit panel plugin (qged_prim_desc)
#
# Builds the QgPrimDescWidget class and registers it as a qged "OC tool"
# plugin.  The plugin is only compiled when BRLCAD_ENABLE_QT is ON.

if(BRLCAD_ENABLE_QT)
  include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${BRLCAD_BINARY_DIR}/${INCLUDE_DIR}
    ${BRLCAD_SOURCE_DIR}/include
    ${QTCAD_INCLUDE_DIRS}
    ${GED_INCLUDE_DIRS}
    ${RT_INCLUDE_DIRS}
    ${BU_INCLUDE_DIRS}
  )
endif(BRLCAD_ENABLE_QT)

set(prim_desc_srcs
  QgPrimDescWidget.cpp
  prim_desc_tool.cpp
)

set(prim_desc_moc_hdrs
  QgPrimDescWidget.h
)

if(BRLCAD_ENABLE_QT)
  if(Qt6Widgets_FOUND)
    qt6_wrap_cpp(prim_desc_moc_srcs ${prim_desc_moc_hdrs})
    qt6_add_resources(icon_qrc icon.qrc)
  else()
    qt5_wrap_cpp(prim_desc_moc_srcs ${prim_desc_moc_hdrs})
    qt5_add_resources(icon_qrc icon.qrc)
  endif(Qt6Widgets_FOUND)

  qged_plugin(qged_prim_desc SHARED
    ${prim_desc_srcs}
    ${prim_desc_moc_srcs}
    ${icon_qrc}
  )

  if(Qt6Widgets_FOUND)
    target_link_libraries(qged_prim_desc
      libqtcad
      libged
      librt
      libbu
      Qt6::Widgets
    )
  else()
    target_link_libraries(qged_prim_desc
      libqtcad
      libged
      librt
      libbu
      Qt5::Widgets
    )
  endif(Qt6Widgets_FOUND)

  set_property(TARGET qged_prim_desc APPEND PROPERTY
    COMPILE_DEFINITIONS BRLCADBUILD HAVE_CONFIG_H)
  validate_style(qged_prim_desc prim_desc_tool.cpp)
  plugin_setup(qged_prim_desc qged)

  foreach(qh ${prim_desc_moc_hdrs})
    get_filename_component(qh_base ${qh} NAME_WE)
    distclean(moc_${qh_base}.cpp_parameters)
  endforeach()
  distclean(icon.qrc.depends)
endif(BRLCAD_ENABLE_QT)

set(ignore_srcs
  CMakeLists.txt
  ${prim_desc_srcs}
  ${prim_desc_moc_hdrs}
  icon.qrc
  prim_desc.svg
)

cmakefiles(${ignore_srcs})

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
