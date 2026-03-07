# libbv — all implementation has moved to libbsg.  This directory is
# retained only so that in-tree files still referenced via cmakefiles()
# are tracked by the distribution system.  The stub library has been
# removed; callers should link against libbsg directly.

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
