# Make sure the converter executable exists
string(REPLACE "\\" "" CONV_EXEC "${EXEC}")
if(NOT EXISTS "${CONV_EXEC}")
  message(FATAL_ERROR "Invalid geometry converter executable specified: ${CONV_EXEC}")
endif(NOT EXISTS "${CONV_EXEC}")

# Use local cache directories for the conversion
set(CBDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/db")
set(CSDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/db")
set(BU_DIR_CACHE ${CBDIR}/cache_sphflake)
set(LIBRT_CACHE ${CBDIR}/rtcache_sphflake)
set(ENV{BU_DIR_CACHE} ${BU_DIR_CACHE})
set(ENV{LIBRT_CACHE} ${LIBRT_CACHE})
file(REMOVE_RECURSE "${BU_DIR_CACHE}")
file(REMOVE_RECURSE "${LIBRT_CACHE}")
file(MAKE_DIRECTORY "${BU_DIR_CACHE}")
file(MAKE_DIRECTORY "${LIBRT_CACHE}")

# Deduce the current BRLCAD_ROOT value from the conversion executable path.
# Setting BRLCAD_ROOT is the hammer that makes certain we are running things
# found in the build directory.
get_filename_component(CEDIR "${CONV_EXEC}" DIRECTORY)
set(BIN_DIR "bin")
string(REGEX REPLACE "${BIN_DIR}$" "" CROOT "${CEDIR}")
set(ENV{BRLCAD_ROOT} "${CROOT}")

# Now that we know the root, finalize the full path output file location and
# run the conversion.
execute_process(
  COMMAND "${CONV_EXEC}" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/db/sphflake.asc" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/sphflake.g"
  RESULT_VARIABLE conv_result
  OUTPUT_VARIABLE conv_log
  ERROR_VARIABLE conv_log
)

if(conv_result)
  set(CARGS "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/db/sphflake.asc" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/share/db/sphflake.g")
  message(FATAL_ERROR "[sphflake] Failure: ${conv_result}\n${CONV_EXEC} ${CARGS}\"\n\n${conv_log}")
endif(conv_result)

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
