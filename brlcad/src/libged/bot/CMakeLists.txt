find_package_eigen(REQUIRED)
brlcad_find_package(manifold REQUIRED)

set(BOT_LIBS libged libbg libbu manifold::manifold)

brlcad_find_package(Geogram REQUIRED SYSPATTERN geogram)
set(BOT_LOCAL_INCLUDE_DIRS ${BOT_LOCAL_INCLUDE_DIRS} ${Geogram_INCLUDE_DIR})
set(BOT_LIBS ${BOT_LIBS} ${Geogram_LIBRARY})


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

# Tier-2 flood-fill exterior: local OpenVDB detection, independent of the
# global BRLCAD_ENABLE_OPENVDB option.  This lets "bot exterior --flood-fill"
# work on any system with libopenvdb-dev installed even without a full
# OPENVDB-enabled build.
#
# For reuse: the rt_rtip_to_occupancy_grid helper inside
# bot_flood_exterior.cpp is designed for eventual promotion to librt once
# the interface stabilises.
if(NOT TARGET OpenVDB::openvdb)
  find_package(OpenVDB QUIET)
endif()
if(TARGET OpenVDB::openvdb)
  message(STATUS "bot exterior: OpenVDB found - tier-2 flood-fill enabled")
  list(APPEND BOT_SRCS bot_flood_exterior.cpp)

  # Link against the raw .so path rather than the imported target.
  # Using OpenVDB::openvdb directly would propagate its
  # INTERFACE_COMPILE_DEFINITIONS (OPENVDB_ABI_VERSION_NUMBER=10, etc.) to
  # the *entire* libged compilation, which would activate the OpenVDB code
  # paths in remesh.cpp and trigger a GCC LTO false-positive:
  #   -Werror=alloc-size-larger-than triggered by LeafManager's UINT64_MAX
  #   sentinel at link time.
  get_target_property(_openvdb_loc OpenVDB::openvdb IMPORTED_LOCATION)
  set(BOT_LIBS ${BOT_LIBS} ${_openvdb_loc})
  set(BOT_FLOOD_EXTERIOR_DEFS HAVE_OPENVDB_BOT_EXTERIOR)

  # Build per-file flags so that bot_flood_exterior.cpp sees the OpenVDB
  # ABI defines without contaminating the rest of libged.
  # OpenVDB_DEFINITIONS already carries proper flag prefixes (-D where needed),
  # same as how remesh.cpp uses them via COMPILE_FLAGS.
  # Note: we deliberately do NOT add INTERFACE_INCLUDE_DIRECTORIES here;
  # they are standard system paths (e.g. /usr/include) already in the
  # compiler's default search path, and explicitly listing them can break
  # the GCC #include_next chain for <cmath>.
  set(_fef_cflags "-w -fno-lto")
  foreach(_d ${OpenVDB_DEFINITIONS})
    string(APPEND _fef_cflags " ${_d}")
  endforeach()

  # Compile bot_flood_exterior.cpp as an isolated translation unit:
  #  -w          suppress all OpenVDB header warnings
  #  -fno-lto    prevent its template expansions from participating in LTO
  #              (avoids the -Werror=alloc-size-larger-than false positive)
  #  -isystem... OpenVDB and Boost headers
  #  -D...       OpenVDB ABI version number and Boost defines
  # DIRECTORY .. sets the property in the parent (libged) scope where the
  # unity files are generated, so SKIP_UNITY_BUILD_INCLUSION is respected.
  set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/bot_flood_exterior.cpp
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    PROPERTIES
      SKIP_UNITY_BUILD_INCLUSION ON
      COMPILE_FLAGS "${_fef_cflags}"
  )
else()
  message(STATUS "bot exterior: OpenVDB not found - tier-2 flood-fill disabled (install libopenvdb-dev to enable)")
  cmakefiles(bot_flood_exterior.cpp)
endif()

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
  DEFINITIONS "EIGEN_DONT_VECTORIZE" ${BOT_FLOOD_EXTERIOR_DEFS}
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
