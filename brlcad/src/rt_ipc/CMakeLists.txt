# rt_ipc CMakeLists.txt
#
# Out-of-process raytrace worker that uses librtrender + libuv IPC.
#
# This executable:
#   - Reads render job requests from its stdin (a libuv pipe set up by the
#     parent via render_ipc_client_spawn in librtrender).
#   - Runs the raytrace using librtrender.
#   - Streams pixel scanline packets back to the parent on stdout.
#   - Can be killed cleanly at any time; the parent's render_ipc_client
#     handles re-spawning as needed.
#
# Requirements: librtrender (with RENDER_HAVE_LIBUV) and libuv.

if(NOT RENDER_LIBUV_LIB)
  # If librtrender couldn't find libuv, we can't build rt_ipc either.
  message(STATUS "rt_ipc: skipping (libuv not found)")
  return()
endif()

# Link against full library path so CMake won't confuse "rt" (POSIX lib)
# with the BRL-CAD "rt" executable target.
brlcad_addexec(rt_ipc rt_ipc.c "librtrender;libbu;${RENDER_LIBUV_LIB}" FOLDER Rt)

# Pass compile definitions and includes using the plain signature used by
# brlcad_addexec internally.
set_property(TARGET rt_ipc APPEND PROPERTY COMPILE_DEFINITIONS RENDER_HAVE_LIBUV)
target_include_directories(rt_ipc BEFORE PRIVATE ${RENDER_LIBUV_INC})

cmakefiles(CMakeLists.txt)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
