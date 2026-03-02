# MGED Primitive Editing Migration to librt — Progress

## Overview

This document tracks progress on migrating MGED's primitive editing
functionality to a reusable component in librt.  The goal is to
establish a validated, primitive-by-primitive implementation in librt
so that applications beyond MGED can leverage solid editing logic.

The **ground truth** for correct behavior is the MGED editing code in
`brlcad/src/mged/edsol.c` (and related files).  The `brlcad_mgedrework/`
directory contains an earlier experimental rework that can serve as
additional context, but should not be assumed correct.

---

## Architecture

| File | Role |
|---|---|
| `brlcad/include/rt/edit.h` | Public API: `rt_edit` struct, edit mode flags |
| `brlcad/src/librt/edit.cpp` | Core editing logic: `rt_edit_create/destroy/process` |
| `brlcad/src/librt/primitives/edgeneric.c` | Generic edit operations shared by all primitives |
| `brlcad/src/librt/primitives/edtable.cpp` | Per-primitive function dispatch table (`EDOBJ[]`) |
| `brlcad/src/librt/primitives/<prim>/ed<prim>.c` | Per-primitive editing implementations |
| `brlcad/src/librt/tests/edit/` | Validation tests |

The `rt_edit_process()` call dispatches to the correct primitive's
`ft_edit()` function through `EDOBJ[type].ft_edit`.  Primitive-specific
edit mode flags (e.g. `ECMD_TOR_R1`) are defined in the individual
`ed<prim>.c` files and aggregated into the auto-generated
`rt/rt_ecmds.h` header at build time.

---

## Primitive Status

### TOR — Torus ✅ DONE (with test)

- Source: `src/librt/primitives/tor/edtor.c`
- Test:   `src/librt/tests/edit/tor.cpp`
- Operations validated:
  - `ECMD_TOR_R1` — scale outer radius r_a (es_scale and e_inpara)
  - `ECMD_TOR_R2` — scale tube radius r_h (es_scale and e_inpara)
  - `RT_PARAMS_EDIT_SCALE` — uniform scale (es_scale and e_inpara)
  - `RT_PARAMS_EDIT_TRANS` — translate (mv_context on and off)
  - `RT_PARAMS_EDIT_ROT`   — rotate (about view, eye, model center, keypoint; mv_context on and off)
  - XY mouse-based edits for ECMD_TOR_R1, RT_PARAMS_EDIT_TRANS, RT_PARAMS_EDIT_ROT
- Notes: Furthest along; serves as the template for all other primitives.

### ELL — Ellipsoid ✅ DONE (with test)

- Source: `src/librt/primitives/ell/edell.c`
- Test:   `src/librt/tests/edit/ell.cpp`
- Operations validated:
  - `ECMD_ELL_SCALE_A`   — scale semi-axis A vector (es_scale and e_inpara)
  - `ECMD_ELL_SCALE_B`   — scale semi-axis B vector (es_scale and e_inpara)
  - `ECMD_ELL_SCALE_C`   — scale semi-axis C vector (es_scale and e_inpara)
  - `ECMD_ELL_SCALE_ABC` — scale all semi-axes uniformly (es_scale and e_inpara)
  - `RT_PARAMS_EDIT_SCALE` — uniform scale about vertex
  - `RT_PARAMS_EDIT_TRANS` — translate (mv_context=1)
  - XY mouse-based edit for ECMD_ELL_SCALE_A
- Bug fixed: `EDOBJ[ID_ELL].ft_keypoint` was `NULL` in `edtable.cpp`,
  causing `rt_get_solid_keypoint` to set the edit keypoint to the origin
  instead of the ellipsoid vertex.  Fixed to use `edit_keypoint`.
- TODO: Add `RT_PARAMS_EDIT_ROT` tests (requires computing expected
  rotation values for a,b,c vectors; straightforward but verbose).
  Add XY tests for TRANS and ROT.

### TGC — Truncated General Cone ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/tgc/edtgc.c`
- Test:   *not yet written*
- Edit mode flags defined: `ECMD_TGC_MV_H`, `ECMD_TGC_MV_HH`,
  `ECMD_TGC_MV_H_CD`, `ECMD_TGC_MV_H_V_AB`, `ECMD_TGC_ROT_AB`,
  `ECMD_TGC_ROT_H`, `ECMD_TGC_SCALE_A/B/C/D/AB/CD/ABCD/H/H_CD/H_V/H_V_AB`
- TODO: Write `src/librt/tests/edit/tgc.cpp` following ell/tor pattern.

### ARB8 — Arbitrary Polyhedron (8 vertices) ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/arb8/edarb.c`
- Test:   *not yet written*
- Notes: Most complex primitive; has face/edge/vertex editing as well
  as generic solid editing.  The ARB-specific face equations are
  maintained in the `rt_edit` state (`es_peqn`).

### EPA — Elliptical Paraboloid ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/epa/edepa.c`
- Edit mode flags: `ECMD_EPA_H`, `ECMD_EPA_R1`, `ECMD_EPA_R2`
- TODO: Write test.

### EHY — Elliptical Hyperboloid ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/ehy/edehy.c`
- Edit mode flags: `ECMD_EHY_H`, `ECMD_EHY_R1`, `ECMD_EHY_R2`, `ECMD_EHY_C`
- TODO: Write test.

### ETO — Elliptical Torus ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/eto/edeto.c`
- TODO: Write test.

### HYP — Hyperboloid of One Sheet ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/hyp/edhyp.c`
- TODO: Write test.

### RPC — Right Parabolic Cylinder ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/rpc/edrpc.c`
- TODO: Write test.

### RHC — Right Hyperbolic Cylinder ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/rhc/edrhc.c`
- TODO: Write test.

### PART — Particle ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/part/edpart.c`
- TODO: Write test.

### PIPE ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/pipe/edpipe.c`
- Notes: Complex — has segment-level editing.

### ARS — Arbitrary Faceted Solid ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/ars/edars.c`
- TODO: Write test.

### BOT — Bag of Triangles ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/bot/edbot.c`
- Notes: Complex — has per-vertex, per-face, and thickness editing.

### SUPERELL ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/superell/edsuperell.c`
- TODO: Write test.

### HRT — Heart ⬜ EDIT CODE EXISTS, NO TEST

- Source: implied by `EDOBJ[]` — needs checking.
- TODO: Verify edit code exists, write test.

### DSP — Displacement Map ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/dsp/eddsp.c`

### EBM — Extruded Bitmap ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/ebm/edebm.c`

### CLINE ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/cline/edcline.c`

### METABALL ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/metaball/edmetaball.c`

### EXTRUDE ⬜ EDIT CODE EXISTS, NO TEST

- Source: `src/librt/primitives/extrude/edextrude.c`

### BSPLINE / NMG / GRIP / HALF / ... ⬜ EDIT CODE EXISTS, NO TEST

---

## Known Issues / TODO

### NULL Keypoints in EDOBJ Table

Several primitives in `src/librt/primitives/edtable.cpp` have `NULL`
for the `ft_keypoint` field, which causes the generic scale/rotate
operations to use the origin (0,0,0) as the keypoint instead of the
primitive's natural reference point (typically its vertex).

**Fixed so far:**
- `ID_ELL` (3): changed `NULL` → `EDFUNCTAB_FUNC_KEYPOINT_CAST(edit_keypoint)`

**Still NULL (needs investigation):**
- `ID_JOINT` (23)
- `ID_SUBMODEL` (28)
- `ID_BREP` (37): BREP editing is complex and may be intentional
- `ID_REVOLVE` (40)
- `ID_PNTS` (41)
- `ID_HRT` (43)
- `ID_COMBINATION` (31): combs don't use solid-edit keypoints — intentional
- `ID_BINUNIF` (33), `ID_UNUSED1`, `ID_UNUSED2`: no edit support — intentional

Most of the above that have actual edit functions should receive the
same `edit_keypoint` fix.  Each should be audited before changing.

### Unimplemented XY Rotation

The `edit_generic_xy` and individual primitive `ft_edit_xy` functions
return an error for `RT_PARAMS_EDIT_ROT` with a TODO note.  The
`brlcad_mgedrework/` directory has an experimental implementation
(`edit_mrot`, `edit_mrot_xy`).  This needs careful validation against
MGED's `doevent.c` / `f_knob` / `mged_erot_xyz` code path before
being committed.  The tor test validates the non-XY rotation path
using the knob infrastructure directly (see the RT_PARAMS_EDIT_ROT XY
section in `tor.cpp`).

### RT_MATRIX_EDIT_ROT in edit_generic

`edit_generic` returns an error for `RT_MATRIX_EDIT_ROT`.  This is
the object-edit (oed) rotation path.  Needs implementation following
MGED's matrix edit rotation logic.

### mgedrework Differences

Comparing `brlcad/` and `brlcad_mgedrework/`:
- `src/librt/edit.cpp`: Several improvements in mgedrework (cleaner
  IDLE handling, eaxes/replot/view-update callbacks, matrix edit flags
  in rt_edit_set_edflag).  These should be reviewed and merged if
  correct.
- `src/librt/primitives/edgeneric.c`: mgedrework adds `edit_mrot` and
  `edit_mrot_xy` for XY rotation support and implements the currently-
  unimplemented XY rotation path.  Needs validation.

---

## Session Log

### Session 1 (2026-03-02)
- Explored repo structure and understand migration architecture.
- Configured and built brlcad_build; verified existing tor test passes.
- Fixed `EDOBJ[ID_ELL].ft_keypoint = NULL` bug in `edtable.cpp`.
- Added `src/librt/tests/edit/ell.cpp` with tests for all four
  ell-specific scale operations plus RT_PARAMS_EDIT_SCALE and
  RT_PARAMS_EDIT_TRANS, and an XY scale test.
- All ell and tor tests pass.
- Created this PROGRESS.md.

---

## Suggested Next Steps

1. **Add more primitive tests** — tgc, epa, ehy are next in complexity
   after ell.  Each has relatively simple, analytically-verifiable
   expected values for their primitive-specific ECMD_* operations.
2. **Fix remaining NULL keypoints** — audit ID_REVOLVE (40), ID_PNTS (41),
   ID_HRT (43), ID_JOINT (23), ID_SUBMODEL (28).
3. **Implement XY rotation** — validate and integrate the mgedrework
   `edit_mrot_xy` approach once the rotation math is confirmed correct.
4. **Audit mgedrework edit.cpp changes** — the cleaner IDLE handling
   and callback improvements look correct and should be cherry-picked.
5. **ARB8 deep-dive** — the ARB8 editing is the most complex (face/edge
   equations, multiple sub-modes) and will require careful side-by-side
   comparison with MGED's `edarb.c` and `facedef.c`.
