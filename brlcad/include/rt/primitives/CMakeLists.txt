set(
  primitives_headers
  arb8.h
  annot.h
  ars.h
  bot.h
  brep.h
  cline.h
  dsp.h
  ebm.h
  ehy.h
  ell.h
  epa.h
  eto.h
  extrude.h
  hf.h
  hyp.h
  metaball.h
  nmg.h
  part.h
  pg.h
  pipe.h
  rhc.h
  rpc.h
  script.h
  sketch.h
  superell.h
  tgc.h
  tor.h
  vol.h
)
brlcad_manage_files(primitives_headers ${INCLUDE_DIR}/brlcad/rt/primitives REQUIRED librt)

cmakefiles(CMakeLists.txt)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
