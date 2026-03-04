cmake_minimum_required(VERSION 3.0...3.31)

# Have a look at VTKConfig.cmake.in, especially regarding UseVTK.cmake
# https://github.com/Kitware/VTK/blob/master/CMake/VTKConfig.cmake.in
# https://github.com/Kitware/VTK/blob/master/CMake/UseVTK.cmake
# Usage example:
# https://github.com/LuaDist/cmake/blob/master/Modules/FindVTK.cmake
# Bastiaan Veelo 2016.03.15

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

set(OBOL_BUILD_MAC_FRAMEWORK )
if(NOT OBOL_BUILD_MAC_FRAMEWORK)

set_and_check(OBOLDIR "${PACKAGE_PREFIX_DIR}")
set_and_check(Obol_INCLUDE_DIR "${PACKAGE_PREFIX_DIR}/include")
set_and_check(Obol_LIB_DIR "${PACKAGE_PREFIX_DIR}/lib")

# optional directory from documentation component
if (EXISTS "${PACKAGE_PREFIX_DIR}/share/doc/Obol/")
  set(Obol_DOC_DIR     "${PACKAGE_PREFIX_DIR}/share/doc/Obol")
endif()

set(OBOL_NAME Obol)

set(OBOL_BUILD_SHARED_LIBS ON)
if(WIN32)
	if (OBOL_BUILD_SHARED_LIBS)
		SET(Obol_DEFINES -DOBOL_DLL)
	else()
		SET(Obol_DEFINES -DOBOL_NOT_DLL)
	endif()
endif()

set(Obol_CONFIGURATION_TYPES )
set(Obol_BUILD_TYPE Release)
foreach(configuration IN LISTS Obol_CONFIGURATION_TYPES)
	if(configuration STREQUAL "Debug")
		set(Obol_LIBRARY_DEBUG ${OBOL_NAME})
	else()
		set(Obol_LIBRARY_RELEASE ${OBOL_NAME})
	endif()
endforeach()

if(NOT Obol_CONFIGURATION_TYPES)
	if(Obol_BUILD_TYPE STREQUAL "Debug")
		set(Obol_LIBRARY_RELEASE ${OBOL_NAME})
	else()
		set(Obol_LIBRARY_RELEASE ${OBOL_NAME})
	endif()
endif()

if(Obol_LIBRARY_RELEASE AND NOT Obol_LIBRARY_DEBUG)
	set(Obol_LIBRARY_DEBUG   ${Obol_LIBRARY_RELEASE})
	set(Obol_LIBRARY         ${Obol_LIBRARY_RELEASE})
	set(Obol_LIBRARIES       ${Obol_LIBRARY_RELEASE})
endif()

if(Obol_LIBRARY_DEBUG AND NOT Obol_LIBRARY_RELEASE)
	set(Obol_LIBRARY_RELEASE ${Obol_LIBRARY_DEBUG})
	set(Obol_LIBRARY         ${Obol_LIBRARY_DEBUG})
	set(Obol_LIBRARIES       ${Obol_LIBRARY_DEBUG})
endif()

if(Obol_LIBRARY_DEBUG AND Obol_LIBRARY_RELEASE)
	if(CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE)
		# If the generator supports configuration types then set
		# optimized and debug libraries, or if the CMAKE_BUILD_TYPE has a value
		SET(Obol_LIBRARY optimized ${Obol_LIBRARY_RELEASE} debug ${Obol_LIBRARY_DEBUG})
	else()
		# If there are no configuration types and CMAKE_BUILD_TYPE has no value
		# then just use the release libraries
		SET(Obol_LIBRARY ${Obol_LIBRARY_RELEASE})
	endif()
	set(Obol_LIBRARIES optimized ${Obol_LIBRARY_RELEASE} debug ${Obol_LIBRARY_DEBUG})
endif()

set(Obol_LIBRARY ${Obol_LIBRARY} CACHE FILEPATH "The Obol library")
mark_as_advanced(Obol_LIBRARY_RELEASE Obol_LIBRARY_DEBUG)

endif()
include(CMakeFindDependencyMacro)

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

include("${CMAKE_CURRENT_LIST_DIR}/obol-export.cmake")

# export feature info
set(Obol_HAVE_THREADS 1)
set(Obol_HAVE_SAFETHREAD 1)

