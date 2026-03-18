if(BRLCAD_ENABLE_OBOL)
  # In Obol builds the USE_DM code in gsh.cpp is compiled out, so there is no
  # dependency on libdm or the dm_plugins target.
  brlcad_addexec(gsh "gsh.cpp" "libged;libbu")
  target_compile_definitions(gsh PRIVATE BRLCAD_ENABLE_OBOL)
else()
  brlcad_addexec(gsh "gsh.cpp" "libged;libdm;libbu")
  add_dependencies(gsh dm_plugins ged_plugins)
endif()
target_include_directories(gsh BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../libtermio)

cmakefiles(
  CMakeLists.txt
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
