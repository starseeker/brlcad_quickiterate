# FCStd (FreeCAD) importer plugin for libgcv.
#
# Uses open2open library sources compiled directly into the plugin (as an
# internal OBJECT library), linked against BRL-CAD's opennurbs, the system
# OpenCASCADE toolkit, and libzip for ZIP archive reading.
#
# When the required libraries (OpenCASCADE, libzip) are not available the
# plugin is silently skipped.

set(FCSTD_SRCS fcstd_read.cpp)

# ---------------------------------------------------------------------------
# Locate open2open source tree.  Default: alongside the BRL-CAD source root.
# Override with -DOPEN2OPEN_DIR=<path>.
# ---------------------------------------------------------------------------
if(NOT DEFINED OPEN2OPEN_DIR)
  set(OPEN2OPEN_DIR "${CMAKE_SOURCE_DIR}/../open2open/open2open"
      CACHE PATH "Path to the open2open source directory")
endif()

if(NOT IS_DIRECTORY "${OPEN2OPEN_DIR}")
  message(STATUS "gcv-fcstd: open2open not found at '${OPEN2OPEN_DIR}' -- FCStd plugin disabled")
  cmakefiles(${FCSTD_SRCS} CMakeLists.txt)
  return()
endif()

# ---------------------------------------------------------------------------
# OpenCASCADE
# ---------------------------------------------------------------------------
if(NOT OpenCASCADE_FOUND)
  # Check for a build_artifacts pre-built copy next to open2open
  set(_BUNDLED_OCCT_CMAKE
      "${OPEN2OPEN_DIR}/../build_artifacts/occt/lib/cmake/opencascade-7.9.3")
  if(EXISTS "${_BUNDLED_OCCT_CMAKE}/OpenCASCADEConfig.cmake" AND NOT OpenCASCADE_DIR)
    set(OpenCASCADE_DIR "${_BUNDLED_OCCT_CMAKE}" CACHE PATH
        "OpenCASCADE cmake config dir (auto-set)" FORCE)
  endif()
  find_package(OpenCASCADE QUIET)
endif()

if(NOT OpenCASCADE_FOUND)
  message(STATUS "gcv-fcstd: OpenCASCADE not found -- FCStd plugin disabled")
  cmakefiles(${FCSTD_SRCS} CMakeLists.txt)
  return()
endif()

# ---------------------------------------------------------------------------
# libzip (required for FCStd / ZIP archive reading)
# ---------------------------------------------------------------------------
find_library(FCSTD_LIBZIP_LIB NAMES zip)
find_path(FCSTD_LIBZIP_INC NAMES zip.h)
if(NOT FCSTD_LIBZIP_LIB OR NOT FCSTD_LIBZIP_INC)
  message(STATUS "gcv-fcstd: libzip not found -- FCStd plugin disabled")
  cmakefiles(${FCSTD_SRCS} CMakeLists.txt)
  return()
endif()

# ---------------------------------------------------------------------------
# BRL-CAD's opennurbs (pre-built by bext)
# Re-run find_package so OPENNURBS_LIBRARIES / OPENNURBS_INCLUDE_DIRS are
# available in this directory scope (same pattern used by libbrep).
# ---------------------------------------------------------------------------
brlcad_find_package(OPENNURBS REQUIRED SYSPATTERN openNURBS)
if(NOT OPENNURBS_LIBRARIES)
  message(STATUS "gcv-fcstd: BRL-CAD opennurbs library not found -- FCStd plugin disabled")
  cmakefiles(${FCSTD_SRCS} CMakeLists.txt)
  return()
endif()

# ---------------------------------------------------------------------------
# Collect OCCT libraries needed by open2open's brep / fcstd conversion.
# ---------------------------------------------------------------------------
set(_OCCT_LINK_LIBS
  ${OpenCASCADE_FoundationClasses_LIBRARIES}
  ${OpenCASCADE_ModelingData_LIBRARIES}
  ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
)
# TKCDF provides the LDOM XML parser used by fcstd_convert.cpp to parse
# Document.xml and GuiDocument.xml from FCStd archives.  It is part of
# ApplicationFramework but is needed independently of full XCAF support.
if(DEFINED OpenCASCADE_ApplicationFramework_LIBRARIES)
  list(APPEND _OCCT_LINK_LIBS TKCDF)
endif()
# XCAF (ApplicationFramework + DataExchange) is intentionally disabled here.
# attrs_convert.cpp uses XCAFDoc_MaterialTool::GetMaterial() as a static
# method, which was changed to an instance method in OCCT 7.6.  Since the
# fcstd_convert → OCCTToON_Brep path does not require XCAF we skip it to
# keep the plugin compatible with the system-installed OCCT package.
set(_OCCT_HAVE_XCAF FALSE)

# ---------------------------------------------------------------------------
# Build open2open sources as an OBJECT library
# ---------------------------------------------------------------------------
set(_O2O_SRCS
  "${OPEN2OPEN_DIR}/src/geom_convert.cpp"
  "${OPEN2OPEN_DIR}/src/brep_convert.cpp"
  "${OPEN2OPEN_DIR}/src/mesh_convert.cpp"
  "${OPEN2OPEN_DIR}/src/fcstd_convert.cpp"
)
if(_OCCT_HAVE_XCAF)
  list(APPEND _O2O_SRCS "${OPEN2OPEN_DIR}/src/attrs_convert.cpp")
endif()

add_library(gcv_fcstd_open2open OBJECT ${_O2O_SRCS})

target_include_directories(gcv_fcstd_open2open
  PUBLIC
    "${OPEN2OPEN_DIR}/include"
    "${OPEN2OPEN_DIR}/compat"
    ${OPENNURBS_INCLUDE_DIRS}
    ${OpenCASCADE_INCLUDE_DIR}
)

target_compile_definitions(gcv_fcstd_open2open
  PUBLIC
    OPEN2OPEN_HAVE_LIBZIP
)
if(_OCCT_HAVE_XCAF)
  target_compile_definitions(gcv_fcstd_open2open PUBLIC OPEN2OPEN_HAVE_XCAF)
endif()

# Suppress warnings from third-party open2open code to keep BRL-CAD build clean
target_compile_options(gcv_fcstd_open2open PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-w>
)

message(STATUS "gcv-fcstd: FCStd plugin enabled (OCCT ${OpenCASCADE_VERSION})")

# ---------------------------------------------------------------------------
# Build the plugin shared library
# ---------------------------------------------------------------------------
gcv_plugin_library(gcv-fcstd SHARED ${FCSTD_SRCS})

target_include_directories(gcv-fcstd PRIVATE
  $<TARGET_PROPERTY:gcv_fcstd_open2open,INTERFACE_INCLUDE_DIRECTORIES>
  "${FCSTD_LIBZIP_INC}"
)

target_compile_definitions(gcv-fcstd PRIVATE
  $<TARGET_PROPERTY:gcv_fcstd_open2open,INTERFACE_COMPILE_DEFINITIONS>
  BRLCADBUILD HAVE_CONFIG_H
)

target_link_libraries(gcv-fcstd
  librt libwdb libbg
  $<TARGET_OBJECTS:gcv_fcstd_open2open>
  ${OPENNURBS_LIBRARIES}
  ${_OCCT_LINK_LIBS}
  "${FCSTD_LIBZIP_LIB}"
)

validate_style(gcv-fcstd "${FCSTD_SRCS}")
plugin_setup(gcv-fcstd gcv)

cmakefiles(
  ${FCSTD_SRCS}
  CMakeLists.txt
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
