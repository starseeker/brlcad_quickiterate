# cmake file for rhino


# lets cmake if() recognize numbers and boolean constants.
if (POLICY CMP0012)
  cmake_policy(SET CMP0012 NEW)
endif (POLICY CMP0012)

# Values set at CMake configure time
set(CSDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/rhino")
set(CBDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/regress/gcv/rhino")
set(LOGFILE "${CBDIR}/regress-rhino-g.log")

file(WRITE "${LOGFILE}" "Starting rhino testing\n\n")

# The executable locations aren't know at CMake configure time, so one of them
# is passed in via the EXEC variable at runtime.  De-quote it and assign it to
# the appropriate variable.
string(REPLACE "\\" "" GCV_EXEC "${EXEC}")
if(NOT EXISTS "${GCV_EXEC}")
  file(WRITE "${LOGFILE}" "gcv not found at location \"${GCV_EXEC}\" - aborting\n")
  message(FATAL_ERROR "Unable to find gcv, aborting.\nSee ${LOGFILE} for more details.")
endif(NOT EXISTS "${GCV_EXEC}")

# check for input file in source and binary dir - priority of src
if(EXISTS "${CSDIR}/idef_test.3dm")
  set(in_path "${CSDIR}/idef_test.3dm")
elseif(EXISTS "${CBDIR}/idef_test.3dm")
  set(in_path "${CBDIR}/idef_test.3dm")
elseif(FALSE AND EXISTS "${CSDIR}/../idef_test.3dm")
  # if this is an exporter, check regress/gcv for .g files used to test multiple converters
  set(in_path "${CSDIR}/../idef_test.3dm")
else()
  message(FATAL_ERROR "[gcv] could not find idef_test.3dm, searched ${CSDIR} and ${CBDIR}. aborting.")
endif(EXISTS "${CSDIR}/idef_test.3dm")

# blow away expected output file if it exists for some reason
set(ofiles  "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/regress/gcv/rhino/conv-idef_test.g")
foreach(of ${ofiles})
  file(REMOVE "${of}")
endforeach(of ${ofiles})

# Run the converter for idef_test.3dm
set(log_msg "Running:
  COMMAND \"${GCV_EXEC}\" \"${in_path}\" \"/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/regress/gcv/rhino/conv-idef_test.g\"\n\n")
file(APPEND "${LOGFILE}" "${log_msg}")

execute_process(
  COMMAND "${GCV_EXEC}" "${in_path}" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_notls/regress/gcv/rhino/conv-idef_test.g"
  RESULTS_VARIABLE curr_result OUTPUT_VARIABLE log_msg ERROR_VARIABLE log_msg
  )

file(APPEND "${LOGFILE}" "${log_msg}\n")
unset(log_msg)
  
# check output files exists and are a reasonable size
foreach(out_file ${ofiles})
  if(NOT EXISTS "${out_file}")
    file(APPEND "${LOGFILE}" "FAILURE: ${curr_result}, ${out_file} not found.")
    message(FATAL_ERROR "[gcv] Unable to convert ${in_path} to ${out_file} with ${GCV_EXEC}, aborting.\nSee ${LOGFILE} for more details.")
  else()
    file(SIZE "${out_file}" file_size)	# size in bytes
    if("${file_size}" GREATER "40712")
      math(EXPR size_delta "${file_size} - 40712")
    else("${file_size}" GREATER "40712")
      math(EXPR size_delta "40712 - ${file_size}")
    endif("${file_size}" GREATER "40712")

    if("${size_delta}" GREATER "20000")
      file(APPEND "${LOGFILE}" "FAILURE: ${curr_result} file size is ${file_size} for ${out_file}\n    Expecting 40712 with delta 20000")
      message(FATAL_ERROR "[gcv] Failure while converting , see ${LOGFILE} for more info.")
    endif("${size_delta}" GREATER "20000")
  endif(NOT EXISTS "${out_file}")


  # cleanup file from build dir
  file(REMOVE "${out_file}")
endforeach(out_file ${ofiles})
    
