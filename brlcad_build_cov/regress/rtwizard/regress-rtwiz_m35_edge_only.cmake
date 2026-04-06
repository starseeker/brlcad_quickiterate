# Values set at CMake configure time
set(CBDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/rtwizard")
set(INPUTFILE "share/db/m35.g")
set(CTRLIMG "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/rtwizard/m35_edge_only_ref.pix")
set(TGTIMG "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/rtwizard/m35_edge_only.pix")
set(BIN_DIR "bin")
set(LOGFILE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/rtwizard/m35_edge_only.log")

file(WRITE "${LOGFILE}" "Starting rtwizard run\n")

# The executable locations aren't know at CMake configure time, so one of them
# is passed in via the EXEC variable at runtime.  De-quote it and assign it to
# the appropriate variable.
string(REPLACE "\\" "" RTWIZ "${EXEC}")
if(NOT EXISTS "${RTWIZ}")
  file(WRITE "${LOGFILE}" "rtwizard not found at location \"${RTWIZ}\" - aborting\n")
  file(READ "${LOGFILE}" LOG)
  message(FATAL_ERROR "Unable to find rtwizard, aborting.\nSee ${LOGFILE} for more details.\n${LOG}")
endif(NOT EXISTS "${RTWIZ}")

# Apparently CMAKE_EXECUTABLE_SUFFIX is not reliably defined in cmake -P - get
# it from the executable passed in to us.
get_filename_component(EXE_SUFFIX "${RTWIZ}" EXT)

# Use the path passed in from the build target generator expression
# to ascertain the current binary directory in use, and locate any
# other necessary programs.
get_filename_component(BDIR "${RTWIZ}" DIRECTORY)
set(PIXCMP_EXEC "${BDIR}/pixcmp${EXE_SUFFIX}")
if(NOT EXISTS "${PIXCMP_EXEC}")
  file(WRITE "${LOGFILE}" "pixcmp not found at location \"${PIXCMP_EXEC}\" - aborting\n")
  file(READ "${LOGFILE}" LOG)
  message(FATAL_ERROR "Unable to find pixcmp, aborting.\nSee ${LOGFILE} for more details.\n${LOG}")
endif(NOT EXISTS "${PIXCMP_EXEC}")

# Reconstruct the tree root from the EXEC path, and identify the input file location
string(REGEX REPLACE "${BIN_DIR}$" "" RDIR "${BDIR}")
set(SRCFILE "${RDIR}/${INPUTFILE}")

# Clean up in case we've run before
execute_process(
  COMMAND "/usr/local/bin/cmake" -E remove -f "${TGTIMG}"
)

# Start with the file using standard UNIX line endings
file(APPEND "${LOGFILE}" "Generating ${TGTIMG}:\n")
execute_process(
  COMMAND "${RTWIZ}" --no-gui -i "${SRCFILE}" -o "${TGTIMG}" -l component --benchmark -z 2
  RESULT_VARIABLE rtwiz_result
  OUTPUT_VARIABLE rtwiz_log
  ERROR_VARIABLE rtwiz_log
)
file(APPEND "${LOGFILE}" "${rtwiz_log}")
set(rtwiz_log)
if(NOT EXISTS "${TGTIMG}")
  file(APPEND "${LOGFILE}" "Failure: ${rtwiz_result}")
  file(READ "${LOGFILE}" LOG)
  message(FATAL_ERROR "Unable to generate ${TGTIMG} with ${RTWIZ}, aborting.\nSee ${LOGFILE} for more details.\n${LOG}")
endif(NOT EXISTS "${TGTIMG}")

# pixcmp the results with the control image to make sure rtwiz got it right
file(APPEND "${LOGFILE}" "\nComparing ${TGTIMG} to ${CTRLIMG}:\n")
execute_process(
  COMMAND "${PIXCMP_EXEC}" "${TGTIMG}" "${CTRLIMG}"
  RESULT_VARIABLE pixcmp_result
  OUTPUT_VARIABLE rtwiz_log
  ERROR_VARIABLE rtwiz_log
)
file(APPEND "${LOGFILE}" "${rtwiz_log}")

if(pixcmp_result)
  file(APPEND "${LOGFILE}" "Failure: ${pixcmp_result}")
  file(READ "${LOGFILE}" LOG)
  message(
    FATAL_ERROR
    "Differences found between ${TGTIMG} and ${CTRLIMG} with ${PIXCMP_EXEC}, aborting.\nSee ${LOGFILE} for more details.\n${LOG}"
  )
endif(pixcmp_result)

execute_process(
  COMMAND "/usr/local/bin/cmake" -E remove -f ${TGTIMG}
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
