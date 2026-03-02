brlcad_addexec(rt_edit_test_tor tor.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_ell ell.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_tgc tgc.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_epa epa.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_ehy ehy.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_eto eto.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_hyp hyp.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_rpc rpc.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_rhc rhc.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_part part.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_superell superell.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_cline cline.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_extrude extrude.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_metaball metaball.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_hrt hrt.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_ars ars.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_pipe pipe.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_dsp dsp.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_vol vol.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_ebm ebm.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_sph sph.cpp "librt" TEST)
brlcad_addexec(rt_edit_test_arb8 arb8.cpp "librt" TEST)

set(
  distcheck_files
  CMakeLists.txt
  tor.cpp
  ell.cpp
  tgc.cpp
  epa.cpp
  ehy.cpp
  eto.cpp
  hyp.cpp
  rpc.cpp
  rhc.cpp
  part.cpp
  superell.cpp
  cline.cpp
  extrude.cpp
  metaball.cpp
  hrt.cpp
  ars.cpp
  pipe.cpp
  dsp.cpp
  vol.cpp
  ebm.cpp
  sph.cpp
  arb8.cpp
)

cmakefiles(${distcheck_files})

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
