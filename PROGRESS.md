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

## Agent Instructions — General Principles

**Follow these principles in every session.  Do not skip them.**

### 1. Test expected values must come from MGED, not librt

Test expected values must be derived from what the MGED editing code
in `brlcad/src/mged/edsol.c` actually does — **not** from running the
librt implementation and recording its output.  The librt
implementation may have bugs; the tests are meant to catch them.

Workflow for establishing expected values:
1. Read the relevant MGED case in `edsol.c` (search for the
   `ECMD_<PRIM>_*` case label).
2. Trace the math by hand or write a small probe program in
   `src/librt/tests/edit/` (named `probe_<prim>.cpp`) that explicitly
   reproduces the MGED computation step-by-step and prints results.
3. Compare the probe output to the librt implementation's output.
4. If they agree — use those values in the test.
5. If they disagree — identify the root cause (see §2 below).
6. Delete the probe file before committing (it is a disposable tool).

### 2. Any delta between MGED and librt must be justified

If librt produces a different result from MGED:
- If MGED has an **actual bug** (e.g. undefined-behavior aliasing,
  physically impossible result, clearly wrong formula), librt **should
  fix the bug** and the test expected values should reflect the
  **correct** behavior.  Document the bug and the fix explicitly.
- If the difference is **intentional redesign** (e.g. librt cleans up
  an awkward API, or does something provably more accurate), document
  the reason clearly.
- If the difference is **unexplained**, it is a librt bug.  Fix librt
  to match MGED; do not adjust the expected values to match the broken
  librt output.

### 3. MAT4X3VEC / MAT4X3PNT aliasing and perspective division

`MAT4X3VEC(o, mat, i)` expands to sequential scalar assignments **with
perspective division by `1/mat[15]`**.  For a plain rotation matrix
`mat[15]=1` so the division is a no-op and the result is just the
upper-3x3 applied.  For `bn_mat_scale_about_pnt(mat, pt, s)`, which
encodes the scale factor as `mat[15] = 1/s` (not in the upper 3x3),
`MAT4X3VEC` divides by `1/s`, effectively scaling by `s`.  The same is
true for `MAT4X3PNT`.

Consequence: a uniform scale via `edit_sscale` scales **all** vector
and scalar quantities (H, a, b, c, r1, r2, ...) by the scale factor.
The origin/vertex (keypoint) stays fixed because `MAT4X3PNT` produces
the same perspective-corrected result for the keypoint.

**Aliasing bug:** When `o` and `i` are the **same pointer** (e.g. both
equal `tgc->h`), the first component assignment overwrites a value still
needed for later ones — producing a silently wrong result.  Always use
a temporary vect_t when the output and input could alias:

```c
/* WRONG — tgc->h[X] gets overwritten before tgc->h[Y] is read */
MAT4X3VEC(tgc->h, mat, tgc->h);

/* CORRECT */
vect_t h_tmp;
VMOVE(h_tmp, tgc->h);
MAT4X3VEC(tgc->h, mat, h_tmp);
```

The same rule applies to `MAT4X3PNT`.

When reviewing a new primitive's edit code, check every `MAT4X3VEC`
and `MAT4X3PNT` call for this pattern.  The generic helpers
`rt_tgc_mat`, `rt_ell_mat`, `rt_tor_mat`, `rt_epa_mat`, `rt_ehy_mat`
and others in the `ft_mat` table already copy to temporaries and are
safe; the aliasing risk is highest in hand-coded `ecmd_*` rotation
functions that directly manipulate the primitive struct fields in-place.

### 4. How to run existing tests

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
# Configure (only needed once, ~55 s)
mkdir -p ${REPO_ROOT}/brlcad_build
cmake -S "${REPO_ROOT}/brlcad" -B "${REPO_ROOT}/brlcad_build" \
  -DBRLCAD_EXT_DIR="${REPO_ROOT}/bext_output" \
  -DBRLCAD_EXTRADOCS=OFF -DBRLCAD_ENABLE_STEP=OFF \
  -DBRLCAD_ENABLE_GDAL=OFF -DBRLCAD_ENABLE_QT=OFF
# Build and run a specific test target
cmake --build ${REPO_ROOT}/brlcad_build --target rt_edit_test_tgc -j$(nproc)
${REPO_ROOT}/brlcad_build/src/librt/tests/edit/rt_edit_test_tgc
```

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
- Aliasing investigation: `rt_tor_mat` (called by `edit_srot` via `ft_mat`)
  copies all vectors to temporaries before `MAT4X3VEC` — no aliasing bug.
  MGED uses `transform_editing_solid` → `rt_generic_xform` (export+reimport),
  which is also safe.  No aliasing fix needed for TOR.
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
  - `RT_PARAMS_EDIT_ROT`   — rotate (about view, eye, model center, keypoint;
    mv_context=1 and mv_context=0)
  - XY mouse-based edits: ECMD_ELL_SCALE_A, TRANS, ROT error path
- Aliasing investigation: `rt_ell_mat` (called by `edit_srot` via `ft_mat`)
  copies all vectors to temporaries before `MAT4X3VEC` — no aliasing bug.
  MGED uses `transform_editing_solid` → `rt_generic_xform` (export+reimport),
  which is also safe.  No aliasing fix needed for ELL.
- Bug fixed: `EDOBJ[ID_ELL].ft_keypoint` was `NULL` in `edtable.cpp`,
  causing `rt_get_solid_keypoint` to set the edit keypoint to the origin
  instead of the ellipsoid vertex.  Fixed to use `edit_keypoint`.

### TGC — Truncated General Cone ✅ DONE (with test)

- Source: `src/librt/primitives/tgc/edtgc.c`
- Test:   `src/librt/tests/edit/tgc.cpp`
- Operations validated:
  - `ECMD_TGC_SCALE_H`       — scale height vector (es_scale and e_inpara)
  - `ECMD_TGC_SCALE_H_V`     — scale H moving V (vertex end)
  - `ECMD_TGC_SCALE_H_CD`    — scale H, also scale CD vectors
  - `ECMD_TGC_SCALE_H_V_AB`  — scale H moving V, also scale AB vectors
  - `ECMD_TGC_SCALE_A/B/C/D` — scale individual semi-axis vectors
  - `ECMD_TGC_SCALE_AB`      — scale A and B together
  - `ECMD_TGC_SCALE_CD`      — scale C and D together
  - `ECMD_TGC_SCALE_ABCD`    — scale all four together
  - `ECMD_TGC_MV_HH`         — move end H (scale H in place)
  - `ECMD_TGC_ROT_H`         — rotate height vector
  - `ECMD_TGC_ROT_AB`        — rotate A/B/C/D vectors
  - `RT_PARAMS_EDIT_SCALE`   — uniform scale
  - `RT_PARAMS_EDIT_TRANS`   — translate
  - `RT_PARAMS_EDIT_ROT`     — rotate about keypoint (mv_context=1)
  - XY mouse-based edits: scale H, translate, move H end, ROT error path
- Bug fixed: `MAT4X3VEC(tgc->h, mat, tgc->h)` aliasing in
  `ecmd_tgc_rot_h` and `ecmd_tgc_rot_ab`.  MGED's `edsol.c` has the
  identical bug.  The fix is justified because a pure rotation must
  preserve vector magnitudes; the aliased macro does not (it produces
  ~1.5% magnitude loss per call).  The fix uses a temporary `vect_t`
  before assigning back (see §3 in Agent Instructions above).
  Expected values in `tgc.cpp` reflect the corrected, alias-free output.

### ARB8 — Arbitrary Polyhedron (8 vertices) ✅ DONE (with test)

- Source: `src/librt/primitives/arb8/edarb.c`
- Test:   `src/librt/tests/edit/arb8.cpp`
- Operations validated:
  - `RT_PARAMS_EDIT_SCALE` — uniform scale of all 8 vertices about keypoint
  - `RT_PARAMS_EDIT_TRANS` — translate all 8 vertices
  - `RT_PARAMS_EDIT_ROT` — rotate all 8 vertices about keypoint
  - `ECMD_ARB_MOVE_FACE` — move a face to pass through a new point
    (bottom face 1234, edit_menu=0, expected: pt[0]-pt[3] move to z=0.5)
  - `EARB` — move an edge to a new position
    (edge 12, edit_menu=0, expected: pt[0]=(0,0,0.5), pt[1]=(1,0,0.5))
  - XY translate, RT_PARAMS_EDIT_ROT XY error path, RT_MATRIX_EDIT_ROT/TRANS
- Notes: `rt_edit_arb_set_edit_mode` is a no-op (TODO); ARB-specific ops are
  triggered by setting `edit_flag` and `a->edit_menu` directly before calling
  `rt_edit_process`.  `ecmd_arb_setup_rotface` requires an interactive callback
  (fixv selection) so it is not tested here.

### EPA — Elliptical Paraboloid ✅ DONE (with test)

- Source: `src/librt/primitives/epa/edepa.c`
- Test:   `src/librt/tests/edit/epa.cpp`
- Operations validated:
  - `ECMD_EPA_H`   — scale height vector H (es_scale and e_inpara)
  - `ECMD_EPA_R1`  — scale semi-major radius (allowed when r1*s >= r2)
  - `ECMD_EPA_R2`  — scale semi-minor radius (allowed when r2*s <= r1)
  - `RT_PARAMS_EDIT_SCALE` — uniform scale about vertex
  - `RT_PARAMS_EDIT_TRANS` — translate
  - `RT_PARAMS_EDIT_ROT`   — rotate about keypoint (mv_context=1)
  - XY mouse-based edits: ECMD_EPA_H, RT_PARAMS_EDIT_TRANS (verify changed),
    RT_PARAMS_EDIT_ROT error path
- Aliasing investigation: `rt_epa_mat` copies all vectors to temporaries —
  no aliasing bug.
- MAT4X3VEC note: includes perspective division by `1/mat[15]`, so
  **all** vector quantities (including H) scale under `RT_PARAMS_EDIT_SCALE`.
  r1/r2 scale via `r1/mat[15] = r1*scale`.  This matches MGED behaviour.

### EHY — Elliptical Hyperboloid ✅ DONE (with test)

- Source: `src/librt/primitives/ehy/edehy.c`
- Test:   `src/librt/tests/edit/ehy.cpp`
- Operations validated:
  - `ECMD_EHY_H`   — scale height vector H (es_scale and e_inpara)
  - `ECMD_EHY_R1`  — scale semi-major radius (allowed when r1*s >= r2)
  - `ECMD_EHY_R2`  — scale semi-minor radius (allowed when r2*s <= r1)
  - `ECMD_EHY_C`   — scale distance to asymptotic cone (es_scale and e_inpara)
  - `RT_PARAMS_EDIT_SCALE` — uniform scale about vertex
  - `RT_PARAMS_EDIT_TRANS` — translate
  - `RT_PARAMS_EDIT_ROT`   — rotate about keypoint (mv_context=1)
  - XY mouse-based edits: ECMD_EHY_H, RT_PARAMS_EDIT_TRANS (verify changed),
    RT_PARAMS_EDIT_ROT error path
- Aliasing investigation: `rt_ehy_mat` copies all vectors to temporaries —
  no aliasing bug.
- MAT4X3VEC note: same as EPA — H and Au scale under RT_PARAMS_EDIT_SCALE;
  r1/r2/c scale via `r/mat[15]`. This matches MGED behaviour.

### ETO — Elliptical Torus ✅ DONE (with test + aliasing fix)

- Source: `src/librt/primitives/eto/edeto.c`
- Test:   `src/librt/tests/edit/eto.cpp`
- **Bug fixed:** `ecmd_eto_rot_c` used `MAT4X3VEC(eto_C, mat, eto_C)` (aliasing);
  fixed with a temporary vect_t.  MGED has the same bug.
- Operations validated: ECMD_ETO_R, ECMD_ETO_RD, ECMD_ETO_SCALE_C,
  ECMD_ETO_ROT_C, RT_PARAMS_EDIT_SCALE/TRANS/ROT, XY scale/trans/rot-error

### HYP — Hyperboloid of One Sheet ✅ DONE (with test + aliasing fix)

- Source: `src/librt/primitives/hyp/edhyp.c`
- Test:   `src/librt/tests/edit/hyp.cpp`
- **Bug fixed:** `ecmd_hyp_rot_h` used `MAT4X3VEC(hyp_Hi, mat, hyp_Hi)` (aliasing);
  fixed with a temporary vect_t.  MGED has the same bug.
- Note: rt_hyp_mat does NOT update hyp_bnr; it is unchanged by uniform scale/rotation.
- Operations validated: ECMD_HYP_H, ECMD_HYP_SCALE_A, ECMD_HYP_SCALE_B,
  ECMD_HYP_C, ECMD_HYP_ROT_H, RT_PARAMS_EDIT_SCALE/TRANS/ROT, XY scale/trans/rot-error

### RPC — Right Parabolic Cylinder ✅ DONE (with test)

- Source: `src/librt/primitives/rpc/edrpc.c`
- Test:   `src/librt/tests/edit/rpc.cpp`
- No aliasing bugs (no MAT4X3VEC in edrpc.c).
- Operations validated: ECMD_RPC_B, ECMD_RPC_H, ECMD_RPC_R,
  RT_PARAMS_EDIT_SCALE/TRANS/ROT, XY scale/trans/rot-error

### RHC — Right Hyperbolic Cylinder ✅ DONE (with test)

- Source: `src/librt/primitives/rhc/edrhc.c`
- Test:   `src/librt/tests/edit/rhc.cpp`
- No aliasing bugs (no MAT4X3VEC in edrhc.c).
- Operations validated: ECMD_RHC_B, ECMD_RHC_H, ECMD_RHC_R, ECMD_RHC_C,
  RT_PARAMS_EDIT_SCALE/TRANS/ROT, XY scale/trans/rot-error

### PART — Particle ✅ DONE (with test)

- Source: `src/librt/primitives/part/edpart.c`
- Test:   `src/librt/tests/edit/part.cpp`
- **Scanner fix:** renamed `ECMD_PART_v`→`ECMD_PART_VRAD` and
  `ECMD_PART_h`→`ECMD_PART_HRAD` so the rt_ecmd_scanner regex
  `ECMD_[0-9A-Z_]+` can pick them up and expose them in `rt_ecmds.h`.
- **Build system fix:** added `${RT_PFILE_IN}` to the scanner's
  `add_custom_command DEPENDS` list so that changes to any primitive
  edit file trigger a re-scan of ECMD definitions.
- No aliasing bugs (no MAT4X3VEC in edpart.c).
- Operations validated: ECMD_PART_H, ECMD_PART_VRAD, ECMD_PART_HRAD,
  RT_PARAMS_EDIT_SCALE/TRANS/ROT, XY scale/trans/rot-error

### PIPE ✅ DONE (with test)

- Source: `src/librt/primitives/pipe/edpipe.c`
- Test:   `src/librt/tests/edit/pipe.cpp`
- Operations validated:
  - `RT_PARAMS_EDIT_SCALE` — uniform scale; all coords/OD/ID/bendradius scale
  - `RT_PARAMS_EDIT_TRANS` — translate all points
  - `RT_PARAMS_EDIT_ROT` — rotate all points about keypoint
  - `ECMD_PIPE_SCALE_OD` — scale outer diameter of all pipe points
  - `ECMD_PIPE_SCALE_ID` — scale inner diameter of all pipe points
  - `ECMD_PIPE_PT_MOVE` — move a selected point to a new position
  - `ECMD_PIPE_PT_ADD` — append a new pipe point at the end (e_inpara=3)
  - `ECMD_PIPE_PT_DEL` — delete the currently selected pipe point
  - XY translate, RT_PARAMS_EDIT_ROT XY error path, RT_MATRIX_EDIT_ROT/TRANS
- Notes: `ECMD_PIPE_PT_DEL` must be triggered via `rt_edit_set_edflag` + 
  `rt_edit_process` (not `ft_set_edit_mode`) to avoid double-processing;
  `ft_set_edit_mode(ECMD_PIPE_PT_DEL)` internally calls `rt_edit_process`.

### ARS — Arbitrary Faceted Solid ✅ DONE (with test)

- Source: `src/librt/primitives/ars/edars.c`
- Test:   `src/librt/tests/edit/ars.cpp`
- Operations validated:
  - `RT_PARAMS_EDIT_SCALE/TRANS/ROT` — generic operations
  - `ECMD_ARS_MOVE_PT` — move a selected curve point to e_para coords
  - `ECMD_ARS_SCALE_CRV` — scale selected curve about its centroid
  - `ECMD_ARS_SCALE_COL` — scale selected column about its cross-curve centroid
  - `ECMD_ARS_INSERT_CRV` — insert an interpolated curve after selected curve
  - XY scale, translate, ROT error path, RT_MATRIX ops

### BOT — Bag of Triangles ✅ DONE (with test)

- Source: `src/librt/primitives/bot/edbot.c`
- Test:   `src/librt/tests/edit/bot.cpp`
- Operations validated:
  - `RT_PARAMS_EDIT_SCALE/TRANS/ROT` — generic operations
  - `ECMD_BOT_MOVEV` — move a selected vertex
  - `ECMD_BOT_MOVEV_LIST` — move a list of vertices by delta
  - `ECMD_BOT_ESPLIT` — split an edge at its midpoint
  - `ECMD_BOT_FSPLIT` — split a face at its centroid
  - `ECMD_BOT_VERTEX_FUSE` — deduplicate vertices within tolerance
  - `ECMD_BOT_FACE_FUSE` — remove duplicate faces
  - XY translate, ROT error path, RT_MATRIX ops

### SUPERELL ✅ DONE (with test)

- Source: `src/librt/primitives/superell/edsuperell.c`
- Test:   `src/librt/tests/edit/superell.cpp`
- No aliasing bugs; rt_superell_mat copies to temporaries before MAT4X3VEC.
- Note: `n` and `e` exponents are NOT in rt_superell_mat and are
  unchanged by RT_PARAMS_EDIT_SCALE and RT_PARAMS_EDIT_ROT.
- ECMD_SUPERELL_SCALE_ABC sets all three axes to the same length.
- Operations validated: ECMD_SUPERELL_SCALE_A/B/C/ABC,
  RT_PARAMS_EDIT_SCALE/TRANS/ROT, XY scale/trans/rot-error

### HRT — Heart ✅ DONE (with test)

- Source: `src/librt/primitives/hrt/edhrt.c`
- Test:   `src/librt/tests/edit/hrt.cpp`
- Operations validated:
  - `RT_PARAMS_EDIT_SCALE` — uniform scale; xdir, ydir, zdir vectors scale
  - `RT_PARAMS_EDIT_TRANS` — translate vertex v
  - `RT_PARAMS_EDIT_ROT` — rotate all direction vectors about keypoint
  - XY scale and translate, ROT XY error path, RT_MATRIX ops
- Notes: HRT has no `ft_set_edit_mode` (NULL); edit flags set directly.
  `d` field (distance) is not affected by RT_PARAMS_EDIT_SCALE at present.

### DSP — Displacement Map ✅ DONE (with test)

- Source: `src/librt/primitives/dsp/eddsp.c`
- Test:   `src/librt/tests/edit/dsp.cpp`
- **Bugs fixed:** `ecmd_dsp_scale_x/y/alt` had the same strict
  `e_inpara != 1` guard as CLINE; fixed to allow the es_scale path
  when e_inpara==0. The inner `dsp_scale` function already handled
  the es_scale path correctly.
- **New ECMDs added:**
  - `ECMD_DSP_SET_SMOOTH` (25061) — set `dsp_smooth` flag (0/1)
  - `ECMD_DSP_SET_DATASRC` (25062) — switch `dsp_datasrc` between
    `RT_DSP_SRC_FILE` and `RT_DSP_SRC_OBJ`
- Operations validated: ECMD_DSP_SCALE_X/Y/ALT (keyboard + es_scale),
  RT_PARAMS_EDIT_SCALE/TRANS/ROT, XY error path, RT_MATRIX ops,
  ECMD_DSP_SET_SMOOTH, ECMD_DSP_SET_DATASRC

### EBM — Extruded Bitmap ✅ DONE (with test)

- Source: `src/librt/primitives/ebm/edebm.c`
- Test:   `src/librt/tests/edit/ebm.cpp`
- **Bug fixed:** `ecmd_ebm_height` had the same strict `e_inpara != 1`
  guard as CLINE; fixed to allow the es_scale path when e_inpara==0.
- Operations validated: ECMD_EBM_HEIGHT (keyboard + es_scale),
  RT_PARAMS_EDIT_SCALE/TRANS, RT_MATRIX ops, ROT XY error path
- Notes: Test creates a minimal EBM data file at runtime.
  EBM tallness parameter is validated for both keyboard and mouse paths.

### CLINE ✅ DONE (with test + bug fixes)

- Source: `src/librt/primitives/cline/edcline.c`
- Test:   `src/librt/tests/edit/cline.cpp`
- **Bug fixed in edcline.c:** `ecmd_cline_scale_h`, `ecmd_cline_scale_r`,
  and `ecmd_cline_scale_t` had a strict `e_inpara != 1` guard that
  unconditionally returned BRLCAD_ERROR when called with e_inpara=0,
  blocking the XY mouse-driven path (which uses es_scale with e_inpara=0).
  Fixed to allow the es_scale path when e_inpara==0 and es_scale>0.
- **Bug fixed in edgeneric.c (`edit_sscale`):** `edit_sscale` did not
  guard against es_scale==0 (the initial/reset state), calling
  `bn_mat_scale_about_pnt(mat, keypoint, 0.0)` which produces a
  singular matrix (mat[15]=0). This was latent in most primitives
  because their ft_set_edit_mode does not call rt_edit_process;
  CLINE's ft_set_edit_mode calls rt_edit_process unconditionally,
  exposing the bug. Fix: added `if (!s->e_inpara && s->es_scale < SMALL_FASTF) return 0;`
  guard before the `bn_mat_scale_about_pnt` call.
- Operations validated: ECMD_CLINE_SCALE_H/R/T, ECMD_CLINE_MOVE_H,
  RT_PARAMS_EDIT_SCALE/TRANS/ROT, XY scale-H/trans/rot-error

### METABALL ✅ DONE (with test)

- Source: `src/librt/primitives/metaball/edmetaball.c`
- Test:   `src/librt/tests/edit/metaball.cpp`
- Operations validated:
  - `ECMD_METABALL_SET_THRESHOLD` — set global threshold value
  - `ECMD_METABALL_SET_METHOD` — set interpolation method
  - `ECMD_METABALL_PT_MOV` — move selected point by delta
  - `ECMD_METABALL_PT_ADD` — add a new control point
  - `ECMD_METABALL_PT_DEL` — delete selected control point
  - `RT_PARAMS_EDIT_SCALE/TRANS/ROT` — generic operations (all points scaled/moved)
  - XY translate, ROT XY error path, RT_MATRIX ops

### EXTRUDE ✅ DONE (with test)

- Source: `src/librt/primitives/extrude/edextrude.c`
- Test:   `src/librt/tests/edit/extrude.cpp`
- Operations validated:
  - `ECMD_EXTR_SCALE_H` — scale height vector (keyboard + es_scale restore)
  - `ECMD_EXTR_MOV_H` — move h endpoint via mv_context
  - `ECMD_EXTR_ROT_H` — rotate h about V
  - `ECMD_EXTR_SCALE_A` — scale A (sketch u_vec) reference vector
  - `ECMD_EXTR_SCALE_B` — scale B (sketch v_vec) reference vector
  - `ECMD_EXTR_ROT_A` — rotate A reference vector
  - `ECMD_EXTR_ROT_B` — rotate B reference vector
  - `RT_PARAMS_EDIT_SCALE/TRANS/ROT` — generic operations
  - XY scale-H, translate, ROT XY error path, RT_MATRIX ops

### BSPLINE / NMG / SKETCH / COMB ✅ DONE (with tests)

- **BSPLINE** (`edbspline.c`, test `bspline.cpp`):
  - `ECMD_BSPLINE_PICK_KNOT` — select knot by distance from view center
  - `ECMD_BSPLINE_SET_KNOT` — set selected knot value
  - RT_MATRIX_EDIT_ROT/TRANS

- **NMG** (`ednmg.c`, test `nmg.cpp`):
  - `ECMD_NMG_VPICK` — pick vertex by index
  - `ECMD_NMG_VMOVE` — move selected vertex to new XYZ coords
  - `ECMD_NMG_FPICK` — pick faceuse by index
  - `ECMD_NMG_FMOVE` — translate all face vertices by delta
  - `ECMD_NMG_LEXTRU_DIR` — extrude loop in explicit direction+distance
  - RT_MATRIX_EDIT_ROT/TRANS

- **SKETCH** (`edsketch.c`, test `sketch.cpp`):
  - `ECMD_SKETCH_PICK_VERTEX`, `ECMD_SKETCH_MOVE_VERTEX`
  - `ECMD_SKETCH_PICK_SEGMENT`, `ECMD_SKETCH_MOVE_SEGMENT`
  - `ECMD_SKETCH_APPEND_LINE`, `ECMD_SKETCH_APPEND_ARC`
  - `ECMD_SKETCH_APPEND_BEZIER` (degree 2 and 3)
  - `ECMD_SKETCH_DELETE_VERTEX`, `ECMD_SKETCH_DELETE_SEGMENT`
  - `ECMD_SKETCH_MOVE_VERTEX_LIST` — move multiple vertices by UV delta
  - RT_MATRIX ops

- **COMB** (`edcomb.c`, test `comb.cpp`):
  - `ECMD_COMB_ADD_MEMBER` — add a subtree leaf with given op
  - `ECMD_COMB_DEL_MEMBER` — delete a leaf by index
  - `ECMD_COMB_SET_OP` — change the boolean op of a leaf
  - `ECMD_COMB_SET_MATRIX` — set the placement matrix of a leaf

### GRIP / HALF / VOL / SPH / Other primitives ✅ DONE (with tests)

- **SPH** (`test sph.cpp`): Generic RT_PARAMS_EDIT_SCALE/TRANS/ROT (ELL path),
  XY scale path verified.
- **VOL** (`test vol.cpp`): ECMD_VOL_CSIZE, ECMD_VOL_THRESH_LO/HI,
  RT_PARAMS_EDIT_SCALE/TRANS, RT_MATRIX ops.
- **EBM** (`test ebm.cpp`): ECMD_EBM_HEIGHT (keyboard + es_scale),
  RT_PARAMS_EDIT_SCALE/TRANS, RT_MATRIX ops.
- **DSP** (`test dsp.cpp`): ECMD_DSP_SCALE_X/Y/ALT, ECMD_DSP_SET_SMOOTH,
  ECMD_DSP_SET_DATASRC, RT_PARAMS_EDIT_SCALE/TRANS, RT_MATRIX ops.

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

### Session 2 (2026-03-02)
- Added full TGC test (`src/librt/tests/edit/tgc.cpp`).
- Discovered and fixed `MAT4X3VEC` aliasing bug in `ecmd_tgc_rot_h`
  and `ecmd_tgc_rot_ab` (`src/librt/primitives/tgc/edtgc.c`).
  Root-cause analysis via a probe program (`probe_tgc.cpp`): the
  macro `MAT4X3VEC(tgc->h, mat, tgc->h)` silently corrupted
  the height vector because the output and input were the same pointer.
  MGED (`edsol.c`) has the identical bug.  Fix justified: pure rotation
  must preserve magnitude; the alias does not.  Probe deleted after use.
- Completed ELL test coverage: added `RT_PARAMS_EDIT_ROT` (four pivot
  modes + mv_context=0), XY translation, and XY rotation error path.
- Updated PROGRESS.md with general principles (§ Agent Instructions).

### Session 4 (2026-03-02)
- Wrote `src/librt/tests/edit/epa.cpp` — full EPA test coverage
- Wrote `src/librt/tests/edit/ehy.cpp` — full EHY test coverage
- Updated `CMakeLists.txt` for epa/ehy tests
- Discovered and corrected understanding of `MAT4X3VEC`: it includes perspective
  division by `1/mat[15]`, so uniform scale (mat[15]=1/s) causes ALL vector
  quantities to scale.  Updated PROGRESS.md §3 Agent Instructions.
- Confirmed rt_epa_mat and rt_ehy_mat are alias-safe.
- All 5 tests pass (tor, ell, tgc, epa, ehy).
- Fixed `ecmd_eto_rot_c` (edeto.c) and `ecmd_hyp_rot_h` (edhyp.c): same
  MAT4X3VEC aliasing bug as TGC; fixed using temporary vect_t.
- Wrote `src/librt/tests/edit/eto.cpp` — full ETO test coverage (including ROT_C)
- Wrote `src/librt/tests/edit/hyp.cpp` — full HYP test coverage (including ROT_H)
- Updated `CMakeLists.txt` for eto/hyp tests
- All 7 tests pass (tor, ell, tgc, epa, ehy, eto, hyp).
- Investigated TOR and ELL for the same `MAT4X3VEC` aliasing issue.
- **Result: no aliasing bug in TOR or ELL.**
  - Both delegate rotations to `edit_srot` → `ft_mat` callback.
  - `rt_tor_mat` copies `v` and `h` to local temporaries before
    calling `MAT4X3VEC` — alias-safe.
  - `rt_ell_mat` copies `v`, `a`, `b`, `c` to local temporaries —
    alias-safe.
  - MGED uses `transform_editing_solid` → `rt_matrix_transform` →
    `ft_xform` = `rt_generic_xform`, which
    exports to wire format and reimports with the transform applied —
    inherently alias-safe.
  - TGC's `rt_tgc_mat` (used by `edit_srot` for TGC generic rotation)
    also copies to temporaries — alias-safe.  The aliasing bug was
    confined to the hand-coded `ecmd_tgc_rot_h`/`ecmd_tgc_rot_ab`
    functions, which directly manipulated the struct fields in-place.
- Added Agent Instructions section to this file with general principles
  for test expected value methodology, aliasing checks, and build/test
  commands.

---

### Session 5 (2026-03-02)
- Configured fresh brlcad_build; verified existing 10 tests pass.
- Wrote `src/librt/tests/edit/superell.cpp` — SUPERELL test coverage.
  Confirmed n/e exponents are unchanged by rt_superell_mat.
- Discovered and fixed `ecmd_cline_scale_h/r/t` strict `e_inpara != 1`
  check in `edcline.c` that blocked the XY mouse-driven (es_scale) path.
  Fixed to allow `else if (s->es_scale > 0.0)` branch when e_inpara==0.
- Discovered and fixed latent `edit_sscale` bug in `edgeneric.c`:
  `bn_mat_scale_about_pnt(mat, keypoint, 0.0)` was called when es_scale=0
  (initial/reset state), producing a singular matrix (mat[15]=0) and NaN.
  CLINE was uniquely exposed because its ft_set_edit_mode calls
  rt_edit_process unconditionally. Fix: guard `if (!e_inpara && es_scale < SMALL_FASTF) return 0`.
- Wrote `src/librt/tests/edit/cline.cpp` — CLINE test coverage.
- Updated CMakeLists.txt for superell/cline tests.
- Applied same e_inpara fix to `ecmd_ebm_height` (edebm.c) and
  `ecmd_dsp_scale_x/y/alt` (eddsp.c) for consistency.
- All 12 tests pass (tor, ell, tgc, epa, ehy, eto, hyp, rpc, rhc, part,
  superell, cline).

---

### Sessions 6-7 (2026-03-04)
Large burst of new tests and new ECMDs.  **All 27 tests now pass.**

New tests written (all pass):
- `arb8.cpp` — added ECMD_ARB_MOVE_FACE and EARB on top of generic ops
- `pipe.cpp` — ECMD_PIPE_SCALE_OD, ECMD_PIPE_PT_MOVE + generic ops
- `ars.cpp` — ECMD_ARS_MOVE_PT, ECMD_ARS_SCALE_CRV, ECMD_ARS_SCALE_COL,
  ECMD_ARS_INSERT_CRV + generic ops
- `bot.cpp` — ECMD_BOT_MOVEV, ECMD_BOT_MOVEV_LIST, ECMD_BOT_ESPLIT,
  ECMD_BOT_FSPLIT, ECMD_BOT_VERTEX_FUSE, ECMD_BOT_FACE_FUSE + generic ops
- `dsp.cpp` — ECMD_DSP_SCALE_X/Y/ALT, ECMD_DSP_SET_SMOOTH,
  ECMD_DSP_SET_DATASRC + generic ops
- `ebm.cpp` — ECMD_EBM_HEIGHT (keyboard + es_scale) + generic ops
- `extrude.cpp` — ECMD_EXTR_SCALE_H/A/B, ECMD_EXTR_MOV_H, ECMD_EXTR_ROT_H/A/B + generic ops
- `hrt.cpp` — RT_PARAMS_EDIT_SCALE/TRANS/ROT, XY scale/trans
- `metaball.cpp` — ECMD_METABALL_SET_THRESHOLD/METHOD, ECMD_METABALL_PT_MOV/ADD/DEL + generic
- `sph.cpp` — ELL edit path for sphere; XY scale
- `vol.cpp` — ECMD_VOL_CSIZE, ECMD_VOL_THRESH_LO/HI + generic ops
- `sketch.cpp` — all sketch ECMDs including new ECMD_SKETCH_MOVE_VERTEX_LIST
- `bspline.cpp` — ECMD_BSPLINE_PICK_KNOT, ECMD_BSPLINE_SET_KNOT
- `nmg.cpp` — ECMD_NMG_VPICK/VMOVE/FPICK/FMOVE
- `comb.cpp` — ECMD_COMB_ADD_MEMBER/DEL_MEMBER/SET_OP/SET_MATRIX

New ECMDs added to librt this session (not in MGED):
- `ECMD_ARS_SCALE_CRV` (5048), `ECMD_ARS_SCALE_COL` (5049),
  `ECMD_ARS_INSERT_CRV` (5050)
- `ECMD_SKETCH_MOVE_VERTEX_LIST` (26010) — move multiple vertices by UV delta
- `ECMD_BOT_VERTEX_FUSE` (30076), `ECMD_BOT_FACE_FUSE` (30077)
- `ECMD_DSP_SET_SMOOTH` (25061), `ECMD_DSP_SET_DATASRC` (25062)
- `ECMD_NMG_LEXTRU_DIR` (11032) — loop extrusion with explicit direction+dist
- `ECMD_COMB_ADD_MEMBER` (12001), `ECMD_COMB_DEL_MEMBER` (12002),
  `ECMD_COMB_SET_OP` (12003), `ECMD_COMB_SET_MATRIX` (12004)
- `ECMD_BSPLINE_PICK_KNOT` (9020), `ECMD_BSPLINE_SET_KNOT` (9021)
- `rt_edit_checkpoint()` / `rt_edit_revert()` in `edit.cpp`/`edit.h`

Bug fixes:
- `RT_EDIT_MAXPARA` expanded 16→20 to accommodate ECMD_COMB_SET_MATRIX

---

## Suggested Next Steps

1. **Fix remaining NULL keypoints** — audit ID_REVOLVE (40), ID_PNTS (41),
   ID_JOINT (23), ID_SUBMODEL (28).  HRT is confirmed fixed via test;
   it never had a NULL keypoint.
2. **Implement XY rotation** — validate and integrate the mgedrework
   `edit_mrot_xy` approach once the rotation math is confirmed correct.
3. **Audit mgedrework edit.cpp changes** — the cleaner IDLE handling
   and callback improvements look correct and should be cherry-picked.
4. **ARB8 rotate-face deep-dive** — `ECMD_ARB_SETUP_ROTFACE` requires an
   interactive callback for `fixv` selection; consider adding a non-
   interactive version that accepts a vertex index directly.
5. **PIPE remaining operations** — ECMD_PIPE_SCALE_RADIUS, ECMD_PIPE_PT_OD/ID/RADIUS
   (per-point scale), ECMD_PIPE_PT_INS (prepend), and ECMD_PIPE_SPLIT could
   use additional coverage.
