brlcad_find_package(manifold REQUIRED)
set(TESS_LIBS libged libgcv manifold::manifold)

set(
  GCV_FACETIZE_PROCESS_SRCS
  brep_csg.cpp
  continuation.cpp
  sample.cpp
  spsr.cpp
  main.cpp
  nmg.cpp
  util.cpp
)

ged_subprocess_library(facetize_process
  ${GCV_FACETIZE_PROCESS_SRCS}
  INCLUDES ${BRLCAD_SOURCE_DIR}/src/libged/include
  LIBS ${TESS_LIBS}
)

cmakefiles(
  CMakeLists.txt
  tessellate.h
  ${GCV_FACETIZE_PROCESS_SRCS}
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
