# cmake file for fbx


# lets cmake if() recognize numbers and boolean constants.
if (POLICY CMP0012)
  cmake_policy(SET CMP0012 NEW)
endif (POLICY CMP0012)

# Values set at CMake configure time
set(CSDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/fbx")
set(CBDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/fbx")
set(LOGFILE "${CBDIR}/regress-fbx-g.log")

file(WRITE "${LOGFILE}" "Starting fbx testing\n\n")

# The executable locations aren't know at CMake configure time, so one of them
# is passed in via the EXEC variable at runtime.  De-quote it and assign it to
# the appropriate variable.
string(REPLACE "\\" "" GCV_EXEC "${EXEC}")
if(NOT EXISTS "${GCV_EXEC}")
  file(WRITE "${LOGFILE}" "gcv not found at location \"${GCV_EXEC}\" - aborting\n")
  message(FATAL_ERROR "Unable to find gcv, aborting.\nSee ${LOGFILE} for more details.")
endif(NOT EXISTS "${GCV_EXEC}")

# check for input file in source and binary dir - priority of src
if(EXISTS "${CSDIR}/shapes.fbx")
  set(in_path "${CSDIR}/shapes.fbx")
elseif(EXISTS "${CBDIR}/shapes.fbx")
  set(in_path "${CBDIR}/shapes.fbx")
elseif(FALSE AND EXISTS "${CSDIR}/../shapes.fbx")
  # if this is an exporter, check regress/gcv for .g files used to test multiple converters
  set(in_path "${CSDIR}/../shapes.fbx")
else()
  message(FATAL_ERROR "[gcv] could not find shapes.fbx, searched ${CSDIR} and ${CBDIR}. aborting.")
endif(EXISTS "${CSDIR}/shapes.fbx")

# blow away expected output file if it exists for some reason
set(ofiles  "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/fbx/conv-shapes.g")
foreach(of ${ofiles})
  file(REMOVE "${of}")
endforeach(of ${ofiles})

# Run the converter for shapes.fbx
set(log_msg "Running:
  COMMAND \"${GCV_EXEC}\" \"--input-format=assetimport\" \"${in_path}\" \"/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/fbx/conv-shapes.g\"\n\n")
file(APPEND "${LOGFILE}" "${log_msg}")

execute_process(
  COMMAND "${GCV_EXEC}" "--input-format=assetimport" "${in_path}" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/fbx/conv-shapes.g"
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
    if("${file_size}" GREATER "168920")
      math(EXPR size_delta "${file_size} - 168920")
    else("${file_size}" GREATER "168920")
      math(EXPR size_delta "168920 - ${file_size}")
    endif("${file_size}" GREATER "168920")

    if("${size_delta}" GREATER "5067")
      file(APPEND "${LOGFILE}" "FAILURE: ${curr_result} file size is ${file_size} for ${out_file}\n    Expecting 168920 with delta 5067")
      message(FATAL_ERROR "[gcv] Failure while converting , see ${LOGFILE} for more info.")
    endif("${size_delta}" GREATER "5067")
  endif(NOT EXISTS "${out_file}")


  # cleanup file from build dir
  file(REMOVE "${out_file}")
endforeach(out_file ${ofiles})
    
