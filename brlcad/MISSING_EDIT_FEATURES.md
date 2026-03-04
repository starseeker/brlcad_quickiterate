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

`edpipe.c` fully implements all pipe edit operations.  The only gap is
limited test coverage for:
- `ECMD_PIPE_SCALE_RADIUS`, `ECMD_PIPE_PT_OD/ID/RADIUS` (per-point set)
- `ECMD_PIPE_PT_INS` (prepend) and `ECMD_PIPE_SPLIT`

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

## 4. NMG (ID_NMG) — mostly complete

`ednmg.c` implements edge/vertex/face pick+move and loop extrusion.

### Remaining gap

| Feature | Status |
|---------|--------|
| `ECMD_NMG_LEXTRU_DIR` test | Added to librt but not yet tested; requires a wire-loop NMG (loop in `lu_hd`, not in a faceuse) |
| Shell-level operations | Only accessible via BRL-CAD command API; not in scope for `rt_edit` |

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

`edmetaball.c` fully implements all metaball edit operations.

### Remaining minor gap

| Feature | Status |
|---------|--------|
| `ECMD_METABALL_PT_SWEAT` | Per-point "sweat" weight settable only via `adjust`; could add a dedicated ECMD |

---

## 8. Combination / Boolean Tree (ID_COMBINATION) — mostly complete

`edcomb.c` provides ECMDs for boolean tree manipulation.

### Remaining gaps

| Feature | Status |
|---------|--------|
| Material property ECMDs | Region flag, los, air, gift, rgb, shader settable only via `adjust`; need `ECMD_COMB_SET_REGION`, `ECMD_COMB_SET_MATERIAL`, etc. |
| Multi-member selection / invert | GUI-level concern; no librt API needed |

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

## 12. BSPLINE / NURBS (ID_BSPLINE) — mostly complete

`edbspline.c` implements control-point pick/move and knot pick/set.

### Remaining gap

| Feature | Status |
|---------|--------|
| `ECMD_SPLINE_VPICK` mouse proximity | Body guarded by `#if 0`; needs view-to-model matrix wired through `sedit_vpick` |

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
- `edit_generic_xy` returns an error for `RT_PARAMS_EDIT_ROT` and directs
  callers to the knob path.
- `brlcad_mgedrework/edgeneric.c` has an experimental `edit_mrot_xy` (UNTESTED).
- **To do**: validate `edit_mrot_xy` against MGED's `doevent.c` / `f_knob`
  / `mged_erot_xyz`, then merge into `edgeneric.c`.

### 13.5 RT_MATRIX_EDIT_ROT via ft_edit_xy
- Same situation as §13.4.  `edit_mrot_xy` in mgedrework handles both cases.

### 13.6 Undo/Redo Stack (Multi-Level)
- Not in scope for `struct rt_edit`; should be implemented at the
  libged/qged level as an edit transaction log.

---

## 14. Summary Table

| Primitive | ECMD coverage | Remaining gaps |
|-----------|--------------|----------------|
| Sketch    | Core CRUD + multi-vertex move | Segment split at t; NURB edit; mouse proximity pick |
| Pipe      | Complete | Additional test coverage; convenience batch-set function |
| BOT       | Complete | None |
| NMG       | Complete (edges + face/vertex + loop extrude dir) | LEXTRU_DIR test; shell-level ops |
| Extrude   | Complete | `keypoint` index (deprecated field) |
| ARS       | Complete | None |
| Metaball  | Complete | `ECMD_METABALL_PT_SWEAT` (minor) |
| Comb      | Boolean tree complete | Material property ECMDs |
| DSP       | Complete | None |
| EBM       | Complete | None |
| VOL       | Complete | None |
| BSPLINE   | CP pick/move, knot pick/set | `ECMD_SPLINE_VPICK` mouse proximity |
