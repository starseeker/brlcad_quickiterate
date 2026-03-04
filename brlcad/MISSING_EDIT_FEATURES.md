# Missing Edit Features: librt vs. MGED/Archer GUI

This document surveys primitive-specific editing capabilities present in
MGED/Archer Tcl GUIs that are not yet exposed as a librt C API.  It is
intended as design documentation for developers working toward a full
qged/librt-based CAD editor.

Sources surveyed:
- `src/tclscripts/mged/` -- MGED Tcl scripts (skt_ed.tcl, botedit.tcl, ...)
- `src/tclscripts/archer/` -- Archer edit frames (SketchEditFrame.tcl, PipeEditFrame.tcl, BotEditFrame.tcl, MetaballEditFrame.tcl, ExtrudeEditFrame.tcl, CombEditFrame.tcl, ...)
- `src/mged/edsol.c` and `src/mged/sedit.h` -- MGED C solid-edit layer
- `src/librt/primitives/*/ed*.c` -- librt edit implementations
- `include/rt/edit.h` -- librt RT_PARAMS_EDIT_* / RT_MATRIX_EDIT_* flags

---

## What librt Already Provides

All primitives that have a `src/librt/primitives/<prim>/ed<prim>.c` file
support the following generic edit modes via `rt_edit_process()`:

| Flag | Meaning |
|------|---------|
| `RT_PARAMS_EDIT_TRANS` | Translate primitive vertex/keypoint |
| `RT_PARAMS_EDIT_SCALE` | Uniform scale about keypoint |
| `RT_PARAMS_EDIT_ROT`   | Rotate primitive about keypoint/view/eye/model |
| `RT_MATRIX_EDIT_ROT`   | Accumulate rotation into `model_changes` matrix |
| `RT_MATRIX_EDIT_TRANS_MODEL_XYZ` | Translate keypoint in model space |
| `RT_MATRIX_EDIT_TRANS_VIEW_XY/X/Y` | Translate in view space |
| `RT_MATRIX_EDIT_SCALE[_X/Y/Z]` | Scale via `model_changes` matrix |
| `ECMD_<PRIM>_*` | Primitive-specific parameter edits (per primitive) |

Cross-cutting features now in librt:
- `rt_edit_checkpoint(s)` / `rt_edit_revert(s)` — single-level undo
- `rt_edit_snap_point(pt, s)` — snap UV coordinates to grid
- `struct rt_edit::e_para[RT_EDIT_MAXPARA]` — 20-element parameter array

---

## 1. Sketch (ID_SKETCH) — mostly complete

`edsketch.c` implements the full ECMD suite for vertex/segment CRUD.
`struct rt_sketch_edit` holds `curr_vert` and `curr_seg` selection state.

### Remaining gaps

| Feature | Status |
|---------|--------|
| Split segment at parameter t | Not implemented; requires per-type parameterization |
| NURB segment add/edit | Not implemented; rarely used in practice |
| Mouse-proximity vertex picking | `ft_edit_xy` needs 2-D UV proximity query against `skt->verts` |

---

## 2. Pipe (ID_PIPE) — complete

`edpipe.c` fully implements all pipe edit operations.  Full test
coverage was added in session 2:
- `ECMD_PIPE_SCALE_RADIUS`, `ECMD_PIPE_PT_OD/ID/RADIUS`
- `ECMD_PIPE_PT_INS` and `ECMD_PIPE_SPLIT`
- `pipe_split_pnt` was a stub in both MGED and librt; it is now fully
  implemented.

A convenience function that sets all parameters of a point in one call
would also be useful:
```c
int rt_pipe_edit_pt_params(struct rt_edit *s, int pt_index,
                           fastf_t od, fastf_t id, fastf_t bend_radius,
                           const point_t pos);
```

---

## 3. BOT (ID_BOT) — complete

`edbot.c` implements all BOT edit operations including multi-vertex move,
edge split, face split, vertex fuse, and face fuse.

---

## 4. NMG (ID_NMG) — complete

`ednmg.c` implements edge/vertex/face pick+move and loop extrusion.
`ECMD_NMG_LEXTRU_DIR` is now tested via a wire-loop NMG (session 2).

---

## 5. Extrude (ID_EXTRUDE) — complete

`edextrude.c` implements H, A, B vector scale/rotate plus move-H and
sketch-name change.

### Remaining gap

| Feature | Status |
|---------|--------|
| `keypoint` integer index | Rarely used deprecated field; could add `ECMD_EXTR_SET_KEYPOINT` accepting `e_para[0]` as an integer index |

---

## 6. ARS (ID_ARS) — complete

`edars.c` implements full curve/column/point edit operations including
insert, scale, and delete.

---

## 7. Metaball (ID_METABALL) — complete

`edmetaball.c` fully implements all metaball edit operations including
`ECMD_METABALL_PT_SWEAT` (added in session 2).

---

## 8. Combination / Boolean Tree (ID_COMBINATION) — complete

`edcomb.c` provides ECMDs for both boolean tree manipulation and
region material properties.  The material-property ECMDs were added
in session 2:

| ECMD | Field |
|------|-------|
| `ECMD_COMB_SET_REGION` | `region_flag` |
| `ECMD_COMB_SET_COLOR` | `rgb` / `rgb_valid` |
| `ECMD_COMB_SET_SHADER` | `shader` |
| `ECMD_COMB_SET_MATERIAL` | `material` |
| `ECMD_COMB_SET_REGION_ID` | `region_id` |
| `ECMD_COMB_SET_AIRCODE` | `aircode` |
| `ECMD_COMB_SET_GIFTMATER` | `GIFTmater` |
| `ECMD_COMB_SET_LOS` | `los` |

---

## 9. DSP (ID_DSP) — complete

`eddsp.c` implements grid scale, altitude scale, filename, smooth toggle,
and data-source switching.

---

## 10. EBM (ID_EBM) — complete

`edebm.c` implements filename, dimensions, and extrusion depth edits.

---

## 11. VOL (ID_VOL) — complete

`edvol.c` implements voxel cell size, grid dimensions, filename, and
threshold edits.

---

## 12. BSPLINE / NURBS (ID_BSPLINE) — complete

`edbspline.c` implements control-point pick/move and knot pick/set.
`ECMD_SPLINE_VPICK` mouse proximity was implemented in session 3 by
removing the `#if 0` guard and wiring the model2objview matrix from
`s->vp->gv_model2view` × `s->model_changes`.

---

## 13. Cross-Cutting Edit-Mode Features

### 13.1 Snap-to-Grid
- `struct rt_edit::snap.{enabled, spacing}` implemented.
- `rt_edit_snap_point(pt, s)` called by `edsketch.c` automatically.
- **Remaining gap**: No major/minor grid distinction; no grid anchor offset; no 3-D snapping for non-sketch primitives.

### 13.2 Checkpoint / Revert (Single-Level Undo)
- **DONE**: `rt_edit_checkpoint(s)` and `rt_edit_revert(s)` serialise/restore `es_int`.

### 13.3 Coordinate System Switching
- Fully supported via `mv_context`, `gv_coord`, `gv_rotate_about`.
- **Gap**: No Tcl/GUI control for `gv_coord='o'` (object coordinate frame) in Archer.

### 13.4 XY Mouse Rotation (RT_PARAMS_EDIT_ROT via ft_edit_xy)
- **DONE**: `edit_mrot_xy` validated against MGED's `doevent.c` / `f_knob` /
  `mged_erot_xyz` and merged into `edgeneric.c`.  `edit_generic_xy` handles
  `RT_PARAMS_EDIT_ROT` by converting the view-space cursor delta to "ax"/"ay"
  knob increments via `rt_edit_knob_cmd_process`.

### 13.5 RT_MATRIX_EDIT_ROT via ft_edit_xy
- **DONE**: Handled by `edit_mrot_xy(s, mousevec, 1)` in `edit_generic_xy`.
  Matrix rotation and solid rotation share the same implementation.

### 13.6 Undo/Redo Stack (Multi-Level)
- Not in scope for `struct rt_edit`; should be implemented at the
  libged/qged level as an edit transaction log.

---

## 14. Summary Table

| Primitive | ECMD coverage | Remaining gaps |
|-----------|--------------|----------------|
| Sketch    | Core CRUD + multi-vertex move | Segment split at t; NURB edit; mouse proximity pick |
| Pipe      | Complete | None |
| BOT       | Complete | None |
| NMG       | Complete (edges + face/vertex + loop extrude dir) | Shell-level ops (out of scope) |
| Extrude   | Complete | `keypoint` index (deprecated field) |
| ARS       | Complete | None |
| Metaball  | Complete | None |
| Comb      | Complete | Multi-member selection (GUI-level) |
| DSP       | Complete | None |
| EBM       | Complete | None |
| VOL       | Complete | None |
| BSPLINE   | Complete | None |
