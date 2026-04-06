# cmake file for x3d


# lets cmake if() recognize numbers and boolean constants.
if (POLICY CMP0012)
  cmake_policy(SET CMP0012 NEW)
endif (POLICY CMP0012)

# Values set at CMake configure time
set(CSDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/x3d")
set(CBDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/x3d")
set(LOGFILE "${CBDIR}/regress-x3d-g.log")

file(WRITE "${LOGFILE}" "Starting x3d testing\n\n")

# The executable locations aren't know at CMake configure time, so one of them
# is passed in via the EXEC variable at runtime.  De-quote it and assign it to
# the appropriate variable.
string(REPLACE "\\" "" GCV_EXEC "${EXEC}")
if(NOT EXISTS "${GCV_EXEC}")
  file(WRITE "${LOGFILE}" "gcv not found at location \"${GCV_EXEC}\" - aborting\n")
  message(FATAL_ERROR "Unable to find gcv, aborting.\nSee ${LOGFILE} for more details.")
endif(NOT EXISTS "${GCV_EXEC}")

# check for input file in source and binary dir - priority of src
if(EXISTS "${CSDIR}/tetra.x3d")
  set(in_path "${CSDIR}/tetra.x3d")
elseif(EXISTS "${CBDIR}/tetra.x3d")
  set(in_path "${CBDIR}/tetra.x3d")
elseif(FALSE AND EXISTS "${CSDIR}/../tetra.x3d")
  # if this is an exporter, check regress/gcv for .g files used to test multiple converters
  set(in_path "${CSDIR}/../tetra.x3d")
else()
  message(FATAL_ERROR "[gcv] could not find tetra.x3d, searched ${CSDIR} and ${CBDIR}. aborting.")
endif(EXISTS "${CSDIR}/tetra.x3d")

# blow away expected output file if it exists for some reason
set(ofiles  "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/x3d/conv-tetra.g")
foreach(of ${ofiles})
  file(REMOVE "${of}")
endforeach(of ${ofiles})

# Run the converter for tetra.x3d
set(log_msg "Running:
  COMMAND \"${GCV_EXEC}\" \"--input-format=assetimport\" \"${in_path}\" \"/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/x3d/conv-tetra.g\"\n\n")
file(APPEND "${LOGFILE}" "${log_msg}")

execute_process(
  COMMAND "${GCV_EXEC}" "--input-format=assetimport" "${in_path}" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/x3d/conv-tetra.g"
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
    if("${file_size}" GREATER "792")
      math(EXPR size_delta "${file_size} - 792")
    else("${file_size}" GREATER "792")
      math(EXPR size_delta "792 - ${file_size}")
    endif("${file_size}" GREATER "792")

    if("${size_delta}" GREATER "350")
      file(APPEND "${LOGFILE}" "FAILURE: ${curr_result} file size is ${file_size} for ${out_file}\n    Expecting 792 with delta 350")
      message(FATAL_ERROR "[gcv] Failure while converting , see ${LOGFILE} for more info.")
    endif("${size_delta}" GREATER "350")
  endif(NOT EXISTS "${out_file}")


  # cleanup file from build dir
  file(REMOVE "${out_file}")
endforeach(out_file ${ofiles})
    
