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

---

## 1. Sketch (ID_SKETCH)

### librt status
`sketch.c` has `rt_sketch_mat()` for matrix application, but **no
`ed<sketch>.c` — no `ft_edit`, `ft_edit_xy`, or `ft_set_edit_mode`
functab entries exist**.  All sketch segment editing lives entirely in Tcl.

### Tcl/GUI capabilities (skt_ed.tcl + SketchEditFrame.tcl)

| Feature | Implementation |
|---------|---------------|
| Create line segment (2 vertex picks) | SketchEditFrame `create_line` + canvas bindings |
| Create arc segment (center, radius, angle) | SketchEditFrame `create_arc` |
| Create Bézier curve (N control-point picks) | SketchEditFrame `create_bezier` |
| Move individual vertex | SketchEditFrame `setup_move_arbitrary`, `start_move_arbitrary` |
| Move whole segment | SketchEditFrame `setup_move_segment`, `start_move_segment` |
| Move selected set of vertices | SketchEditFrame `setup_move_selected` |
| Delete vertex (if unused) | SketchEditFrame (guarded delete, checked against all segs) |
| Delete segment | SketchEditFrame canvas delete logic |
| Split segment at vertex | SketchEditFrame split logic |
| Snap-to-grid | SketchEditFrame `do_snap_sketch` (configurable major/minor spacing, anchor) |
| Draw grid overlay | SketchEditFrame `build_grid` |
| Snap enable/disable toggle | `snapgridCB` checkbutton |
| Display/edit 2-D vertex list | `sketchVL` entry widgets |
| Checkpoint / revert | Tcl-level save/restore of VL and SL arrays |

### Missing from librt
Everything.  librt has no ECMD constants for sketch segment editing.

### Design notes for librt sketch API

```c
// New ECMD constants (edsketch.c)
#define ECMD_SKETCH_ADD_LINE    <n>   // add line seg (two vertex indices)
#define ECMD_SKETCH_ADD_ARC     <n>   // add arc seg (center-is-left flag, radius)
#define ECMD_SKETCH_ADD_BEZIER  <n>   // add Bezier seg (degree, control-pt list)
#define ECMD_SKETCH_MOVE_VERT   <n>   // move vertex at index i to e_para XY
#define ECMD_SKETCH_DEL_VERT    <n>   // delete unused vertex at index i
#define ECMD_SKETCH_DEL_SEG     <n>   // delete segment at index i
#define ECMD_SKETCH_SPLIT_SEG   <n>   // split segment at midpoint / parameter t
#define ECMD_SKETCH_PICK_VERT   <n>   // set current_vertex for subsequent edits
#define ECMD_SKETCH_PICK_SEG    <n>   // set current_segment
```

The snap-to-grid and 2-D canvas interaction are necessarily higher-level
(GUI toolkit) concerns, but the underlying vertex-move operations should be
C API so qged can implement them without Tcl.

---

## 2. Pipe (ID_PIPE)

### librt status
`edpipe.c` defines and fully implements:

| ECMD | Meaning |
|------|---------|
| `ECMD_PIPE_SELECT`       | pick a pipe control point |
| `ECMD_PIPE_NEXT_PT`      | advance to next point |
| `ECMD_PIPE_PREV_PT`      | retreat to previous point |
| `ECMD_PIPE_SPLIT`        | split segment at current point |
| `ECMD_PIPE_PT_ADD`       | append a point at the pipe end |
| `ECMD_PIPE_PT_INS`       | prepend a point at the pipe start |
| `ECMD_PIPE_PT_DEL`       | delete current point |
| `ECMD_PIPE_PT_MOVE`      | move current point via mouse/XY |
| `ECMD_PIPE_PT_OD`        | set outer diameter of current point |
| `ECMD_PIPE_PT_ID`        | set inner diameter of current point |
| `ECMD_PIPE_SCALE_OD`     | scale outer diameter |
| `ECMD_PIPE_SCALE_ID`     | scale inner diameter |
| `ECMD_PIPE_PT_RADIUS`    | set bend radius of current point |
| `ECMD_PIPE_SCALE_RADIUS` | scale bend radius |

### Tcl/GUI capabilities (PipeEditFrame.tcl)

Archer wraps the ECMD constants through `p_pscale` / `p_ptranslate` /
`moveElement` callbacks plus a table widget showing per-point OD, ID,
bend radius, and XYZ.  The GUI adds:

| Feature | Status |
|---------|--------|
| Per-point OD/ID/radius numeric entry | Tcl table widget → `adjust` command |
| Append / prepend point buttons | Calls `ECMD_PIPE_PT_ADD` / `ECMD_PIPE_PT_INS` via `p` method |
| Delete point button | Calls `ECMD_PIPE_PT_DEL` |
| Move point by mouse drag | `endPipePointMove` → `moveElement` → `ECMD_PIPE_PT_MOVE` |
| Point navigation (next/prev) | `pipePointSelectCallback` → table selection |
| Checkpoint / revert | Full copy of `mDetail` array |

### Missing from librt
The librt API is nearly complete.  The only gap is:
- No batch-set of all point parameters in a single call (currently done
  via repeated `adjust` in Tcl).

### Design notes
Add a convenience function:
```c
int rt_pipe_edit_pt_params(struct rt_edit *s, int pt_index,
                           fastf_t od, fastf_t id, fastf_t bend_radius,
                           const point_t pos);
```

---

## 3. BOT (ID_BOT — Bag of Triangles)

### librt status
`edbot.c` defines:

| ECMD | Meaning |
|------|---------|
| `ECMD_BOT_PICKV` | pick vertex |
| `ECMD_BOT_PICKE` | pick edge |
| `ECMD_BOT_PICKT` | pick triangle |
| `ECMD_BOT_MOVEV` | move vertex via mouse |
| `ECMD_BOT_MOVEE` | move edge midpoint via mouse |
| `ECMD_BOT_MOVET` | move triangle centroid via mouse |
| `ECMD_BOT_MODE`  | set BOT mode (surface/solid/plate) |
| `ECMD_BOT_ORIENT`| set face orientation |
| `ECMD_BOT_THICK` | set face thickness (plate mode) |
| `ECMD_BOT_FMODE` | set face mode (one or all) |
| `ECMD_BOT_FDEL`  | delete current face |
| `ECMD_BOT_FLAGS` | set BOT flags |

### Tcl/GUI capabilities (BotEditFrame.tcl + boteditor/)

| Feature | Status |
|---------|--------|
| Multi-vertex select (Shift+click, rubber-band) | `multiPointSelectCallback`, `moveBotPtsMode` |
| Multi-edge select | `multiEdgeSelectCallback` |
| Multi-face select | `multiFaceSelectCallback` |
| Move multiple vertices simultaneously | `moveBotPts` |
| Edge split (add midpoint vertex) | `botEdgeSplitCallback` |
| Face split | `botFaceSplitCallback` |
| Vertex/edge/face table with per-row editing | `loadTables`, `validateDetailEntry` |
| Highlight current vertex/edge/face in view | `highlightCurrentBotElements` |
| Select by index vs. pick-in-view | `botPointSelectCallback` vs. `moveBotPtMode` |
| BOT repair / cleanup | `boteditor/botTools.tcl` (vertex fuse, degenerate removal) |

### Missing from librt

| Feature | Gap |
|---------|-----|
| Multi-vertex move in one call | No `ECMD_BOT_MOVEV_LIST` |
| Edge split (insert midpoint) | No `ECMD_BOT_ESPLIT` |
| Face split | No `ECMD_BOT_FSPLIT` |
| Vertex merge/fuse | Only in `src/tclscripts/mged/bot_vertex_fuse_all.tcl` → calls `bot_vertex_fuse` command |
| Degenerate-face removal | Only in Tcl via `bot_face_fuse` |

### Design notes
```c
#define ECMD_BOT_MOVEV_LIST   <n>  // move list of vertices by common delta
#define ECMD_BOT_ESPLIT       <n>  // split edge, insert midpoint vertex + 2 new faces
#define ECMD_BOT_FSPLIT       <n>  // split face into 3 by centroid
```

---

## 4. NMG (ID_NMG — Non-Manifold Geometry)

### librt status
`ednmg.c` defines:

| ECMD | Meaning |
|------|---------|
| `ECMD_NMG_EPICK`  | pick edge-use |
| `ECMD_NMG_EMOVE`  | move edge via mouse |
| `ECMD_NMG_EDEBUG` | debug dump of current edge |
| `ECMD_NMG_FORW`   | advance to next edge-use |
| `ECMD_NMG_BACK`   | retreat to previous edge-use |
| `ECMD_NMG_RADIAL` | jump to radial+mate edge-use |
| `ECMD_NMG_ESPLIT` | split current edge |
| `ECMD_NMG_EKILL`  | kill current edge |
| `ECMD_NMG_LEXTRU` | extrude current loop |

### Tcl/GUI capabilities
MGED's NMG editing is almost entirely mediated through the ECMD constants
above plus `sedit.c` state tracking.  There is **no Archer EditFrame for
NMG**.  All interaction is keyboard-driven via the MGED command line:
`nmg_simplify`, `nmg_extrude`, `nmg_fix_normals`, etc.

### Missing from librt / gaps

| Feature | Gap |
|---------|-----|
| Face pick/move | No `ECMD_NMG_FPICK` / `ECMD_NMG_FMOVE` |
| Vertex pick/move | No `ECMD_NMG_VPICK` / `ECMD_NMG_VMOVE` |
| Loop extrude with direction | `ECMD_NMG_LEXTRU` only extrudes by e_para, no direction control |
| Shell-level operations | Only accessible via BRL-CAD command API |

### Design notes
NMG editing is complex enough that a full GUI would need substantial new
ECMD constants.  For qged, the minimal useful additions are:
```c
#define ECMD_NMG_VPICK   <n>  // pick vertex
#define ECMD_NMG_VMOVE   <n>  // move vertex via mouse/e_para
#define ECMD_NMG_FPICK   <n>  // pick face
#define ECMD_NMG_FMOVE   <n>  // translate face
```

---

## 5. Extrude (ID_EXTRUDE)

### librt status
`edextrude.c` defines:

| ECMD | Meaning |
|------|---------|
| `ECMD_EXTR_SCALE_H`  | scale H vector length |
| `ECMD_EXTR_MOV_H`    | move H endpoint via mouse |
| `ECMD_EXTR_ROT_H`    | rotate H vector |
| `ECMD_EXTR_SKT_NAME` | change reference sketch name |

### Tcl/GUI capabilities (ExtrudeEditFrame.tcl)

| Feature | Implementation |
|---------|---------------|
| Scale H via numeric entry | `setH` mode → `p_pscale obj h` |
| Move H endpoint via mouse | `moveHR` mode → `ECMD_EXTR_MOV_H` |
| Move H in model space     | `moveH` mode → `p_ptranslate obj h` |
| Edit A/B vectors directly | Direct numeric entry → `adjust` command |
| Change sketch reference   | Invokes `cad_input_dialog` → `ECMD_EXTR_SKT_NAME` |

### Missing from librt

| Feature | Gap |
|---------|-----|
| Edit A vector (reference width direction) | No `ECMD_EXTR_SCALE_A` / `ECMD_EXTR_ROT_A` |
| Edit B vector (reference height direction) | No `ECMD_EXTR_SCALE_B` / `ECMD_EXTR_ROT_B` |
| `keypoint` parameter edit | Only via `adjust` in Tcl |

### Design notes
```c
#define ECMD_EXTR_SCALE_A   <n>  // scale A (sketch width) vector
#define ECMD_EXTR_SCALE_B   <n>  // scale B (sketch height) vector
#define ECMD_EXTR_ROT_A     <n>  // rotate A vector
#define ECMD_EXTR_ROT_B     <n>  // rotate B vector
```
These can be modeled on `ECMD_EXTR_SCALE_H` / `ECMD_EXTR_ROT_H`.

---

## 6. ARS (ID_ARS — Arbitrary Faceted Solid)

### librt status
`edars.c` defines:

| ECMD | Meaning |
|------|---------|
| `ECMD_ARS_PICK`      | pick a vertex by mouse |
| `ECMD_ARS_NEXT_PT`   | advance to next point in same curve |
| `ECMD_ARS_PREV_PT`   | retreat to previous point in same curve |
| `ECMD_ARS_NEXT_CRV`  | jump to corresponding point in next curve |
| `ECMD_ARS_PREV_CRV`  | jump to corresponding point in previous curve |
| `ECMD_ARS_MOVE_PT`   | translate current point |
| `ECMD_ARS_DEL_CRV`   | delete entire curve (row) |
| `ECMD_ARS_DEL_COL`   | delete a column (corresponding points across all curves) |
| `ECMD_ARS_DUP_CRV`   | duplicate current curve |
| `ECMD_ARS_DUP_COL`   | duplicate current column |
| `ECMD_ARS_MOVE_CRV`  | translate entire curve |
| `ECMD_ARS_MOVE_COL`  | translate entire column |
| `ECMD_ARS_PICK_MENU` | display pick sub-menu |
| `ECMD_ARS_EDIT_MENU` | display edit sub-menu |

### Tcl/GUI capabilities
MGED drives ARS editing entirely through the ECMD constants.  There is no
dedicated Archer EditFrame for ARS.  The Archer UI presents ARS parameters
as raw attributes only.

### Missing from librt
The ECMD coverage is good.  Gaps:
- No `ECMD_ARS_INSERT_CRV` (insert a new curve between two existing ones).
- No `ECMD_ARS_SCALE_CRV` / `ECMD_ARS_SCALE_COL` (uniform scale of row/col).

### Design notes
```c
#define ECMD_ARS_INSERT_CRV  <n>  // insert a new curve after current curve
#define ECMD_ARS_SCALE_CRV   <n>  // scale current curve about its centroid
#define ECMD_ARS_SCALE_COL   <n>  // scale current column
```

---

## 7. Metaball (ID_METABALL)

### librt status
`edmetaball.c` defines:

| ECMD | Meaning |
|------|---------|
| `ECMD_METABALL_SET_THRESHOLD` | set global threshold |
| `ECMD_METABALL_SET_METHOD`    | set render method |
| `ECMD_METABALL_PT_PICK`       | pick control point |
| `ECMD_METABALL_PT_MOV`        | move control point |
| `ECMD_METABALL_PT_FLDSTR`     | set field strength of current point |
| `ECMD_METABALL_PT_DEL`        | delete control point |
| `ECMD_METABALL_PT_ADD`        | add control point |
| `ECMD_METABALL_RMET`          | set render method (alias) |
| `ECMD_METABALL_PT_NEXT`       | advance to next control point |
| `ECMD_METABALL_PT_PREV`       | retreat to previous control point |
| `ECMD_METABALL_PT_SET_GOO`    | set "goo" (smoothing) parameter |

### Tcl/GUI capabilities (MetaballEditFrame.tcl)

| Feature | Implementation |
|---------|---------------|
| Point position, field strength via table | `validateDetailEntry` → `adjust` |
| Add / delete points via buttons | `metaballPointAddCallback`, `metaballPointDeleteCallback` |
| Move point by mouse drag | `endMetaballPointMove` → `moveElement` |
| Navigate points | `metaballPointSelectCallback`, `highlightCurrentPoint` |
| Global method and threshold edits | Direct numeric entry |
| Checkpoint / revert | Copy of detail array |

### Missing from librt
The ECMD API is nearly complete.  Gaps:
- No `ECMD_METABALL_PT_SET_SWEAT` — the second per-point weight parameter
  ("sweat") present in the data structure is not individually settable via
  ECMD (only via `adjust`).

### Design notes
```c
#define ECMD_METABALL_PT_SWEAT  <n>  // set sweat value of current point
```

---

## 8. Combination / Boolean Tree (ID_COMBINATION)

### librt status
librt has **no `ft_edit` or ECMD constants for combinations**.  The comb
type is not a geometric primitive in the traditional sense; its "editing"
is structural (tree manipulation).

### Tcl/GUI capabilities (CombEditFrame.tcl + mged/comb.tcl)

| Feature | Implementation |
|---------|---------------|
| Edit material properties (region flag, id, air, los, gift, rgb, shader) | `CombEditFrame::buildGeneralGUI` → `adjust` |
| Display / edit boolean tree as member table | `buildTreeGUI` / `buildMembersGUI` |
| Add member (union/intersect/subtract) | `appendRow`, `insertRow` → `combmem` command |
| Delete member | `deleteRow` → `combmem` |
| Change boolean operator per member | Table cell edit → `syncColumn` → `combmem` |
| Edit per-member transformation matrix | `setKeypoint`, `setKeypointVC` → `combmem -i` |
| Multi-member selection / invert | `invertSelect` |
| Checkpoint / revert | Save/restore of member data arrays |

### Missing from librt / what should be added for qged

The `combmem` command is a libged (not librt) function.  For qged, the
right approach is to expose combination tree editing through libged:

```c
// In libged (already partial):
int ged_combmem(struct ged *gedp, ...);  // already exists

// Desired additional C API:
int ged_comb_add_member(struct ged *gedp, const char *comb,
                        const char *member, int op,
                        const mat_t matrix);
int ged_comb_del_member(struct ged *gedp, const char *comb,
                        int member_index);
int ged_comb_set_member_op(struct ged *gedp, const char *comb,
                           int member_index, int new_op);
int ged_comb_set_member_matrix(struct ged *gedp, const char *comb,
                               int member_index, const mat_t matrix);
```

---

## 9. DSP (ID_DSP — Displacement Map)

### librt status
`eddsp.c` defines:

| ECMD | Meaning |
|------|---------|
| `ECMD_DSP_FNAME`     | set data file or object name |
| `ECMD_DSP_FSIZE`     | set grid dimensions |
| `ECMD_DSP_SCALE_X`   | scale X cell size |
| `ECMD_DSP_SCALE_Y`   | scale Y cell size |
| `ECMD_DSP_SCALE_ALT` | scale altitude |

### Tcl/GUI capabilities
MGED uses a menu-driven approach via `sedit.c`.  Archer's DspEditFrame
provides numeric entry for all the above parameters, plus:
- Toggle between file and in-db object data source.
- Smooth normals checkbox.

### Missing from librt
```c
#define ECMD_DSP_SET_SMOOTH  <n>  // toggle smooth-normals flag
#define ECMD_DSP_SET_DATASRC <n>  // switch between file and in-db data source
```

---

## 10. EBM (ID_EBM — Extruded Bitmap)

### librt status
`edebm.c` defines:

| ECMD | Meaning |
|------|---------|
| `ECMD_EBM_FNAME`  | set bitmap file name |
| `ECMD_EBM_FSIZE`  | set bitmap dimensions (width × height) |
| `ECMD_EBM_HEIGHT` | set extrusion depth |

### Tcl/GUI capabilities
Same numeric-entry form as DSP, no additional GUI-only features beyond the
three ECMD ops.

### Missing from librt
None significant.

---

## 11. VOL (ID_VOL — Volumetric Data)

### librt status
`edvol.c` defines:

| ECMD | Meaning |
|------|---------|
| `ECMD_VOL_CSIZE`      | set voxel cell size (XYZ) |
| `ECMD_VOL_FSIZE`      | set volume grid dimensions |
| `ECMD_VOL_THRESH_LO`  | set low threshold |
| `ECMD_VOL_THRESH_HI`  | set high threshold |
| `ECMD_VOL_FNAME`      | set data file name |

### Missing from librt
None significant.

---

## 12. BSPLINE / NURBS (ID_BSPLINE)

### librt status
`edbspline.c` routes the `ft_edit` to `edit_generic`.  There are **no
primitive-specific ECMD constants** for control-point editing.

### Tcl/GUI capabilities
MGED supports individual control-point selection/move for NURBS surfaces via
`edsol.c` and in-memory data access.

### Missing from librt
```c
#define ECMD_BSPLINE_PICK_CP   <n>  // pick control point (surf_u, surf_v)
#define ECMD_BSPLINE_MOVE_CP   <n>  // move control point via e_para
#define ECMD_BSPLINE_PICK_KNOT <n>  // pick a knot value
#define ECMD_BSPLINE_SET_KNOT  <n>  // set a knot value via e_para
```

---

## 13. Cross-Cutting Edit-Mode Features

### 13.1 Snap-to-Grid
- **Where**: `SketchEditFrame` has a full snap-to-grid with configurable
  major/minor spacing and anchor.  MGED's `grid.tcl` provides a grid
  display overlay and `adc` (angle/distance cursor) but does NOT provide
  automatic snap for solid editing.
- **librt gap**: No snap concept at the `rt_edit` level.
- **Design note**: Add `struct rt_edit_snap { int enabled; fastf_t spacing; }` 
  to `struct rt_edit`, consulted by `ft_edit_xy` implementations.

### 13.2 Checkpoint / Revert (Single-Level Undo)
- **Where**: Every Archer EditFrame implements `checkpointGeometry()` and
  `revertGeometry()` which copy the internal parameter state before an edit
  and can restore it.  This is a single-level undo (no stack).
- **librt gap**: `struct rt_edit` has no checkpoint mechanism.
- **Design note**: Add `rt_edit_checkpoint(struct rt_edit *s)` and
  `rt_edit_revert(struct rt_edit *s)` which copy/restore `es_int`.

### 13.3 Coordinate System Switching (View vs. Model Space)
- **Where**: MGED's `mv_context` flag on `struct rt_edit` already controls
  view vs. model context for translation and rotation.  The `gv_coord`
  field on `struct bview` selects view ('v'), model ('m'), or object ('o')
  coordinate frame for rotations.  The `gv_rotate_about` field selects the
  rotation center ('v'=view center, 'e'=eye, 'm'=model center, 'k'=keypoint).
- **librt status**: Already fully supported via `mv_context`, `gv_coord`,
  and `gv_rotate_about`.
- **Gap**: No Tcl/GUI control for `gv_coord='o'` (object coordinate frame)
  is exposed in Archer.

### 13.4 Undo/Redo Stack (Multi-Level)
- **Where**: Neither MGED nor Archer implement a true multi-level undo
  stack.  The database-level `keep`/`killtree`/`undo` commands provide
  coarse object-level undo only.
- **librt gap**: Not in scope for `struct rt_edit`; would need a higher-level
  edit history manager.
- **Design note**: Implement at the libged/qged level as an edit transaction
  log, not in librt.

### 13.5 Primitive-Specific Keyboard Shortcuts / Menus
- **Where**: MGED's `sedit.c` pops a menu of mode choices for each primitive
  via `ECMD_*_MENU` constants.  Archer replicates this with radio buttons in
  each EditFrame.
- **librt status**: `ft_set_edit_mode` accepts any ECMD constant directly.
  The menu/radio structure is a GUI concern, not a librt concern.
- **No librt gap**: callers simply call `ft_set_edit_mode(s, ECMD_*)`.

### 13.6 Mouse-to-Parameter Mapping (ft_edit_xy)
- **Where**: `ft_edit_xy` in each `ed<prim>.c` converts a 2-D mouse
  position (in ±BV normalized screen coordinates) to a parameter change.
  This is fully implemented for all primitives that have `ft_edit_xy`.
- **Gap for sketch**: Sketch has no `ft_edit_xy`; all mouse interaction is
  canvas-level Tcl.

---

## 14. Summary Table

| Primitive | librt ECMD coverage | Major Tcl-only features |
|-----------|--------------------|-----------------------|
| Sketch    | None               | All segment editing, snap-to-grid, canvas interaction |
| Pipe      | Complete           | None significant; checkpoint/revert is minor |
| BOT       | Good               | Multi-select move, edge/face split, vertex fuse |
| NMG       | Partial (edges only) | Face/vertex pick+move |
| Extrude   | Good (H only)      | A/B vector editing |
| ARS       | Complete           | Insert curve/column, scale row/col |
| Metaball  | Complete           | Sweat parameter |
| Comb      | None (libged)      | All: add/del/reorder members, set boolean op, set matrix |
| DSP       | Good               | Smooth flag, datasrc toggle |
| EBM       | Complete           | None |
| VOL       | Complete           | None |
| BSPLINE   | None               | Control-point pick/move, knot editing |

---

## 15. Priority Order for qged Migration

1. **Sketch** — most complex, most user-visible.  Needs full ECMD suite +
   snap infrastructure.
2. **Combination tree editing** — critical for any real assembly work;
   build on libged `combmem`.
3. **BOT multi-select and split** — needed for mesh editing workflows.
4. **NMG vertex/face editing** — needed if NMG is to be a first-class type.
5. **BSPLINE control-point editing** — needed for NURBS surface work.
6. **Extrude A/B vectors** — low effort, high value.
7. **Snap-to-grid in rt_edit** — infrastructure, enables better sketch and
   future 3-D grid snapping.
