#                   A S C I I D O C . C M A K E
# BRL-CAD
#
# Copyright (c) 2025 United States Government as represented by
# the U.S. Army Research Laboratory.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following
# disclaimer in the documentation and/or other materials provided
# with the distribution.
#
# 3. The name of the author may not be used to endorse or promote
# products derived from this software without specific prior written
# permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
# OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
###
#
# CMake macro ADD_ASCIIDOC:  build and install documentation from AsciiDoc
# source files using the asciiquack processor.
#
# Usage:
#   ADD_ASCIIDOC(formats adoc_files outdir_var deps_list
#                [REQUIRED target1 ...])
#
# formats      - Semicolon-separated list of output formats to generate.
#                Supported: HTML MAN1 MAN3 MAN5 MANN
# adoc_files   - Either a CMake list variable name OR a single .adoc file path.
# outdir_var   - Ignored (kept for API compatibility).
# deps_list    - CMake target name to add generated files to (may be "").
#
# The macro generates HTML5 output for HTML format and troff man pages for
# MAN* formats.  MAN* formats also generate companion HTML files (installed
# to ${DOC_DIR}/html/man{section}/) so that the brlman GUI viewer and Tcl
# scripts (brlman.tcl, Archer.tcl) can locate and display man pages.
# Outputs are installed into the standard locations under ${DOC_DIR} and
# ${MAN_DIR} respectively.
#
# Example:
#   ADD_ASCIIDOC("HTML;MAN1" man1_ADOC "" "" REQUIRED brlman)

if(NOT COMMAND ADD_ASCIIDOC)

  # Locate asciiquack – look in the ext install tree first, then the PATH.
  find_program(
    ASCIIQUACK_EXECUTABLE
    asciiquack
    HINTS
      ${CMAKE_BINARY_DIR}
      ${BRLCAD_EXT_NOINSTALL_DIR}/${BIN_DIR}
      ${BRLCAD_EXT_DIR}/install/${BIN_DIR}
  )
  mark_as_advanced(ASCIIQUACK_EXECUTABLE)

  # ─────────────────────────────────────────────────────────────────────
  # Internal helper: run asciiquack to produce a single output file.
  # Called as a CMake -P script via add_custom_command.
  # ─────────────────────────────────────────────────────────────────────

  function(ADD_ASCIIDOC fmts in_adoc_files outdir deps_list)
    cmake_parse_arguments(A "" "" "REQUIRED" ${ARGN})

    # Check required targets exist before registering any commands.
    if(A_REQUIRED)
      foreach(rdep ${A_REQUIRED})
        if(NOT TARGET ${rdep})
          return()
        endif()
      endforeach()
    endif()

    # Resolve the file list (list variable name or direct path).
    list(GET ${in_adoc_files} 0 _first)
    if("${_first}" MATCHES "NOTFOUND")
      # Direct file path supplied.
      set(adoc_files ${in_adoc_files})
    else()
      # Variable name supplied.
      set(adoc_files ${${in_adoc_files}})
    endif()

    if(NOT ASCIIQUACK_EXECUTABLE)
      # asciiquack not found – just register the source files so the
      # build system knows about them without attempting conversion.
      foreach(adoc_file ${adoc_files})
        set(src "${CMAKE_CURRENT_SOURCE_DIR}/${adoc_file}")
        cmakefiles("${adoc_file}")
      endforeach()
      return()
    endif()

    # Build a stable target-name root from directory components.
    list(GET adoc_files 0 _first_adoc)
    get_filename_component(_abs "${CMAKE_CURRENT_SOURCE_DIR}/${_first_adoc}" ABSOLUTE)
    get_filename_component(_dir "${_abs}" PATH)
    get_filename_component(_d1 "${_dir}" NAME)
    get_filename_component(_d2 "${_dir}/.." ABSOLUTE)
    get_filename_component(_d2 "${_d2}" NAME)
    get_filename_component(_d3 "${_dir}/../.." ABSOLUTE)
    get_filename_component(_d3 "${_d3}" NAME)
    set(_target_root "${_d3}-${_d2}-${_d1}")

    set(all_outfiles)

    foreach(adoc_file ${adoc_files})
      get_filename_component(_stem "${adoc_file}" NAME_WE)
      set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${adoc_file}")

      foreach(fmt ${fmts})
        string(TOUPPER "${fmt}" _FMT)
        set(_html_section_subdir "")  # companion HTML section dir; set by MAN* cases

        if(_FMT STREQUAL "HTML")
          set(_outfile "${CMAKE_CURRENT_BINARY_DIR}/${_stem}.html")
          set(_backend "html5")
          set(_doctype "article")
          set(_install_dir "${DOC_DIR}/html/asciidoc")

        elseif(_FMT STREQUAL "MAN1")
          set(_outfile "${CMAKE_CURRENT_BINARY_DIR}/${_stem}.1")
          set(_backend "manpage")
          set(_doctype "manpage")
          set(_install_dir "${MAN_DIR}/man1")
          set(_html_section_subdir "man1")

        elseif(_FMT STREQUAL "MAN3")
          set(_outfile "${CMAKE_CURRENT_BINARY_DIR}/${_stem}.3")
          set(_backend "manpage")
          set(_doctype "manpage")
          set(_install_dir "${MAN_DIR}/man3")
          set(_html_section_subdir "man3")

        elseif(_FMT STREQUAL "MAN5")
          set(_outfile "${CMAKE_CURRENT_BINARY_DIR}/${_stem}.5")
          set(_backend "manpage")
          set(_doctype "manpage")
          set(_install_dir "${MAN_DIR}/man5")
          set(_html_section_subdir "man5")

        elseif(_FMT STREQUAL "MANN")
          set(_outfile "${CMAKE_CURRENT_BINARY_DIR}/${_stem}.nged")
          set(_backend "manpage")
          set(_doctype "manpage")
          set(_install_dir "${MAN_DIR}/mann")
          set(_html_section_subdir "mann")

        else()
          message(WARNING "ADD_ASCIIDOC: unknown format '${fmt}' – skipping")
          continue()
        endif()

        add_custom_command(
          OUTPUT "${_outfile}"
          COMMAND "${ASCIIQUACK_EXECUTABLE}"
                  -b "${_backend}"
                  -d "${_doctype}"
                  -o "${_outfile}"
                  "${_src}"
          DEPENDS "${_src}"
          COMMENT "asciiquack: ${_stem} (${_FMT})"
          VERBATIM
        )

        install(FILES "${_outfile}" DESTINATION "${_install_dir}")
        list(APPEND all_outfiles "${_outfile}")

        # For man page formats, also generate HTML so the brlman GUI viewer,
        # brlman.tcl, and Archer.tcl can find pages in html/man{section}/.
        if(_html_section_subdir)
          set(_html_outfile "${CMAKE_CURRENT_BINARY_DIR}/${_stem}.html")
          add_custom_command(
            OUTPUT "${_html_outfile}"
            COMMAND "${ASCIIQUACK_EXECUTABLE}"
                    -b html5
                    -d manpage
                    -o "${_html_outfile}"
                    "${_src}"
            DEPENDS "${_src}"
            COMMENT "asciiquack: ${_stem} (HTML)"
            VERBATIM
          )
          install(FILES "${_html_outfile}" DESTINATION "${DOC_DIR}/html/${_html_section_subdir}")
          list(APPEND all_outfiles "${_html_outfile}")
        endif()
      endforeach()

      cmakefiles("${adoc_file}")
    endforeach()

    if(all_outfiles)
      # Derive a unique target name.
      set(_tgt "asciidoc-${_target_root}")
      set(_inc 0)
      while(TARGET "${_tgt}")
        math(EXPR _inc "${_inc} + 1")
        set(_tgt "asciidoc-${_target_root}-${_inc}")
      endwhile()

      add_custom_target("${_tgt}" ALL DEPENDS ${all_outfiles})
      set_target_properties("${_tgt}" PROPERTIES FOLDER "DocBuild/AsciiDoc")

      if(deps_list AND TARGET "${deps_list}")
        add_dependencies("${deps_list}" "${_tgt}")
      endif()
    endif()

  endfunction(ADD_ASCIIDOC)

endif(NOT COMMAND ADD_ASCIIDOC)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
