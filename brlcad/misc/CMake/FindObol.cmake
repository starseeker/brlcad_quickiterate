#[=======================================================================[.rst:
FindObol
--------

Find the Obol scene-graph library (a BRL-CAD–hosted minimalist rework of
Coin / Open Inventor).

Obol can be consumed in three ways, tried in this order:

1. **Direct variable path** – if ``OBOL_INCLUDE_DIRS`` and ``OBOL_LIBRARY``
   are both set (e.g. passed via ``-DOBOL_INCLUDE_DIRS=...`` on the cmake
   command line), an IMPORTED target is created directly without any
   filesystem search.  This is the recommended path when Obol has been
   built separately (e.g. with the script in bext/obol) because it avoids
   the add_subdirectory path that can conflict with BRL-CAD's cmake
   function overrides.

2. **Installed package** – a pre-built Obol package located via
   ``find_package(Obol NO_MODULE)`` (uses ``obol-config.cmake``).  The
   caller may set ``OBOL_INSTALL_DIR`` to guide the search directly to an
   install prefix.  Note: BRL-CAD's ``brlcad_find_package`` wrapper always
   resets ``Obol_ROOT`` to ``CMAKE_BINARY_DIR``, so use ``OBOL_INSTALL_DIR``
   instead to specify an external install prefix.

3. **Source-tree subdirectory** – when the ``obol/`` directory is present
   as a sibling of the BRL-CAD source tree, CMake can add it directly via
   ``add_subdirectory``.  The caller may pre-set ``OBOL_SOURCE_DIR`` to
   point at the Obol source root.  This path is skipped when
   ``OBOL_INSTALL_DIR`` or the direct-variable path is used.

IMPORTED Targets
^^^^^^^^^^^^^^^^

``Obol::Obol``
  The Obol library, available when ``Obol_FOUND`` is TRUE.

``Obol``
  Unnamespaced alias for ``Obol::Obol``, provided for internal BRL-CAD use.

Result Variables
^^^^^^^^^^^^^^^^

``Obol_FOUND``        – TRUE when the library is available.
``OBOL_INCLUDE_DIRS`` – Include directories for Obol headers.
``OBOL_LIBRARIES``    – Libraries to link against (``Obol::Obol``).

Hints
^^^^^

``OBOL_INSTALL_DIR``  – Root directory of an Obol installation (takes
                         precedence over ``OBOL_ROOT`` and the source path).
``OBOL_LIBRARY``      – Full path to the Obol shared library.  When set
                         together with ``OBOL_INCLUDE_DIRS`` the direct-
                         variable path is used and no search is performed.
``OBOL_SOURCE_DIR``   – Root directory of the Obol *source* tree (enables
                         the add_subdirectory path).
#]=======================================================================]

# ------------------------------------------------------------------
# 0.  Already found in a parent scope — nothing to do.
# ------------------------------------------------------------------
if(TARGET Obol::Obol)
  if(NOT TARGET Obol)
    add_library(Obol ALIAS Obol::Obol)
  endif()
  get_target_property(OBOL_INCLUDE_DIRS Obol::Obol INTERFACE_INCLUDE_DIRECTORIES)
  if(NOT OBOL_INCLUDE_DIRS)
    set(OBOL_INCLUDE_DIRS "")
  endif()
  set(OBOL_LIBRARIES Obol::Obol)
  set(Obol_FOUND TRUE)
endif()

# ------------------------------------------------------------------
# 1.  Direct-variable path: OBOL_INCLUDE_DIRS + OBOL_LIBRARY pre-set
#     This avoids add_subdirectory entirely — safest for pre-built Obol.
# ------------------------------------------------------------------
if(NOT Obol_FOUND AND OBOL_INCLUDE_DIRS AND OBOL_LIBRARY)
  if(NOT TARGET Obol::Obol)
    # Detect a sibling OSMesa library (Obol's bundled mesa links against it).
    get_filename_component(_obol_libdir "${OBOL_LIBRARY}" DIRECTORY)
    set(_obol_osmesa_libs)
    foreach(_mesa_name libosmesa.so libOSMesa.so)
      if(EXISTS "${_obol_libdir}/${_mesa_name}")
        list(APPEND _obol_osmesa_libs "${_obol_libdir}/${_mesa_name}")
        break()
      endif()
    endforeach()

    add_library(Obol::Obol SHARED IMPORTED)
    set_target_properties(Obol::Obol PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES        "${OBOL_INCLUDE_DIRS}"
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${OBOL_INCLUDE_DIRS}"
      IMPORTED_LOCATION                    "${OBOL_LIBRARY}"
    )
    if(_obol_osmesa_libs)
      set_property(TARGET Obol::Obol APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES "${_obol_osmesa_libs}")
      set_property(TARGET Obol::Obol APPEND PROPERTY
        BUILD_RPATH "${_obol_libdir}")
    endif()
  endif()
  if(NOT TARGET Obol)
    add_library(Obol ALIAS Obol::Obol)
  endif()
  set(OBOL_LIBRARIES Obol::Obol)
  set(Obol_FOUND TRUE)
endif()

# ------------------------------------------------------------------
# 2.  Installed-package path: find_package(Obol NO_MODULE)
#     Use OBOL_INSTALL_DIR to avoid brlcad_find_package's Obol_ROOT override.
# ------------------------------------------------------------------
if(NOT Obol_FOUND)
  # OBOL_INSTALL_DIR takes priority; fall back to OBOL_ROOT if set externally.
  set(_obol_install_hint)
  if(OBOL_INSTALL_DIR)
    set(_obol_install_hint PATHS "${OBOL_INSTALL_DIR}" NO_DEFAULT_PATH)
  elseif(DEFINED OBOL_ROOT AND NOT OBOL_ROOT STREQUAL CMAKE_BINARY_DIR)
    set(_obol_install_hint PATHS "${OBOL_ROOT}" NO_DEFAULT_PATH)
  endif()

  if(_obol_install_hint)
    # Save and restore Obol_ROOT so brlcad_find_package's override doesn't
    # interfere with a recursive CONFIG search inside find_package.
    set(_saved_Obol_ROOT "${Obol_ROOT}")
    find_package(Obol QUIET CONFIG ${_obol_install_hint})
    set(Obol_ROOT "${_saved_Obol_ROOT}")
    set(_obol_installed TRUE)
    # Promote to GLOBAL so TARGET Obol::Obol is visible in all directories.
    # By default, IMPORTED targets are directory-scoped; without GLOBAL they
    # are invisible to brlcad_find_package calls made from other directories.
    if(TARGET Obol::Obol)
      set_target_properties(Obol::Obol PROPERTIES IMPORTED_GLOBAL TRUE)
    endif()
  endif()
endif()

# ------------------------------------------------------------------
# 3.  Source-subdirectory path: add obol/ directly
#     Only attempted when neither direct vars nor an installed package
#     worked AND OBOL_INSTALL_DIR is not set (which would imply the user
#     specifically wants the installed version).
# ------------------------------------------------------------------
if(NOT Obol_FOUND AND NOT TARGET Obol::Obol AND NOT OBOL_INSTALL_DIR)
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
    # this CMake project.  We suppress Obol's own tests, examples, viewer
    # and mentor to keep build times short and avoid the bundled FLTK
    # add_subdirectory (which clashes with BRL-CAD's message() override).
    set(_obol_TESTS_save    ${OBOL_BUILD_TESTS})
    set(_obol_EXAMPLES_save ${OBOL_BUILD_EXAMPLES})
    set(_obol_MENTOR_save   ${OBOL_BUILD_MENTOR})
    set(_obol_VIEWER_save   ${OBOL_BUILD_VIEWER})
    set(OBOL_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(OBOL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(OBOL_BUILD_MENTOR   OFF CACHE BOOL "" FORCE)
    set(OBOL_BUILD_VIEWER   OFF CACHE BOOL "" FORCE)
    add_subdirectory("${OBOL_SOURCE_DIR}"
                     "${CMAKE_BINARY_DIR}/obol_build"
                     EXCLUDE_FROM_ALL)
    set(OBOL_BUILD_TESTS    ${_obol_TESTS_save}    CACHE BOOL "" FORCE)
    set(OBOL_BUILD_EXAMPLES ${_obol_EXAMPLES_save} CACHE BOOL "" FORCE)
    set(OBOL_BUILD_MENTOR   ${_obol_MENTOR_save}   CACHE BOOL "" FORCE)
    set(OBOL_BUILD_VIEWER   ${_obol_VIEWER_save}   CACHE BOOL "" FORCE)
    # Obol is third-party code; suppress BRL-CAD's strict warning flags
    # (e.g. -Werror=shadow, -Werror=float-equal, -Werror=deprecated-copy)
    # that are injected via CMAKE_CXX_FLAGS / CMAKE_C_FLAGS.
    # Also disable LTO to avoid ODR false-positives from -flto=auto.
    if(TARGET Obol)
      target_compile_options(Obol PRIVATE -w -fno-lto)
      target_link_options(Obol PRIVATE -fno-lto)
    endif()
    set(_obol_src_used TRUE)
  endif()
endif()

# ------------------------------------------------------------------
# 4.  Collect results — handle both namespaced (Obol::Obol) and
#     unnamespaced (Obol) targets that different probe paths create.
# ------------------------------------------------------------------
# Always collect results, regardless of which discovery path succeeded.
# Paths 0-3 may set Obol_FOUND via different mechanisms (pre-existing target,
# installed config, or source subdirectory), but OBOL_LIBRARIES and
# OBOL_INCLUDE_DIRS must be set in all cases for downstream consumers.
# Prefer the namespaced target (installed package), fall back to plain.
if(TARGET Obol::Obol)
  set(_obol_tgt Obol::Obol)
elseif(TARGET Obol)
  set(_obol_tgt Obol)
else()
  set(_obol_tgt "")
endif()

if(_obol_tgt)
  set(Obol_FOUND TRUE)

  get_target_property(OBOL_INCLUDE_DIRS ${_obol_tgt} INTERFACE_INCLUDE_DIRECTORIES)
  if(NOT OBOL_INCLUDE_DIRS)
    set(OBOL_INCLUDE_DIRS "")
  endif()

  # Mark Obol include directories as SYSTEM so that BRL-CAD's strict
  # warning flags (-Wfloat-equal, -Werror=unused-parameter, etc.) are not
  # applied to Obol's own header files.
  if(OBOL_INCLUDE_DIRS AND NOT ${_obol_tgt} STREQUAL "Obol::Obol")
    set_property(TARGET ${_obol_tgt} APPEND PROPERTY
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${OBOL_INCLUDE_DIRS}")
  elseif(OBOL_INCLUDE_DIRS)
    set_property(TARGET Obol::Obol APPEND PROPERTY
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${OBOL_INCLUDE_DIRS}")
  endif()

  # Ensure both Obol and Obol::Obol targets exist for all consumers.
  if(NOT TARGET Obol::Obol)
    add_library(Obol::Obol ALIAS Obol)
  endif()
  if(NOT TARGET Obol)
    # Can't create a plain ALIAS to an ALIAS, so import directly.
    get_target_property(_obol_loc Obol::Obol IMPORTED_LOCATION_RELEASE)
    if(NOT _obol_loc)
      get_target_property(_obol_loc Obol::Obol IMPORTED_LOCATION)
    endif()
    add_library(Obol SHARED IMPORTED)
    set_target_properties(Obol PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES        "${OBOL_INCLUDE_DIRS}"
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${OBOL_INCLUDE_DIRS}"
      IMPORTED_LOCATION                    "${_obol_loc}"
    )
  endif()

  set(OBOL_LIBRARIES Obol::Obol)

  # Detect a sibling OSMesa library and add it as a transitive dependency.
  # libObol.so links against libosmesa.so; without this the linker can't
  # resolve OSMesaGetProcAddress when linking executables that use Obol.
  get_target_property(_obol_loc_rel Obol::Obol IMPORTED_LOCATION_RELEASE)
  if(NOT _obol_loc_rel)
    get_target_property(_obol_loc_rel Obol::Obol IMPORTED_LOCATION)
  endif()
  if(_obol_loc_rel)
    get_filename_component(_obol_libdir "${_obol_loc_rel}" DIRECTORY)
    set(_obol_osmesa_libs "")
    foreach(_mesa_name libosmesa.so libOSMesa.so)
      if(EXISTS "${_obol_libdir}/${_mesa_name}")
        list(APPEND _obol_osmesa_libs "${_obol_libdir}/${_mesa_name}")
        break()
      endif()
    endforeach()
    if(_obol_osmesa_libs)
      get_target_property(_existing_ill Obol::Obol INTERFACE_LINK_LIBRARIES)
      if(NOT _existing_ill OR NOT "${_obol_osmesa_libs}" IN_LIST _existing_ill)
        set_property(TARGET Obol::Obol APPEND PROPERTY
          INTERFACE_LINK_LIBRARIES "${_obol_osmesa_libs}")
      endif()
      set_property(TARGET Obol::Obol APPEND PROPERTY
        BUILD_RPATH "${_obol_libdir}"
        INSTALL_RPATH "${_obol_libdir}")
    endif()
  endif()

else()
  set(Obol_FOUND FALSE)
  set(OBOL_INCLUDE_DIRS "")
  set(OBOL_LIBRARIES "")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Obol
  REQUIRED_VARS Obol_FOUND
  REASON_FAILURE_MESSAGE
    "Obol not found. Use -DOBOL_INSTALL_DIR=<prefix> for a pre-built install, -DOBOL_INCLUDE_DIRS=<dir> -DOBOL_LIBRARY=<lib> for explicit paths, or -DOBOL_SOURCE_DIR=<src> to build from source."
)

mark_as_advanced(OBOL_INCLUDE_DIRS OBOL_LIBRARIES OBOL_LIBRARY OBOL_INSTALL_DIR)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
