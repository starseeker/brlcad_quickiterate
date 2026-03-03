#[=======================================================================[.rst:
FindObol
--------

Find the Obol scene-graph library (a BRL-CAD–hosted minimalist rework of
Coin / Open Inventor).

Obol can be consumed in two ways:

1. **Source-tree subdirectory** – when the ``obol/`` directory is present
   as a sibling of the BRL-CAD source tree, CMake can add it directly via
   ``add_subdirectory``.  The caller may pre-set ``OBOL_SOURCE_DIR`` to
   point at the Obol source root.

2. **Installed package** – a pre-built Obol package located via
   ``find_package(Obol NO_MODULE)`` (uses ``obol-config.cmake``).  The
   caller may set ``OBOL_ROOT`` or ``CMAKE_PREFIX_PATH`` to guide the
   search.

IMPORTED Targets
^^^^^^^^^^^^^^^^

``Obol::Obol``
  The Obol library, available when ``Obol_FOUND`` is TRUE.

Result Variables
^^^^^^^^^^^^^^^^

``Obol_FOUND``        – TRUE when the library is available.
``OBOL_INCLUDE_DIRS`` – Include directories for Obol headers.
``OBOL_LIBRARIES``    – Libraries to link against.

Hints
^^^^^

``OBOL_ROOT``       – Root directory of an Obol installation.
``OBOL_SOURCE_DIR`` – Root directory of the Obol *source* tree (enables
                       the add_subdirectory path).
#]=======================================================================]

# ------------------------------------------------------------------
# 1.  Source-subdirectory path: add obol/ directly if source is present
# ------------------------------------------------------------------
if(NOT TARGET Obol)
  # Caller may supply OBOL_SOURCE_DIR; fall back to the conventional
  # sibling layout used in this repository.
  if(NOT OBOL_SOURCE_DIR)
    get_filename_component(_obol_sibling
      "${CMAKE_CURRENT_SOURCE_DIR}/../obol" ABSOLUTE)
    if(EXISTS "${_obol_sibling}/CMakeLists.txt")
      set(OBOL_SOURCE_DIR "${_obol_sibling}")
    endif()
  endif()

  if(OBOL_SOURCE_DIR AND EXISTS "${OBOL_SOURCE_DIR}/CMakeLists.txt")
    # Build Obol as a subdirectory so its Obol target is available in
    # this CMake project.  We suppress Obol's own tests and examples to
    # keep build times short.
    set(_obol_TESTS_save   ${OBOL_BUILD_TESTS})
    set(_obol_EXAMPLES_save ${OBOL_BUILD_EXAMPLES})
    set(_obol_MENTOR_save  ${OBOL_BUILD_MENTOR})
    set(OBOL_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
    set(OBOL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(OBOL_BUILD_MENTOR  OFF CACHE BOOL "" FORCE)
    add_subdirectory("${OBOL_SOURCE_DIR}"
                     "${CMAKE_BINARY_DIR}/obol_build"
                     EXCLUDE_FROM_ALL)
    set(OBOL_BUILD_TESTS   ${_obol_TESTS_save}   CACHE BOOL "" FORCE)
    set(OBOL_BUILD_EXAMPLES ${_obol_EXAMPLES_save} CACHE BOOL "" FORCE)
    set(OBOL_BUILD_MENTOR  ${_obol_MENTOR_save}  CACHE BOOL "" FORCE)
    set(_obol_src_used TRUE)
  endif()
endif()

# ------------------------------------------------------------------
# 2.  Installed-package path: find_package(Obol NO_MODULE)
# ------------------------------------------------------------------
if(NOT TARGET Obol AND NOT _obol_src_used)
  if(OBOL_ROOT)
    set(_obol_root_hint PATHS "${OBOL_ROOT}" NO_DEFAULT_PATH)
  endif()
  find_package(Obol QUIET CONFIG ${_obol_root_hint})
endif()

# ------------------------------------------------------------------
# 3.  Collect results
# ------------------------------------------------------------------
if(TARGET Obol)
  set(Obol_FOUND TRUE)

  # Populate convenience variables from the imported target's properties
  get_target_property(OBOL_INCLUDE_DIRS Obol INTERFACE_INCLUDE_DIRECTORIES)
  if(NOT OBOL_INCLUDE_DIRS)
    set(OBOL_INCLUDE_DIRS "")
  endif()

  if(NOT TARGET Obol::Obol)
    add_library(Obol::Obol ALIAS Obol)
  endif()

  set(OBOL_LIBRARIES Obol::Obol)
else()
  set(Obol_FOUND FALSE)
  set(OBOL_INCLUDE_DIRS "")
  set(OBOL_LIBRARIES "")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Obol
  REQUIRED_VARS Obol_FOUND
  REASON_FAILURE_MESSAGE
    "Obol not found.  Set OBOL_SOURCE_DIR to the Obol source tree or "
    "OBOL_ROOT to an installed Obol prefix."
)

mark_as_advanced(OBOL_INCLUDE_DIRS OBOL_LIBRARIES)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
