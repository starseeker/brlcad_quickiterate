# rt_ipc CMakeLists.txt
#
# Out-of-process raytrace worker using librtrender + bu_ipc.
#
# The child reads a JOB packet via bu_ipc (stdin/stdout for pipe transport,
# or a socket/TCP fd from BU_IPC_ADDR_ENVVAR for other transports), runs
# the raytrace with librtrender, streams PIXELS packets back, and exits.
# Can be killed cleanly at any time by the parent.
#
# rt_ipc does NOT require libuv: it uses blocking bu_ipc_read/write only.
# librtrender is listed as a dep because it also uses bu_ipc internally
# for the IPC client side.

brlcad_addexec(rt_ipc rt_ipc.cpp "librtrender;libbu" FOLDER Rt)

add_subdirectory(tests)

cmakefiles(CMakeLists.txt)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
