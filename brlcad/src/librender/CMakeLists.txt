# librender CMakeLists.txt
#
# A clean C library encapsulating BRL-CAD raytracing capabilities.
# The library target is named "librtrender" to avoid conflict with the
# existing "librender" in src/adrt/.
#
# Dependencies: librt, liboptical, libbu (from source_dirs.cmake)
#
# When libuv is available, the IPC client (render_ipc_client.c) is
# also compiled in so that callers can spawn an rt_ipc child process
# and receive pixel data over a local pipe instead of TCP.

set(
  LIBRENDER_SOURCES
  render_ctx.c
  render_opts.c
  render_run.c
)

# Note - librender_deps is defined by ${BRLCAD_SOURCE_DIR}/src/source_dirs.cmake
brlcad_addlib(librtrender "${LIBRENDER_SOURCES}" "${librender_deps}" "" "")
set_target_properties(librtrender PROPERTIES VERSION 1.0.0 SOVERSION 1)

# ── Optional libuv IPC client ─────────────────────────────────────────
# Use find_library to get a full-path to libuv.so to avoid CMake
# confusing "rt" (from pkg-config's -lrt output) with the BRL-CAD rt exe.
find_path(LIBUV_INCLUDE_DIR
  NAMES uv.h
  HINTS /usr/include /usr/local/include
)
find_library(LIBUV_LIBRARY
  NAMES uv libuv
  HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
)

if(LIBUV_INCLUDE_DIR AND LIBUV_LIBRARY)
  set(LIBUV_FOUND TRUE)
  message(STATUS "librender: libuv found (${LIBUV_LIBRARY}) — IPC client enabled")

  target_sources(librtrender PRIVATE render_ipc_client.c)
  target_compile_definitions(librtrender PUBLIC RENDER_HAVE_LIBUV)
  target_include_directories(librtrender PRIVATE ${LIBUV_INCLUDE_DIR})
  # Use the full library path so CMake cannot confuse it with a target name.
  target_link_libraries(librtrender PUBLIC ${LIBUV_LIBRARY})

  set(RENDER_LIBUV_LIB  ${LIBUV_LIBRARY}     CACHE INTERNAL "libuv full-path library")
  set(RENDER_LIBUV_INC  ${LIBUV_INCLUDE_DIR}  CACHE INTERNAL "libuv include dir")
else()
  set(LIBUV_FOUND FALSE)
  message(STATUS "librender: libuv not found — IPC client disabled")
endif()

# ── Installation ─────────────────────────────────────────────────────
install(
  FILES
    ${BRLCAD_SOURCE_DIR}/include/render.h
    ${BRLCAD_SOURCE_DIR}/include/render_ipc.h
  DESTINATION ${INCLUDE_DIR}/brlcad
)

cmakefiles(
  CMakeLists.txt
  render_private.h
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
