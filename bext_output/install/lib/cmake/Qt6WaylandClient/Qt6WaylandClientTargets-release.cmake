#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::WaylandClient" for configuration "Release"
set_property(TARGET Qt6::WaylandClient APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(Qt6::WaylandClient PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libQt6WaylandClient.so.6.10.2"
  IMPORTED_SONAME_RELEASE "libQt6WaylandClient.so.6"
  )

list(APPEND _cmake_import_check_targets Qt6::WaylandClient )
list(APPEND _cmake_import_check_files_for_Qt6::WaylandClient "${_IMPORT_PREFIX}/lib/libQt6WaylandClient.so.6.10.2" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
