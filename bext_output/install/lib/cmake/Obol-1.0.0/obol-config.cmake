# Obol CMake package configuration file.
# Provides the Obol::Obol imported target and convenience variables
# for consumers that use find_package(Obol).

set(OBOL_VERSION 1.0.0)

####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was obol-config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

set(OBOL_BUILD_SHARED_LIBS ON)
set(OBOL_BUILD_MAC_FRAMEWORK )

if(NOT OBOL_BUILD_MAC_FRAMEWORK)
  set_and_check(OBOLDIR "${PACKAGE_PREFIX_DIR}")
  set_and_check(Obol_INCLUDE_DIR "${PACKAGE_PREFIX_DIR}/include")
  set_and_check(Obol_LIB_DIR "${PACKAGE_PREFIX_DIR}/lib")

  # optional documentation directory
  if(EXISTS "${PACKAGE_PREFIX_DIR}/share/doc/Obol")
    set(Obol_DOC_DIR "${PACKAGE_PREFIX_DIR}/share/doc/Obol")
  endif()

  # Windows DLL visibility flag for consumers that link manually
  if(WIN32)
    if(OBOL_BUILD_SHARED_LIBS)
      set(Obol_DEFINES -DOBOL_DLL)
    else()
      set(Obol_DEFINES -DOBOL_NOT_DLL)
    endif()
  endif()
endif()

include(CMakeFindDependencyMacro)

# For static builds, pull in the same dependencies that were used at build time
if(NOT OBOL_BUILD_SHARED_LIBS)
  set(_OPENGL_FOUND TRUE)
  if(_OPENGL_FOUND)
    find_dependency(OpenGL)
  endif()
  set(_Threads_FOUND TRUE)
  if(_Threads_FOUND)
    find_dependency(Threads)
  endif()
endif()

# Import the Obol::Obol target (and any per-configuration variants)
include("${CMAKE_CURRENT_LIST_DIR}/obol-export.cmake")

# Modern imported-target alias and convenience variables
set(Obol_LIBRARIES Obol::Obol)
set(Obol_LIBRARY   Obol::Obol)
if(NOT OBOL_BUILD_MAC_FRAMEWORK)
  set(Obol_INCLUDE_DIRS ${Obol_INCLUDE_DIR})
endif()

# Feature flags recorded at build time
set(Obol_HAVE_THREADS 1)
set(Obol_HAVE_SAFETHREAD 1)

set(Obol_FOUND TRUE)
check_required_components(Obol)

