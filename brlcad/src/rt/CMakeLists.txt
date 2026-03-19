if(BRLCAD_ENABLE_OPENCL)
  add_definitions(-DUSE_OPENCL)
endif(BRLCAD_ENABLE_OPENCL)

set(
  LIBRTUIF_SOURCES
  do.c
  grid.c
  heatgraph.c
  main.c
  opt.c
  scanline.c
  usage.cpp
  worker.c
)

brlcad_addlib(librtuif "${LIBRTUIF_SOURCES}" "librt;${M_LIBRARY}" "" "${CMAKE_CURRENT_SOURCE_DIR}" STATIC NO_INSTALL)

if(BRLCAD_ENABLE_OPENCL)
  set(OPENCL_LIBS ${OPENCL_LIBRARIES})
endif(BRLCAD_ENABLE_OPENCL)

set(RT_STD_LIBS Threads::Threads)
if(MPI_C_LIBRARIES AND BRLCAD_ENABLE_MPI)
  set(RT_STD_LIBS ${RT_STD_LIBS} ${MPI_C_LIBRARIES})
endif(MPI_C_LIBRARIES AND BRLCAD_ENABLE_MPI)
set(
  RT_STD_LIBS
  ${RT_STD_LIBS}
  librt
  libnmg
  liboptical
  libicv
  ${M_LIBRARY}
)
if(NOT BRLCAD_ENABLE_OBOL)
  list(APPEND RT_STD_LIBS libdm)
else()
  list(APPEND RT_STD_LIBS libpkg)
  add_definitions(-DBRLCAD_ENABLE_OBOL)
endif()

brlcad_adddata(rtexample.c sample_applications)

if(NOT BRLCAD_ENABLE_OBOL)
  set(RT_DM_DEP libdm)
  set(RT_DM_PLUGINS dm_plugins)
  set(RT_DM_TXT dm-txt)
else()
  set(RT_DM_DEP libpkg)
  set(RT_DM_PLUGINS "")
  set(RT_DM_TXT "")
endif()

brlcad_addexec(rtexample "rtexample.c" "librt;${RT_DM_DEP};liboptical;libicv" NO_INSTALL)

brlcad_addexec(reshoot "reshoot.c" "Threads::Threads;librt;${RT_DM_DEP};liboptical;libicv" FOLDER Rt)
target_include_directories(reshoot BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

brlcad_addexec(rtshot "rtshot.c" "${RT_STD_LIBS}" FOLDER Rt)
target_include_directories(rtshot BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(RT_DM_PLUGINS)
  add_target_deps(rtshot ${RT_DM_PLUGINS})
endif()

brlcad_addexec(rtscale "read-rtlog.c;rtscale.c" "Threads::Threads;librt;${RT_DM_DEP};liboptical;libicv;${M_LIBRARY}" FOLDER Rt)
target_include_directories(rtscale BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(RT_DM_PLUGINS)
  add_target_deps(rtscale ${RT_DM_PLUGINS})
endif()

brlcad_addexec(rt "${LIBRTUIF_SOURCES};view.c" "${OPENCL_LIBS};${RT_STD_LIBS}" FOLDER Rt)
target_include_directories(rt BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(RT_DM_PLUGINS)
  add_target_deps(rt ${RT_DM_PLUGINS} ${RT_DM_TXT})
endif()

brlcad_addexec(rtarea "${LIBRTUIF_SOURCES};viewarea.c" "${RT_STD_LIBS}" FOLDER Rt)
target_include_directories(rtarea BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET rtarea APPEND PROPERTY COMPILE_DEFINITIONS "RT_TXT_OUTPUT")

brlcad_addexec(rtcheck "${LIBRTUIF_SOURCES};viewcheck.c" "${RT_STD_LIBS}" FOLDER Rt)
target_include_directories(rtcheck BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET rtcheck APPEND PROPERTY COMPILE_DEFINITIONS "RT_TXT_OUTPUT")

brlcad_addexec(rtedge "${LIBRTUIF_SOURCES};viewedge.c" "${RT_STD_LIBS}" FOLDER Rt)
target_include_directories(rtedge BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(RT_DM_PLUGINS)
  add_target_deps(rtedge ${RT_DM_PLUGINS})
endif()

brlcad_addexec(rtg3 "${LIBRTUIF_SOURCES};viewg3.c" "${RT_STD_LIBS}" FOLDER Rt)
target_include_directories(rtg3 BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET rtg3 APPEND PROPERTY COMPILE_DEFINITIONS "RT_TXT_OUTPUT")

brlcad_addexec(rthide "${LIBRTUIF_SOURCES};viewhide.c" "${RT_STD_LIBS}" FOLDER Rt)
target_include_directories(rthide BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET rthide APPEND PROPERTY COMPILE_DEFINITIONS "RT_TXT_OUTPUT")

brlcad_addexec(rtsil "${LIBRTUIF_SOURCES};viewsil.c" "${RT_STD_LIBS}" FOLDER Rt)
target_include_directories(rtsil BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

brlcad_addexec(rtsurf "rtsurf.c;rtsurf_hits.cpp" "libanalyze;librt;${RT_DM_DEP};liboptical;libicv")
target_include_directories(rtsurf BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

brlcad_addexec(rtweight "${LIBRTUIF_SOURCES};viewweight.c" "libanalyze;${RT_STD_LIBS}" FOLDER Rt)
target_include_directories(rtweight BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET rtweight APPEND PROPERTY COMPILE_DEFINITIONS "RT_TXT_OUTPUT")

brlcad_addexec(rtxray "${LIBRTUIF_SOURCES};viewxray.c" "${RT_STD_LIBS}" FOLDER Rt)
target_include_directories(rtxray BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(RT_DM_PLUGINS)
  add_target_deps(rtxray ${RT_DM_PLUGINS})
endif()

brlcad_addexec(rtdepth "${LIBRTUIF_SOURCES};viewdepth.c" "${RT_STD_LIBS}" FOLDER Rt)
target_include_directories(rtdepth BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(RT_DM_PLUGINS)
  add_target_deps(rtdepth ${RT_DM_PLUGINS})
endif()

set(
  rt_ignore_files
  CMakeLists.txt
  ext.h
  libfb-dummy.c
  main.c
  rt_fb_pkg.h
  rtuif.h
  rtsurf_hits.h
  scanline.h
  viewdir.c
  viewdummy.c
)
cmakefiles(${rt_ignore_files})

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
