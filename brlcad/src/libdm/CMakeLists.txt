# Main libdm library
set(
  LIBDM_SRCS
  asize.c
  dm_init.cpp
  vers.c
)
# In Obol builds the entire struct-fb stack (fb_generic.c, fb_plugins.cpp,
# if_remote.c, if_disk.c, etc.) and the dm-rendering helpers (dm_plugins.cpp,
# dm-generic.c, dm-Null.c, view.c, dm_util.c) are not needed:
#  - rt and related tools use a PKG-direct inline abstraction (rt_fb_pkg.h)
#    instead of fb_open / fb_write / fb_close via libdm/if_remote.c.
#  - All GL rendering plugins are excluded separately (Stage 22+).
# This keeps libdm minimal in Obol builds.
if(NOT BRLCAD_ENABLE_OBOL)
  list(APPEND LIBDM_SRCS
    null/if_null.c
    null/dm-Null.c
    txt/if_debug.c
    dm-generic.c
    dm_plugins.cpp
    dm_util.c
    fb_generic.c
    fb_log.c
    fb_paged_io.c
    fb_plugins.cpp
    fb_rect.c
    fb_util.c
    fbserv.c
    if_disk.c
    if_mem.c
    if_remote.c
    if_stack.c
    view.c
  )
else()
  list(APPEND LIBDM_SRCS dm_obol_stubs.cpp)
endif()
# In Obol builds the GL rendering plugins are replaced by the Obol/Coin3D
# scene-graph path.  The 2D overlay drawing helpers (ADC cursor, axes, grid,
# rect, scale, labels, clipping) and the options parser are only used by
# those plugins and by the non-Obol dm_draw_faceplate/dm_draw_viewobjs paths
# in view.c.  Excluding them from Obol builds keeps libdm trim.
if(NOT BRLCAD_ENABLE_OBOL)
  list(APPEND LIBDM_SRCS
    adc.c
    axes.c
    clip.c
    grid.c
    labels.c
    options.c
    rect.c
    scale.c
  )
endif(NOT BRLCAD_ENABLE_OBOL)
set_property(SOURCE dm_obj.c APPEND PROPERTY COMPILE_DEFINITIONS FB_USE_INTERNAL_API)
set_property(
  SOURCE dm_init.cpp
  APPEND
  PROPERTY COMPILE_DEFINITIONS "DM_PLUGIN_SUFFIX=\"${CMAKE_SHARED_LIBRARY_SUFFIX}\""
)

if(BRLCAD_ENABLE_OPENGL)
  set(DM_INCLUDE_DIRS ${DM_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIR})
endif(BRLCAD_ENABLE_OPENGL)

# local includes
set(DM_LOCAL_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${PNG_INCLUDE_DIRS} ${BRLCAD_SOURCE_DIR}/src/libbg)

# Note - libdm_deps is defined by ${BRLCAD_SOURCE_DIR}/src/source_dirs.cmake
brlcad_addlib(libdm "${LIBDM_SRCS}" "${libdm_deps};${PNG_LIBRARIES}" "${DM_INCLUDE_DIRS}" "${DM_LOCAL_INCLUDE_DIRS}"
  UNITY_BUILD_SKIP dm_init.cpp
  )
set_target_properties(libdm PROPERTIES VERSION 20.0.1 SOVERSION 20)
# Propagate BRLCAD_ENABLE_OBOL as a C preprocessor define so that #ifndef
# BRLCAD_ENABLE_OBOL guards in dm_init.cpp and dm_obol_stubs.cpp are active.
if(BRLCAD_ENABLE_OBOL)
  if(TARGET libdm-obj)
    target_compile_definitions(libdm-obj PRIVATE BRLCAD_ENABLE_OBOL)
  endif()
  target_compile_definitions(libdm PRIVATE BRLCAD_ENABLE_OBOL)
endif(BRLCAD_ENABLE_OBOL)
if(TARGET profont_ProFont_ttf_cp)
  add_dependencies(libdm profont_ProFont_ttf_cp)
endif(TARGET profont_ProFont_ttf_cp)

# Many of the backends use the same OpenGL drawing logic for much of their
# functionality.  If OpenGL support is available, We encapsulate the common
# elements of this logic in a libdmgl library used by those plugins.
# When Obol is the rendering backend all GL plugins are skipped, so libdmgl
# is never referenced and should not be built.
set(LIBDMGL_SRCS dm-gl.c dm-gl_lod.cpp)
if(NOT BRLCAD_ENABLE_OBOL AND BRLCAD_ENABLE_OPENGL)
  find_package(OpenGL)

  if(OPENGL_opengl_LIBRARY)
    set(OPENGL_LIBS ${OPENGL_opengl_LIBRARY})
  endif(OPENGL_opengl_LIBRARY)
  if(OPENGL_gl_LIBRARY)
    set(OPENGL_LIBS ${OPENGL_gl_LIBRARY})
  endif(OPENGL_gl_LIBRARY)

  brlcad_addlib(libdmgl "${LIBDMGL_SRCS}" "libdm;libbn;libbu;${OPENGL_LIBS}" "${DM_INCLUDE_DIRS}" "")
  set_target_properties(libdmgl PROPERTIES VERSION 20.0.1 SOVERSION 20)
endif(NOT BRLCAD_ENABLE_OBOL AND BRLCAD_ENABLE_OPENGL)

add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/dm_plugins.sentinel"
  COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/dm_plugins.sentinel"
)
add_custom_target(dm_plugins_sentinel DEPENDS ${CMAKE_BINARY_DIR}/dm_plugins.sentinel)
set_target_properties(dm_plugins_sentinel PROPERTIES FOLDER "BRL-CAD Plugins/dm")

function(dm_plugin_library name)
  add_library(${name} ${ARGN})
  add_dependencies(dm_plugins_sentinel ${name})
endfunction(dm_plugin_library name)

# This target is supplied so applications wanting to run libdm
# commands at build time can depend on the plugins being built as well as
# libdm itself (otherwise LIBGED commands might fail due to their plugins not
# yet having been built.)
add_custom_target(dm_plugins ALL DEPENDS dm_plugins_sentinel)
set_target_properties(dm_plugins PROPERTIES FOLDER "BRL-CAD Plugins")

# Backend implementations
# Detect Obol early so we can guard the GL rendering plugins.
# When Obol is the rendering backend, all GL-backed libdm plugins (qtgl,
# swrast, glx, X, wgl) are replaced by the Obol/Coin3D scene-graph path and
# must be excluded to keep Obol binaries free of libdm rendering symbols.
if(BRLCAD_ENABLE_QT AND NOT DEFINED BRLCAD_ENABLE_OBOL)
  brlcad_find_package(Obol)
  if(TARGET Obol::Obol)
    set(BRLCAD_ENABLE_OBOL ON CACHE BOOL "Obol scene-graph library available" FORCE)
  else()
    set(BRLCAD_ENABLE_OBOL OFF CACHE BOOL "Obol scene-graph library available" FORCE)
  endif()
endif()
add_subdirectory(X)
add_subdirectory(glx)
add_subdirectory(plot)
add_subdirectory(postscript)
add_subdirectory(qtgl)
add_subdirectory(swrast)
add_subdirectory(txt)
add_subdirectory(wgl)

add_subdirectory(tests)

set(
  libdm_ignore_files
  CMakeLists.txt
  README
  TODO
  # dm-gl.c is in LIBDMGL_SRCS; it is always registered here so that it
  # appears in source distributions even when OpenGL is disabled.
  dm-gl.c
  dm-gl.h
  if_TEMPLATE.c
  null/dm-Null.h
  include/private.h
  include/calltable.h
  fontstash/LICENSE.fontstash
  fontstash/README.md
  fontstash/fontstash.h
  fontstash/glfontstash.h
  fontstash/stb_truetype.h
)
# Register Obol-gated source files so they appear in distribution tarballs
# even when BRLCAD_ENABLE_OBOL is set and they are not compiled.
# dm-gl_lod.cpp is in LIBDMGL_SRCS along with dm-gl.c; in non-Obol+OpenGL
# builds both are registered by brlcad_addlib, but in Obol builds libdmgl is
# skipped so dm-gl_lod.cpp needs explicit registration here.
# Similarly, dm_plugins.cpp, dm-generic.c, view.c, and null/dm-Null.c are only
# compiled in non-Obol builds, so register them here for Obol builds.
# dm_obol_stubs.cpp is only compiled in Obol builds, so register it for
# non-Obol builds.
if(BRLCAD_ENABLE_OBOL)
  list(APPEND libdm_ignore_files
    adc.c
    axes.c
    clip.c
    grid.c
    labels.c
    options.c
    rect.c
    scale.c
    dm-gl_lod.cpp
    dm_plugins.cpp
    dm-generic.c
    view.c
    null/dm-Null.c
  )
else()
  list(APPEND libdm_ignore_files
    dm_obol_stubs.cpp
  )
endif(BRLCAD_ENABLE_OBOL)
cmakefiles(${libdm_ignore_files})

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
