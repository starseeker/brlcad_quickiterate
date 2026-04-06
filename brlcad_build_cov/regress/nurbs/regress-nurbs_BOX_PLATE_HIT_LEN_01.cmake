# Values set at CMake configure time
set(CBDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/nurbs")
set(CSDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/nurbs")
set(BIN_DIR "bin")
set(LOGFILE "${CBDIR}/regress-nurbs_BOX_PLATE_HIT_LEN_01.log")

set(BU_DIR_CACHE ${CBDIR}/cache)
set(LIBRT_CACHE ${CBDIR}/rtcache)
set(ENV{BU_DIR_CACHE} ${BU_DIR_CACHE})
set(ENV{LIBRT_CACHE} ${LIBRT_CACHE})
file(REMOVE_RECURSE "${BU_DIR_CACHE}")
file(REMOVE_RECURSE "${LIBRT_CACHE}")
file(MAKE_DIRECTORY "${BU_DIR_CACHE}")
file(MAKE_DIRECTORY "${LIBRT_CACHE}")

file(WRITE "${LOGFILE}" "Starting nurbs test run\n")

# The executable locations aren't know at CMake configure time, so one of them
# is passed in via the EXEC variable at runtime.  De-quote it and assign it to
# the appropriate variable.
string(REPLACE "\\" "" NHIT "${EXEC}")
if(NOT EXISTS "${NHIT}")
  file(WRITE "${LOGFILE}" "analyze_nhit not found at location \"${NHIT}\" - aborting\n")
  file(READ "${LOGFILE}" LOG)
  message(FATAL_ERROR "Unable to find analyze_nhit, aborting.\nSee ${LOGFILE} for more details.\n${LOG}")
endif(NOT EXISTS "${NHIT}")

# Canonicalize path - if we have an absolute path use it, but if the .g file is
# a build output we need to find the local configuration's version of the .g
# file.  The caller needs to define a relative path to the root of the build.
get_filename_component(BDIR "${NHIT}" DIRECTORY)
get_filename_component(GFILE_FP "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/libbrep/tests/plate_mode_samples.g" ABSOLUTE)
if("${GFILE_FP}" STREQUAL "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/libbrep/tests/plate_mode_samples.g")
  set(NHIT_G "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/libbrep/tests/plate_mode_samples.g")
else("${GFILE_FP}" STREQUAL "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/libbrep/tests/plate_mode_samples.g")
  string(REGEX REPLACE "${BIN_DIR}$" "" RDIR "${BDIR}")
  set(NHIT_G "${RDIR}//home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/libbrep/tests/plate_mode_samples.g")
endif("${GFILE_FP}" STREQUAL "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/libbrep/tests/plate_mode_samples.g")

execute_process(
  COMMAND "${NHIT}" -L .1 -b "${NHIT_G}" box.s 0 0 0 270 90
  RESULT_VARIABLE nurbs_result
  OUTPUT_VARIABLE nurbs_log
  ERROR_VARIABLE nurbs_log
  WORKING_DIRECTORY ${CBDIR}
)

if(nurbs_result)
  file(READ "${LOGFILE}" LOG)
  message(
    FATAL_ERROR
    "[nurbs] Failure, unexpected result running ${NHIT} -L .1 -b /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/libbrep/tests/plate_mode_samples.g box.s 0 0 0 270 90\n ${nurbs_log}\n${LOG}"
  )
endif(nurbs_result)

# Cleanup
execute_process(
  COMMAND "/usr/local/bin/cmake" -E rm -rf ${BU_DIR_CACHE}
)
execute_process(
  COMMAND "/usr/local/bin/cmake" -E rm -rf ${LIBRT_CACHE}
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
