if(BRLCAD_ENABLE_TCL)
  if(BRLCAD_ENABLE_TK)
    set(TCL_ENABLE_TK ON)
  endif(BRLCAD_ENABLE_TK)
  find_package_tcl(REQUIRED)
  if(EXISTS ${CMAKE_BINARY_DIR}/${LIB_DIR}/itcl3.4)
    set(ITCL_VERSION "3.4")
  endif(EXISTS ${CMAKE_BINARY_DIR}/${LIB_DIR}/itcl3.4)
  if(EXISTS ${CMAKE_BINARY_DIR}/${LIB_DIR}/itk3.4)
    set(ITK_VERSION "3.4")
  endif(EXISTS ${CMAKE_BINARY_DIR}/${LIB_DIR}/itk3.4)
  if(EXISTS ${CMAKE_BINARY_DIR}/${LIB_DIR}/Iwidgets4.1.1)
    set(IWIDGETS_VERSION "4.1.1")
  endif(EXISTS ${CMAKE_BINARY_DIR}/${LIB_DIR}/Iwidgets4.1.1)
endif()

set(
  LIBTCLCAD_SRCS
  auto_path.c
  bn.c
  bu.c
  command_io.cpp
  commands.c
  cmdhist.c
  dm.c
  eval.c
  fb.c
  fbserv.c
  global.c
  init.c
  mouse.c
  polygons.c
  rt.c
  tkImgFmtPIX.c
  view/arrows.c
  view/autoview.c
  view/axes.c
  view/draw.c
  view/faceplate.c
  view/labels.c
  view/lines.c
  view/refresh.c
  view/util.c
  wrapper.c
)

# Obol-backed Tk 3D view widget (platform-neutral; no native GL window needed)
#
# brlcad_find_package(Obol) is normally called by qged/CMakeLists.txt when Qt
# is enabled.  When building without Qt (e.g. mged-only builds) we do the
# detection here so that libtclcad can still provide obol_view.  Use a guard
# so we don't call it twice if qged already ran the detection.
if(NOT DEFINED BRLCAD_ENABLE_OBOL AND BRLCAD_ENABLE_TCL)
  brlcad_find_package(Obol)
  if(TARGET Obol::Obol)
    set(BRLCAD_ENABLE_OBOL ON CACHE BOOL "Obol scene-graph library available" FORCE)
    message(STATUS "Obol found (from libtclcad) — obol_view Tcl widget enabled")
    # Check for OSMesa dual-GL support the same way qged does
    get_target_property(_obol_iface Obol::Obol INTERFACE_COMPILE_DEFINITIONS)
    if(_obol_iface AND "${_obol_iface}" MATCHES "OBOL_BUILD_DUAL_GL")
      set(BRLCAD_OBOL_DUAL_GL ON CACHE BOOL "Obol built with dual-GL (OSMesa) support" FORCE)
    else()
      set(BRLCAD_OBOL_DUAL_GL OFF CACHE BOOL "Obol built with dual-GL (OSMesa) support" FORCE)
    endif()
  else()
    set(BRLCAD_ENABLE_OBOL OFF CACHE BOOL "Obol scene-graph library available" FORCE)
    set(BRLCAD_OBOL_DUAL_GL OFF CACHE BOOL "Obol built with dual-GL (OSMesa) support" FORCE)
  endif()
endif()

if(BRLCAD_ENABLE_OBOL AND TK_LIBRARY)
  list(APPEND LIBTCLCAD_SRCS obol_view.cpp)
endif(BRLCAD_ENABLE_OBOL AND TK_LIBRARY)

if(BRLCAD_ENABLE_TCL)
  # Include directories needed by libtclcad users
  set(TCLCAD_INCLUDE_DIRS ${TCL_INCLUDE_PATH})
  if(TK_INCLUDE_PATH)
    set(TCLCAD_INCLUDE_DIRS ${TCLCAD_INCLUDE_DIRS} ${TK_INCLUDE_PATH} ${X11_INCLUDE_DIR})
  endif(TK_INCLUDE_PATH)

  # Include directories only needed by the implementation
  set(TCLCAD_LOCAL_INCLUDE_DIRS ${PNG_INCLUDE_DIRS})
  if(BRLCAD_ENABLE_X11)
    set(TCLCAD_XLIBS ${TCLCAD_XLIBS} ${X11_LIBRARIES})
  endif(BRLCAD_ENABLE_X11)

  # Note - libtclcad_deps is defined by ${BRLCAD_SOURCE_DIR}/src/source_dirs.cmake
  set(TCLCAD_LIBS ${libtclcad_deps} ${TCL_LIBRARY} ${TCLCAD_XLIBS})
  if(TK_LIBRARY)
    set(TCLCAD_LIBS ${TCLCAD_LIBS} ${TK_LIBRARY})
  endif(TK_LIBRARY)
  if(BRLCAD_ENABLE_OBOL AND TARGET Obol::Obol AND TK_LIBRARY)
    set(TCLCAD_LIBS ${TCLCAD_LIBS} Obol::Obol)
    set(TCLCAD_LOCAL_INCLUDE_DIRS ${TCLCAD_LOCAL_INCLUDE_DIRS}
      ${CMAKE_SOURCE_DIR}/src/libged)
  endif(BRLCAD_ENABLE_OBOL AND TARGET Obol::Obol AND TK_LIBRARY)

  brlcad_addlib(libtclcad "${LIBTCLCAD_SRCS}" "${TCLCAD_LIBS}" "${TCLCAD_INCLUDE_DIRS}" "${TCLCAD_LOCAL_INCLUDE_DIRS}"
    # refresh.c defines DM_WITH_RT before including dm/view.h to expose
    # dm_draw_viewobjs; when batched after draw.c the DM_VIEW_H guard fires
    # without DM_WITH_RT set and the declaration is lost.
    # bu.c and dm.c both define a non-static register_cmds function.
    UNITY_BUILD_SKIP view/refresh.c bu.c dm.c
  )
  set(ptargets libtclcad)
  if(TARGET libtclcad-obj)
    list(APPEND ptargets libtclcad-obj)
  endif(TARGET libtclcad-obj)
  foreach(pt ${ptargets})
    target_compile_definitions(${pt} PRIVATE ITCL_VERSION=\"${ITCL_VERSION}\")
    target_compile_definitions(${pt} PRIVATE ITK_VERSION=\"${ITK_VERSION}\")
    target_compile_definitions(${pt} PRIVATE IWIDGETS_VERSION=\"${IWIDGETS_VERSION}\")
    if(TK_LIBRARY)
      target_compile_definitions(${pt} PRIVATE HAVE_TK)
    endif(TK_LIBRARY)
    if(BRLCAD_ENABLE_OBOL AND TARGET Obol::Obol AND TK_LIBRARY)
      target_compile_definitions(${pt} PRIVATE BRLCAD_ENABLE_OBOL)
      if(BRLCAD_OBOL_DUAL_GL)
        target_compile_definitions(${pt} PRIVATE OBOL_BUILD_DUAL_GL)
      endif()
    endif(BRLCAD_ENABLE_OBOL AND TARGET Obol::Obol AND TK_LIBRARY)
  endforeach(pt ${ptargets})

  set_target_properties(libtclcad PROPERTIES VERSION 20.0.1 SOVERSION 20)
  if(TARGET itcl_pkgIndex)
    add_dependencies(libtclcad itcl_pkgIndex)
  endif(TARGET itcl_pkgIndex)
  if(TARGET itk_pkgIndex)
    add_dependencies(libtclcad itk_pkgIndex)
  endif(TARGET itk_pkgIndex)

  # For anyone to do anything with tclcad, we need the libged plugins
  add_dependencies(libtclcad ged_plugins)
endif(BRLCAD_ENABLE_TCL)

add_subdirectory(tests)

cmakefiles(
  ${LIBTCLCAD_SRCS}
  obol_view.cpp
  tclcad_private.h
  view/view.h
  CMakeLists.txt
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
