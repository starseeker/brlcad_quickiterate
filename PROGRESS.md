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

## Primitive Status (all 27 tests pass)

| Primitive | Source | Test | Notes |
|-----------|--------|------|-------|
| TOR | `edtor.c` | `tor.cpp` | Template for all others |
| ELL | `edell.c` | `ell.cpp` | ft_keypoint NULL bug fixed |
| TGC | `edtgc.c` | `tgc.cpp` | MAT4X3VEC aliasing fixed in ecmd_tgc_rot_h/rot_ab |
| ARB8 | `edarb.c` | `arb8.cpp` | ECMD_ARB_MOVE_FACE, EARB |
| EPA | `edepa.c` | `epa.cpp` | |
| EHY | `edehy.c` | `ehy.cpp` | |
| ETO | `edeto.c` | `eto.cpp` | MAT4X3VEC aliasing fixed in ecmd_eto_rot_c |
| HYP | `edhyp.c` | `hyp.cpp` | MAT4X3VEC aliasing fixed in ecmd_hyp_rot_h |
| RPC | `edrpc.c` | `rpc.cpp` | |
| RHC | `edrhc.c` | `rhc.cpp` | |
| PART | `edpart.c` | `part.cpp` | ECMD names fixed for scanner; build deps fixed |
| PIPE | `edpipe.c` | `pipe.cpp` | ft_set_edit_mode(ECMD_PIPE_PT_DEL) calls rt_edit_process |
| ARS | `edars.c` | `ars.cpp` | ECMD_ARS_SCALE_CRV/COL/INSERT_CRV added |
| BOT | `edbot.c` | `bot.cpp` | ECMD_BOT_MOVEV_LIST/ESPLIT/FSPLIT/VERTEX_FUSE/FACE_FUSE added |
| SUPERELL | `edsuperell.c` | `superell.cpp` | |
| HRT | `edhrt.c` (via edit_generic) | `hrt.cpp` | No ft_keypoint (origin used) |
| DSP | `eddsp.c` | `dsp.cpp` | ECMD_DSP_SET_SMOOTH/DATASRC added; e_inpara guard fixed |
| EBM | `edebm.c` | `ebm.cpp` | e_inpara guard fixed |
| CLINE | `edcline.c` | `cline.cpp` | e_inpara guard fixed; edit_sscale es_scale==0 guard added |
| METABALL | `edmetaball.c` | `metaball.cpp` | ECMD_METABALL_PT_SWEAT added |
| EXTRUDE | `edextrude.c` | `extrude.cpp` | ECMD_EXTR_SCALE_A/B and ROT_A/B added |
| BSPLINE | `edbspline.c` | `bspline.cpp` | ECMD_BSPLINE_PICK_KNOT/SET_KNOT added; ECMD_SPLINE_VPICK implemented |
| NMG | `ednmg.c` | `nmg.cpp` | ECMD_NMG_VPICK/VMOVE/FPICK/FMOVE/LEXTRU_DIR added + tested |
| SKETCH | `edsketch.c` | `sketch.cpp` | Full ECMD suite; ECMD_SKETCH_MOVE_VERTEX_LIST added |
| COMB | `edcomb.c` | `comb.cpp` | All ECMDs: ADD/DEL/SET_OP/SET_MATRIX + SET_REGION/COLOR/SHADER/MATERIAL/REGION_ID/AIRCODE/GIFTMATER/LOS |
| SPH | (via ELL path) | `sph.cpp` | |
| VOL | `edvol.c` | `vol.cpp` | |

---

## Known Issues / TODO

### 1. NULL Keypoints in EDOBJ Table

Primitives using `edit_generic` need a valid `ft_keypoint` in both the
OBJ table (`table.cpp`) and the EDOBJ table (`edtable.cpp`).  Without
one, scale and rotate operations use the world origin (0,0,0).

**Fixed:**
- `ID_ELL` (3): `NULL` → `edit_keypoint` in edtable.cpp
- `ID_REVOLVE` (40): added `rt_revolve_keypoint` (uses `v3d`) in revolve.c + table.cpp + edtable.cpp
- `ID_JOINT` (23): added `rt_joint_keypoint` (uses `location`) in joint.c + table.cpp + edtable.cpp

**Still NULL (needs audit before changing):**
- `ID_SUBMODEL` (28): references another .g file; no intrinsic vertex — likely intentional
- `ID_PNTS` (41): point cloud, no single vertex — likely intentional
- `ID_HRT` (43): currently uses origin; test passes because HRT vertex is at origin by convention
- `ID_BREP` (37): complex surface representation — intentional; see BREP editing notes
- `ID_COMBINATION` (31): combs don't use solid-edit keypoints — intentional
- `ID_BINUNIF`, `ID_UNUSED1`, `ID_UNUSED2`: no edit support — intentional

### 2. XY Mouse Rotation — **COMPLETED** (session 4)

`edit_mrot_xy` was validated against MGED's `doevent.c` / `f_knob` /
`mged_erot_xyz` knob path and merged into `edgeneric.c`.
`edit_generic_xy` now handles `RT_PARAMS_EDIT_ROT` (solid rotation) and
`RT_MATRIX_EDIT_ROT` (object rotation) via `edit_mrot_xy(s, mousevec, 0/1)`.

### 3. RT_MATRIX_EDIT_ROT via ft_edit_xy — **COMPLETED** (session 4)

Handled by `edit_mrot_xy` (matrix_edit=1) in `edit_generic_xy`.  See §2.

### 4. ARB8 Rotate-Face — **COMPLETED** (session 4)

`ecmd_arb_setup_rotface` accepts a vertex index directly via `e_para[0]`
when no interactive callback is registered, enabling programmatic use.
Test coverage added in `arb8.cpp` (`ECMD_ARB_SETUP_ROTFACE` +
`ECMD_ARB_ROTATE_FACE` non-interactive path).

### 5. PIPE / NMG / BSPLINE / COMB — **COMPLETED** (sessions 2–3)

- PIPE: All ECMDs covered; `pipe_split_pnt` implemented (was a stub).
- NMG: `ECMD_NMG_LEXTRU_DIR` tested via wire-loop NMG.
- BSPLINE: `sedit_vpick` uses `nurb_closest2d` + `s->vp` model2objview matrices.
- COMB material properties: `SET_REGION/COLOR/SHADER/MATERIAL/REGION_ID/AIRCODE/GIFTMATER/LOS` added.

### 6. Sketch Segment Split / NURB Segments / Mouse Proximity Pick

- Segment split at parameter t is not implemented (requires
  parameterization per segment type).
- NURB segments are rarely used and not yet editable via ECMD.
- Mouse-proximity vertex picking (`ECMD_SKETCH_PICK_VERTEX` via
  nearest-vertex hit test in `ft_edit_xy`) is not yet implemented.

