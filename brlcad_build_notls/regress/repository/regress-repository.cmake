string(REPLACE "\\" "" REPOSITORY_CHECK_EXEC "${EXEC}")
set(FILES_LIST "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/cmakefiles.cmake")
set(B_SOURCE_DIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad")
set(LOGFILE "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/regress/repository/regress-repository.log")

file(
  WRITE
  ${LOGFILE}
  "Running repository check:\n${REPOSITORY_CHECK_EXEC} ${REPOSITORY_FILE_SET} ${FILES_LIST} \"${B_SOURCE_DIR}\"\n"
)
execute_process(
  COMMAND "${REPOSITORY_CHECK_EXEC}" "${FILES_LIST}" "${B_SOURCE_DIR}"
  RESULT_VARIABLE repository_result
  OUTPUT_VARIABLE repository_log
  ERROR_VARIABLE repository_log
)
if(repository_result)
  file(APPEND ${LOGFILE} "${repository_log}\n")
  file(READ ${LOGFILE} LOGMSG)
  message(
    FATAL_ERROR
    "[repository check] Failure: ${repository_result} - see details in log file: ${LOGFILE}\n${LOGMSG}"
  )
endif(repository_result)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
