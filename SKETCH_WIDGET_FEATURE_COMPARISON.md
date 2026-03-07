# Sketch Editing Widget Feature Comparison

MGED (`skt_ed.tcl`), Archer (`SketchEditFrame.tcl`), and the new Qt widget
(`QgSketchFilter` / `qsketch`).

---

## 1. Segment Types

| Feature | MGED | Archer | New Qt widget |
|---------|:----:|:------:|:-------------:|
| Line segment | ✓ | ✓ | ✓ |
| Full circle (CARC, radius < 0) | ✓ | ✓ | ✓ (via arc dialog, negative radius) |
| Circular arc (partial CARC) | ✓ | ✓ | ✓ |
| Bezier curve (arbitrary degree) | ✓ | ✓ | ✓ |
| NURB curve | — | — | ✓ (ECMD_SKETCH_APPEND_NURB) |

---

## 2. Vertex Operations

| Feature | MGED | Archer | New Qt widget |
|---------|:----:|:------:|:-------------:|
| Pick vertex by mouse proximity | ✓ | ✓ | ✓ (QgSketchPickVertexFilter) |
| Move single vertex interactively | ✓ | ✓ | ✓ (QgSketchMoveVertexFilter) |
| Move list of vertices by UV delta | — | — | ✓ (ECMD_SKETCH_MOVE_VERTEX_LIST) |
| Add vertex at click position | — | — | ✓ (QgSketchAddVertexFilter) |
| Delete unreferenced vertex | ✓ | ✓ | ✓ |
| Live vertex table (index, U, V) | — | — | ✓ |
| Snap vertex to grid | — | ✓ | ✓ (via rt_edit_snap_point in edsketch.c) |
| 3-D sketch origin (V) edit | — | ✓ | — |
| u_vec / v_vec (A, B) edit | — | ✓ | — |

---

## 3. Segment Operations

| Feature | MGED | Archer | New Qt widget |
|---------|:----:|:------:|:-------------:|
| Pick segment by mouse proximity | ✓ | ✓ | ✓ (QgSketchPickSegmentFilter) |
| Translate segment interactively | ✓ | ✓ | ✓ (QgSketchMoveSegmentFilter) |
| Move selected set of segments | ✓ | ✓ | — |
| Delete selected segment | ✓ | ✓ | ✓ |
| Split segment at parameter t | — | — | ✓ (ECMD_SKETCH_SPLIT_SEGMENT) |
| Live segment table (type, params) | — | — | ✓ |
| Segment reverse (complement) | ✓ | — | ✓ (ECMD_SKETCH_TOGGLE_ARC_ORIENT / `C` key) |
| Arc: drag-set radius | ✓ | ✓ | ✓ (QgSketchArcRadiusFilter / `I` key) |
| Arc: set tangency angle | ✓ | — | ✓ (ECMD_SKETCH_SET_TANGENCY / `T` key / QgSketchSetTangencyFilter) |
| Arc: use complement (other half) | ✓ | — | ✓ (ECMD_SKETCH_TOGGLE_ARC_ORIENT / `C` key) |
| Arc: center sampled for proximity (circle_3pt) | — | ✓ | — |
| NURB: edit knot vector | — | — | ✓ (ECMD_SKETCH_NURB_EDIT_KV) |
| NURB: edit weights | — | — | ✓ (ECMD_SKETCH_NURB_EDIT_WEIGHTS) |

---

## 4. Multi-select

| Feature | MGED | Archer | New Qt widget |
|---------|:----:|:------:|:-------------:|
| Select multiple segments | ✓ | ✓ (Shift-Button-1 in move mode) | — |
| Select multiple vertices | ✓ | ✓ (Shift-Button-1 in move mode) | ✓ (Shift/Ctrl-click in Vertices table) |
| Move selection as a group | ✓ | ✓ | ✓ ("Move Selected…" button → ECMD_SKETCH_MOVE_VERTEX_LIST) |
| Delete selection as a group | ✓ | ✓ | — |

---

## 5. View Controls

| Feature | MGED | Archer | New Qt widget |
|---------|:----:|:------:|:-------------:|
| Zoom in / zoom out buttons | ✓ | ✓ | — (QgView handles navigation separately) |
| Scroll canvas (X/Y scrollbars) | ✓ | ✓ | — (QgView pan/zoom) |
| Scale view (Ctrl+Shift+drag) | — | ✓ | — |
| Translate view (Shift+drag) | — | ✓ | — |
| Redraw / refresh button | ✓ | ✓ | automatic on each edit |
| Fit-to-window (auto-scale) | ✓ | — | ✓ (`F` key / View menu) |

---

## 6. Grid and Snap

| Feature | MGED | Archer | New Qt widget |
|---------|:----:|:------:|:-------------:|
| Draw background grid | — | ✓ | ✓ (`H` key / View → Show Grid; uses existing `dm_draw_grid` via `gv_grid.draw`) |
| Snap to grid | — | ✓ | ✓ (View → Grid Settings… enables `gv_grid.snap`; `bv_snap_grid_2d` is called by `rt_edit_snap_point`) |
| Configurable major/minor grid spacing | — | ✓ | ✓ (View → Grid Settings… dialog: res_h, res_v, res_major_h, res_major_v) |
| Configurable grid anchor point | — | ✓ | — (`gv_grid.anchor` field exists but not yet exposed) |
| Snap to other sketch vertices | — | ✓ (do_snap_sketch) | — |
| Configurable pick tolerance (pixels) | — | ✓ | — (proximity uses view scale) |

---

## 7. Live Coordinate Feedback

| Feature | MGED | Archer | New Qt widget |
|---------|:----:|:------:|:-------------:|
| Live X/Y cursor position display | ✓ | — | ✓ (QgSketchCursorTracker → status bar UV label) |
| Live radius display during arc creation | ✓ | — | — |
| Status / hint text line | ✓ | ✓ (read-only label) | ✓ (QStatusBar) |
| Editable X/Y coordinate entry (type to move) | ✓ | — | — |
| Editable radius entry | ✓ | — | — |

---

## 8. Persistence and Undo

| Feature | MGED | Archer | New Qt widget |
|---------|:----:|:------:|:-------------:|
| Save to database (rename on save) | ✓ | ✓ | ✓ (Ctrl+S) |
| Undo / revert | — | — | ✓ (rt_edit_checkpoint/rt_edit_revert, Ctrl+Z) |
| Reset to original (full revert) | ✓ | — | — |

---

## 9. Contour Management

| Feature | MGED | Archer | New Qt widget |
|---------|:----:|:------:|:-------------:|
| Multi-contour sketches | ✓ (Escape starts new contour) | ✓ (Escape starts new contour) | ✓ (`N` key: chains from last vertex OR starts new vertex for new contour) |
| Per-contour reverse flag | ✓ (via CARC reverse) | ✓ | — |
| Sketch plane (V, A, B) edit | — | ✓ | ✓ (Edit → Sketch Plane… / ECMD_SKETCH_SET_PLANE) |

---

## 10. Keyboard Shortcuts

| Key | MGED | Archer | New Qt widget |
|-----|:----:|:------:|:-------------:|
| `l` — line mode | — | ✓ | ✓ |
| `c` — circle mode | — | ✓ | — |
| `a` — arc mode | — | ✓ | — |
| `b` — Bezier mode | — | ✓ | ✓ |
| `m` — move mode | — | ✓ | — |
| `d` / Delete / BSpace — delete selected | — | ✓ | ✓ (Delete) |
| `Escape` — new contour / cancel | — | ✓ | ✓ (cancel only) |
| `P` — pick vertex | — | — | ✓ |
| `M` — move vertex | — | — | ✓ |
| `A` — add vertex | — | — | ✓ |
| `S` — pick segment | — | — | ✓ |
| `G` — move segment | — | — | ✓ |
| `L` — add line | — | — | ✓ |
| `R` — add arc (dialog) | — | — | ✓ |
| `C` — flip arc complement | — | — | ✓ (ECMD_SKETCH_TOGGLE_ARC_ORIENT) |
| `I` — interactive arc radius drag | — | — | ✓ (QgSketchArcRadiusFilter) |
| `T` — set arc tangency | — | — | ✓ (QgSketchSetTangencyFilter + ECMD_SKETCH_SET_TANGENCY) |
| `N` — chain / new contour | — | — | ✓ |
| `H` — toggle grid display | — | — | ✓ |
| `F` — fit view to sketch | — | — | ✓ |
| `Enter` — commit line/Bezier | — | — | ✓ |
| `Ctrl+S` — save | — | — | ✓ |
| `Ctrl+Z` — undo | — | — | ✓ |

---

## 11. Debug / Diagnostics

| Feature | MGED | Archer | New Qt widget |
|---------|:----:|:------:|:-------------:|
| "Describe All Segments" (text dump) | ✓ | — | ✓ (Debug menu, scrollable dialog) |
| Live vertex/segment tables (always visible) | — | — | ✓ |
| Highlight selected elements in 3-D view | — | ✓ (data_axes / data_lines) | — |

---

## 12. Architectural Differences

| Property | MGED | Archer | New Qt widget |
|----------|------|--------|---------------|
| Toolkit | Tk canvas (2-D) | Tk canvas (2-D) | Qt + QgView (3-D, OpenGL) |
| Editing backend | Direct Tcl manipulation of sketch data | Direct Tcl manipulation of sketch data | `librt` `rt_edit` API (`ECMD_SKETCH_*`) |
| View type | Dedicated 2-D canvas window | Dedicated 2-D canvas window inside panel | 3-D BRL-CAD view, face-on |
| Database units | Handled in Tcl | Handled in Tcl | `local2base` / `base2local` via `rt_edit` |
| Integration target | Standalone MGED window | Archer panel | `qged` panel (currently standalone demo) |
| NURB support | No | No | Yes (ECMD 26012–26014) |
| Segment split | No | No | Yes (ECMD 26011) |
| Multi-level undo | No | No | Yes (`rt_edit_checkpoint` / `rt_edit_revert`) |

---

## 13. Remaining Gaps (Minor / Low Priority)

The following features from MGED/Archer remain unimplemented.  All 10 original
gaps have now been addressed; the items below are secondary or architectural:

1. **Grid anchor point** — the `gv_grid.anchor` field is not yet exposed in the
   Grid Settings dialog.  Grid snap and drawing are functional; anchor is always
   at the origin.

2. **Snap to sketch vertices** — Archer's `do_snap_sketch` snaps to existing
   vertex positions.  `bv_snap_lines_2d` would need per-vertex vlist objects
   registered in `gv_s->gv_snap_objs`.

3. **Per-contour reverse flag** — each contour (disconnected curve) can have a
   reverse orientation.  The new widget does not yet expose this per-contour.

4. **Editable coordinate entry** — MGED allows typing U/V coordinates directly
   to position a vertex.  The new widget shows coordinates in the status bar but
   does not accept typed coordinate input.

---

## 14. Closed Gaps (All 10 Original Gaps Addressed)

| Gap | Resolution |
|-----|-----------|
| Arc complement toggle | `ECMD_SKETCH_TOGGLE_ARC_ORIENT` (26016) + `C` key / "Flip Arc" toolbar button |
| Interactive arc radius drag | `ECMD_SKETCH_SET_ARC_RADIUS` (26017) + `QgSketchArcRadiusFilter` + `I` key |
| Arc tangency | `ECMD_SKETCH_SET_TANGENCY` (26018) + `QgSketchSetTangencyFilter` + `T` key (optional angle offset via dialog) |
| Live UV cursor display | `QgSketchCursorTracker` emits `uv_moved`; status bar shows "U: … V: …" |
| Multi-select move | Vertex table uses `ExtendedSelection`; "Move Selected…" button calls `ECMD_SKETCH_MOVE_VERTEX_LIST` |
| Fit-to-window | `F` key / View menu computes vertex bounds and adjusts `gv_scale` |
| Describe all segments | Debug menu shows scrollable text dump with all vertex and segment details |
| Grid drawing + snap | `H` key toggles `gv_grid.draw`; View → Grid Settings dialog configures `gv_grid.snap`, spacing, major-lines; `dm_draw_grid` called via existing `dm_draw_faceplate` pipeline |
| Multi-contour / segment chaining | `N` key: chains from last accumulated vertex, or adds new vertex for a new disconnected contour |
| Sketch plane parameters (V, A, B) | `ECMD_SKETCH_SET_PLANE` (26019) + Edit → Sketch Plane… dialog exposes origin V, u_vec A, v_vec B with Gram-Schmidt orthogonalisation |
