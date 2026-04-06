# cmake file for assxml


# lets cmake if() recognize numbers and boolean constants.
if (POLICY CMP0012)
  cmake_policy(SET CMP0012 NEW)
endif (POLICY CMP0012)

# Values set at CMake configure time
set(CSDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/regress/gcv/assetimport-xml")
set(CBDIR "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/assetimport-xml")
set(LOGFILE "${CBDIR}/regress-g-assxml.log")

file(WRITE "${LOGFILE}" "Starting assxml testing\n\n")

# The executable locations aren't know at CMake configure time, so one of them
# is passed in via the EXEC variable at runtime.  De-quote it and assign it to
# the appropriate variable.
string(REPLACE "\\" "" GCV_EXEC "${EXEC}")
if(NOT EXISTS "${GCV_EXEC}")
  file(WRITE "${LOGFILE}" "gcv not found at location \"${GCV_EXEC}\" - aborting\n")
  message(FATAL_ERROR "Unable to find gcv, aborting.\nSee ${LOGFILE} for more details.")
endif(NOT EXISTS "${GCV_EXEC}")

# check for input file in source and binary dir - priority of src
if(EXISTS "${CSDIR}/tetra.g")
  set(in_path "${CSDIR}/tetra.g")
elseif(EXISTS "${CBDIR}/tetra.g")
  set(in_path "${CBDIR}/tetra.g")
elseif(TRUE AND EXISTS "${CSDIR}/../tetra.g")
  # if this is an exporter, check regress/gcv for .g files used to test multiple converters
  set(in_path "${CSDIR}/../tetra.g")
else()
  message(FATAL_ERROR "[gcv] could not find tetra.g, searched ${CSDIR} and ${CBDIR}. aborting.")
endif(EXISTS "${CSDIR}/tetra.g")

# blow away expected output file if it exists for some reason
set(ofiles  "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/assetimport-xml/conv-tetra.assxml")
foreach(of ${ofiles})
  file(REMOVE "${of}")
endforeach(of ${ofiles})

# Run the converter for tetra.g
set(log_msg "Running:
  COMMAND \"${GCV_EXEC}\" \"--output-format=assetimport\" \"${in_path}\" \"/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/assetimport-xml/conv-tetra.assxml\"\n\n")
file(APPEND "${LOGFILE}" "${log_msg}")

execute_process(
  COMMAND "${GCV_EXEC}" "--output-format=assetimport" "${in_path}" "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build_cov/regress/gcv/assetimport-xml/conv-tetra.assxml"
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
    if("${file_size}" GREATER "3425")
      math(EXPR size_delta "${file_size} - 3425")
    else("${file_size}" GREATER "3425")
      math(EXPR size_delta "3425 - ${file_size}")
    endif("${file_size}" GREATER "3425")

    if("${size_delta}" GREATER "350")
      file(APPEND "${LOGFILE}" "FAILURE: ${curr_result} file size is ${file_size} for ${out_file}\n    Expecting 3425 with delta 350")
      message(FATAL_ERROR "[gcv] Failure while converting , see ${LOGFILE} for more info.")
    endif("${size_delta}" GREATER "350")
  endif(NOT EXISTS "${out_file}")


  # cleanup file from build dir
  file(REMOVE "${out_file}")
endforeach(out_file ${ofiles})
    
