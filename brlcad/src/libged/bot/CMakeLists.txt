find_package_eigen(REQUIRED)
brlcad_find_package(manifold REQUIRED)

set(BOT_LIBS libged libbg libbu manifold::manifold)

brlcad_find_package(Geogram SYSPATTERN geogram)
# geogram is now compiled as part of libbg; add its source directory FIRST so
# remesh.cpp finds the BRL-CAD geogram headers before any system geogram
# (not part of libbg's installed public API).
set(BOT_LOCAL_INCLUDE_DIRS ${BRLCAD_SOURCE_DIR}/src/libbg ${BOT_LOCAL_INCLUDE_DIRS})

# remesh.cpp includes geogram headers directly and must be compiled with
# BRLCAD_GEOGRAM_EMBED so that GEOBRLCAD_API resolves to the import side
# of libbg's export mechanism.  Per-source COMPILE_DEFINITIONS do not apply
# inside Unity TUs, so remesh.cpp is excluded from Unity batching.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/remesh.cpp
    PROPERTIES
        COMPILE_DEFINITIONS "BRLCAD_GEOGRAM_EMBED;NL_WITH_AMGCL"
        SKIP_UNITY_BUILD_INCLUSION ON
)


if(BRLCAD_ENABLE_OPENVDB)
  set(BOT_LIBS ${BOT_LIBS} ${OpenVDB_LIBRARIES} ${IlmBase_LIBRARIES} ${Tbb_LIBRARIES})
  # disable warnings due to unquellable issues in Boost and IlmBase/half headers
  set_property(SOURCE remesh.cpp APPEND PROPERTY COMPILE_FLAGS "${OpenVDB_DEFINITIONS} -w")
  set(BOT_LOCAL_INCLUDE_DIRS ${BOT_LOCADL_INCLUDE_DIRS} ${OpenVDB_INCLUDE_DIRS} ${Tbb_INCLUDE_DIRS} ${IlmBase_INCLUDE_DIRS})
endif(BRLCAD_ENABLE_OPENVDB)

# TODO - investigate https://github.com/nissmar/VSA to see if it could be
# a viable way to simplify large, complex BoT primitives...

set(
  BOT_SRCS
  bot_condense.c
  bot.cpp
  bot_decimate.c
  bot_exterior.c
  bot_face_fuse.c
  bot_face_sort.c
  bot_flip.c
  bot_fuse.c
  bot_merge.c
  bot_smooth.c
  bot_split.c
  bot_sync.c
  bot_vertex_fuse.c
  dump/bot_dump.cpp
  dump/dxf.c
  dump/gltf.cpp
  dump/obj.c
  dump/sat.c
  dump/stl.c
  edbot.c
  check.cpp
  decimate.cpp
  extrude.cpp
  remesh.cpp
  repair.cpp
  smooth.cpp
  subdivide.cpp
  ../../libbu/fort.c
)

set(
  BOT_LOCAL_INCLUDE_DIRS
  ${EIGEN3_INCLUDE_DIR}
  ${BRLCAD_SOURCE_DIR}/src/libbu # for fort.h
  ${BOT_LOCAL_INCLUDE_DIRS}
)

# altivec vectorization is broken in our bundled version of eigen.
# looks like it was fixed in upstream long time ago, so just disable
# for now with EIGEN_DONT_VECTORIZE.
# TODO: remove this / retest after next Eigen upgrade.
ged_plugin_library(ged-bot
  ${BOT_SRCS}
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  SYSTEM_INCLUDES ${BOT_LOCAL_INCLUDE_DIRS}
  LIBS ${BOT_LIBS}
  DEFINITIONS "EIGEN_DONT_VECTORIZE"
)

cmakefiles(
  CMakeLists.txt
  ${BOT_SRCS}
  dump/ged_bot_dump.h
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
