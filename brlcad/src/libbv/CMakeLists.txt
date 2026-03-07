# libbv — backward-compatibility stub.
#
# All implementation has moved to libbsg (Step 7 of BSG modernization).
# libbv is now a stub library that links libbsg so that code still
# linking against -lbv continues to resolve symbols.
#
# The bv/ public headers remain in place during the transition; once all
# downstream users have fully migrated to bsg/ headers, bv/ and libbv can
# be deleted entirely.

# Note - libbv_deps is defined by ${BRLCAD_SOURCE_DIR}/src/source_dirs.cmake
brlcad_addlib(libbv "bv_stub.c" "libbsg" "" "")
set_target_properties(libbv PROPERTIES VERSION 20.0.1 SOVERSION 20)

add_subdirectory(tests)

cmakefiles(
  CMakeLists.txt
  TODO
  bv_private.h
  bv_stub.c
  adc.c
  diff.c
  font.c
  hash.c
  knobs.cpp
  lod.cpp
  polygon.c
  polygon_op.cpp
  polygon_fill.cpp
  snap.c
  tig/axis.c
  tig/list.c
  tig/marker.c
  tig/scale.c
  tig/symbol.c
  tig/tplot.c
  tig/vectfont.c
  tig/vector.c
  util.cpp
  vlist.c
  view_sets.cpp
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
