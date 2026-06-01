set(LIBGCV_PLUGINS_DIRECTORY "gcv")
brlcad_find_package(manifold REQUIRED)
add_subdirectory(plugins)

set(
  LIBGCV_SOURCES
  bottess.c
  facetize.c
  facetize_manifold.cpp
  facetize_nmg.cpp
  gcv.c
  region_end.c
  region_end_mc.c
  tri_intersect.c
)

# Note - libgcv_deps is defined by ${BRLCAD_SOURCE_DIR}/src/source_dirs.cmake
brlcad_addlib(libgcv "${LIBGCV_SOURCES}" "" ""
  SHARED_SRCS "${GCV_SO_SRCS}" STATIC_SRCS "${GCV_STATIC_SRCS}"
  PUBLIC_LIBS ${libgcv_deps}
  PRIVATE_LIBS ${M_LIBRARY} manifold::manifold
)
set_target_properties(libgcv PROPERTIES VERSION 20.0.1 SOVERSION 20)
set_property(
  SOURCE gcv.c
  APPEND
  PROPERTY COMPILE_DEFINITIONS "LIBGCV_PLUGINS_DIRECTORY=\"${LIBGCV_PLUGINS_DIRECTORY}\""
)
set_property(
  SOURCE gcv.c
  APPEND
  PROPERTY COMPILE_DEFINITIONS "LIBGCV_PLUGINS_SUFFIX=\"${CMAKE_SHARED_LIBRARY_SUFFIX}\""
)

add_subdirectory(tests)

set(GCV_IGNORE_FILES CMakeLists.txt NOTES soup.h subprocess.h tri_intersect.h facetize_manifold.cpp facetize_nmg.cpp)
cmakefiles(${GCV_IGNORE_FILES})

distclean(${CMAKE_CURRENT_BINARY_DIR}/CTestTestfile.cmake)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
