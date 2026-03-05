# BSG Modernization TODO
## Propagating the Phase 2 BSG API Throughout BRL-CAD

This document is the output of a survey of every BRL-CAD library, command and
program that consumes the `bv_*` / `bsg_*` scene-graph API.  For each consumer
the items that need to change are listed, together with the relevant Phase 2
BSG facility they should migrate to and the source files affected.

The survey was conducted against commit `e012926b` on the
`copilot/update-bsg-apis-for-obol` branch, which introduced the Phase 2 API
additions documented in `include/bsg/defines.h` and `include/bsg/util.h`.

---

## Priority legend

| Tag | Meaning |
|-----|---------|
| **P1** | Required for Obol integration correctness |
| **P2** | Clean up / user-feature parity preserved |
| **P3** | Nice-to-have / technical debt |

---

## 1. `libdm` — Display Manager

### 1.1 `src/libdm/view.c`  **[P1]** – Replace flat-table render loop with graph traversal

**Current pattern** (repeated ~8 times in the file):
```c
struct bu_ptbl *db_objs = bsg_view_shapes(v, BSG_DB_OBJS);
for (size_t i = 0; i < BU_PTBL_LEN(db_objs); i++) {
    bsg_group *g = (bsg_group *)BU_PTBL_GET(db_objs, i);
    dm_draw_solid(dmp, g, ...);
}
// same for BSG_VIEW_OBJS, BSG_LOCAL_OBJS …
```

**Target pattern**:
```c
bsg_view_traverse(v, dm_draw_visitor, dmp);
```

A single `dm_draw_visitor` callback receives the accumulated
`bsg_traversal_state` (with correct `xform`, `material`, and `active_camera`)
and dispatches to the existing draw helpers.  The four flat loops (db_objs,
local_db_objs, view_objs, local_view_objs) collapse into one graph walk.

**Also needed in this file**:
- Replace the direct `v->gv_model2view` / `v->gv_perspective` / `v->gv_pmat`
  reads with reads from `state->active_camera` (obtained via
  `bsg_view_get_camera()`).  This makes the render loop independent of the
  legacy `bview` camera fields.
- Lines 789–795: `matp_t mat = v->gv_model2view; dm_loadpmatrix(dmp, v->gv_pmat)`
  should become: `bsg_view_get_camera(v, &cam); dm_loadpmatrix(dmp, cam.pmat)`.

### 1.2 `src/libdm/dm-gl.c`  **[P1]** – Replace `dl_head_scene_obj` list walk

`dl_head_scene_obj` is the legacy linked-list on a `display_list` struct.
Line 1549:
```c
for (BU_LIST_FOR(sp, bsg_shape, &obj->dl_head_scene_obj)) { … }
```
This is a holdover from before `bsg_shape` children were stored in
`BU_PTBL`-based `children` tables.  Migrate to child-table iteration or,
better, expose the shapes through the scene-root graph and use
`bsg_view_traverse`.

### 1.3 `src/libdm/dm-gl_lod.cpp`  **[P1]** – Replace `s_dlist_stale` polling with sensors

Six locations poll or clear `s->s_dlist_stale` directly:
```c
if (s->s_dlist_stale) { /* rebuild display list */ s->s_dlist_stale = 0; }
```
**Target**: Register a `bsg_sensor` at display-list creation time that fires
`gl_rebuild_dlist(s, dmp)` whenever `bsg_shape_stale(s)` is called.  The raw
`s_dlist_stale` flag can remain as a fallback but primary notification should
flow through the sensor system.

**Also in this file**: The `BSG_NODE_MESH_LOD` branch (lines 545–546) reads
`(bsg_lod *)s->draw_data` directly.  Once LoD group nodes
(`BSG_NODE_LOD_GROUP`) are fully adopted, leaf-shape LoD can be preserved but
the driver should also handle the case where the shape *is* a LoD group node —
in that case the traversal engine already selected the right child so the draw
callback just draws it without any special LoD logic.

### 1.4 `src/libdm/dm-gl_lod.cpp` **[P2]** – Replace inline `gv_model2view` reads

Lines 164–165, 200–201, etc.:
```c
MAT_COPY(save_mat, s->s_v->gv_model2view);
bn_mat_mul(draw_mat, s->s_v->gv_model2view, s->s_mat);
```
These should use the `xform` accumulated in `bsg_traversal_state` passed by
`bsg_view_traverse`, so that ancestor separator transforms are included.
`draw_mat = state->xform * s->s_mat` is the correct formulation once the
caller drives via `bsg_view_traverse`.

### 1.5 `src/libdm/swrast/fb-swrast.cpp` **[P2]** – Add scene-root initialisation

Line 390 calls `bsg_view_init(qi->mw->canvas->v, NULL)` without subsequently
calling `bsg_scene_root_create()`.  Add:
```c
bsg_view_init(qi->mw->canvas->v, NULL);
bsg_scene_root_create(qi->mw->canvas->v);
```

---

## 2. `libged` — Geometry Editing Library

### 2.1 `src/libged/ged.cpp` / `include/ged/defines.h`  **[P1]** – Add scene-root creation in GED init

`libged/ged.cpp` lines 112–125 initialise the GED instance:
```c
bsg_scene_init(&gedp->ged_views);
bsg_view_init(gedp->ged_gvp, &gedp->ged_views);
bsg_scene_add_view(&gedp->ged_views, gedp->ged_gvp);
```
Add after `bsg_view_init`:
```c
bsg_scene_root_create(gedp->ged_gvp);
```
Each additional view created in `libged/dm/dm.c` (line 582) should also call
`bsg_scene_root_create(target_view)` after `bsg_view_init`.

### 2.2 `src/libged/display_list.c`  **[P1]** – Migrate `dl_head_scene_obj` linked list to scene-graph tree

The `display_list` struct maintains a legacy `bu_list`-based
`dl_head_scene_obj` linked list (~20 uses).  Shapes added via
`display_list_add_scene_obj()` (line 133) should instead be added as children
of the per-view scene root returned by `bsg_scene_root_get(v)`.

Callers that iterate the list (`BU_LIST_FOR(sp, bsg_shape, &gdlp->dl_head_scene_obj)`)
should migrate to `bsg_view_traverse`.  The display list struct itself may
then become a lightweight container that holds only the name / DB path without
the shape list.

### 2.3 `src/libged/draw.cpp`  **[P1]** – Feed stale notifications through sensors

Lines 173, 381, 532:
```c
bsg_shape_stale(vo);
```
This already calls the Phase 2 `bsg_shape_stale` which fires sensors.  The
callers should register a sensor on newly created view objects that schedules a
re-render (`bsg_shape_add_sensor(s, schedule_redraw_cb, dmp)`) so renderers
do not need to poll.

### 2.4 `src/libged/draw/draw2.cpp` **[P1]** – Replace flat-table emptiness check with scene-root check

Lines 209–214:
```c
struct bu_ptbl *dobjs  = bsg_view_shapes(cv, BSG_DB_OBJS);
struct bu_ptbl *vobjs  = bsg_view_shapes(cv, BSG_VIEW_OBJS);
if ((!dobjs || !BU_PTBL_LEN(dobjs)) && …)  /* view is empty */
```
Replace with:
```c
bsg_shape *root = bsg_scene_root_get(cv);
if (!root || BU_PTBL_LEN(&root->children) == 0) /* view is empty */
```

### 2.5 `src/libged/draw/loadview.cpp`, `preview.cpp` **[P2]** – Use `bsg_view_set_camera` instead of direct field writes

`loadview.cpp` lines 170–171, `preview.cpp` lines 116–117:
```c
MAT_COPY(gedp->ged_gvp->gv_rotation, (*m));
MAT_DELTAS_VEC_NEG(gedp->ged_gvp->gv_center, (*v));
bsg_view_update(gedp->ged_gvp);
```
Replace with:
```c
struct bsg_camera cam;
bsg_view_get_camera(gedp->ged_gvp, &cam);
MAT_COPY(cam.rotation, (*m));
MAT_DELTAS_VEC_NEG(cam.center, (*v));
bsg_view_set_camera(gedp->ged_gvp, &cam);
/* Also update the camera node in the scene root */
bsg_shape *cam_node = /* first child of scene root */;
if (cam_node && (cam_node->s_type_flags & BSG_NODE_CAMERA))
    bsg_camera_node_set(cam_node, &cam);
bsg_view_update(gedp->ged_gvp);
```

### 2.6 `src/libged/view/*.cpp` **[P2]** – Replace flat-table loops with `bsg_view_traverse`

Files `view/objs.cpp`, `view/gobjs.cpp`, `view/view.c`, `view/polygons.c`
each contain 4–6 flat-loop blocks over `bsg_view_shapes(...)`.  All should be
unified under a single `bsg_view_traverse` visitor that dispatches based on
`node->s_type_flags`.

### 2.7 `src/libged/adc/adc.c` **[P2]** – Use camera accessor instead of inline field reads

~15 occurrences of `gedp->ged_gvp->gv_model2view`, `gv_view2model`:
```c
MAT4X3PNT(view_pt, gvp->gv_model2view, model_pt);
```
Replace with:
```c
struct bsg_camera cam;
bsg_view_get_camera(gvp, &cam);
MAT4X3PNT(view_pt, cam.model2view, model_pt);
```

### 2.8 `src/libged/eye_pos/eye_pos.c`, `brep/pick.cpp`, `brep/tikz.cpp`, etc. **[P2]**

Each of these reads one or more camera fields directly from `gedp->ged_gvp`.
All should go through `bsg_view_get_camera()`.  The list of affected files:
- `eye_pos/eye_pos.c` — `gv_eye_pos`, `gv_pmat`
- `brep/pick.cpp` — `gv_center`, `gv_rotation`
- `brep/tikz.cpp` — `gv_aet`
- `check/check.c` — `gv_rotation`
- `get_eyemodel/get_eyemodel.c` — `gv_rotation`
- `illum/illum.c` — `gv_rotation`
- `keypoint/keypoint.c` — `gv_keypoint`
- `dm/ert.cpp` — `gv_perspective`
- `ged_util.cpp` — `gv_perspective`, `gv_view2model`, `gv_center`, `gv_rotation`, `gv_coord`
- `arot/arot.c` — `gv_coord`
- `grid/grid.c` — `gv_center`, `gv_model2view`, `gv_view2model`
- `m2v_point/m2v_point.c` — `gv_model2view`
- `metaball/metaball.c` — `gv_model2view`, `gv_view2model`
- `bot/edbot.c` — `gv_model2view`

### 2.9 `src/libged/view/lod.cpp` **[P1]** – Add `BSG_NODE_LOD_GROUP` support

The LoD GED command (`view lod`) currently only manages the `bsg_lod`
(BSG_NODE_MESH_LOD) payload on leaf shapes.  Add commands to:
- Create a `BSG_NODE_LOD_GROUP` node (`bsg_lod_group_alloc`).
- Add/remove detail-level children.
- Set/query switch distances.

### 2.10 `src/libged/dbi_state.cpp` **[P2]** – Replace `bsg_shape_stale` calls with sensor scheduling

Lines 2277–2295 call `bsg_shape_stale(sp)` directly after database state
changes.  This already fires sensors (Phase 2).  Future work: rather than
each caller calling `bsg_shape_stale`, register a single database-change
sensor (`bsg_shape_add_sensor`) at draw time that handles re-generation.

---

## 3. `libqtcad` — Qt CAD Library

### 3.1 `src/libqtcad/QgGL.cpp` **[P1]** – Replace direct `gv_aet` writes with camera API

Lines 60–105 use a pattern repeated for every standard view:
```c
bn_decode_vect(v->gv_aet, "35 -25 0");
bsg_view_mat_aet(v);
bsg_view_update(v);
```
Replace with:
```c
struct bsg_camera cam;
bsg_view_get_camera(v, &cam);
bn_decode_vect(cam.aet, "35 -25 0");
/* recompute rotation from aet */
bsg_view_set_camera(v, &cam);
bsg_view_update(v);
/* propagate to camera node if scene root exists */
bsg_shape *root = bsg_scene_root_get(v);
if (root && BU_PTBL_LEN(&root->children) > 0) {
    bsg_shape *cn = (bsg_shape *)BU_PTBL_GET(&root->children, 0);
    if (cn->s_type_flags & BSG_NODE_CAMERA) bsg_camera_node_set(cn, &cam);
}
```

Lines 391–405 also write `v->gv_aet` and `v->gv_rotation` directly — same fix.

### 3.2 `src/libqtcad/QgGL.cpp` line 52–53 **[P1]** – Add scene-root creation

```c
BU_GET(local_v, bsg_view);
bsg_view_init(local_v, NULL);
// ADD:
bsg_scene_root_create(local_v);
```

### 3.3 `src/libqtcad/QgModel.cpp` **[P2]** – Use scene-root per empty view

Lines 323–325 create a temporary empty view for LoD key operations.
Although the view is short-lived, calling `bsg_scene_root_create` on it
would make it well-formed from the start:
```c
BU_GET(empty_gvp, bsg_view);
bsg_view_init(empty_gvp, &gedp->ged_views);
bsg_scene_root_create(empty_gvp);  /* ADD */
bsg_scene_add_view(&gedp->ged_views, empty_gvp);
```

### 3.4 `src/libqtcad/QgQuadView.cpp`, `QgView.cpp` **[P2]** – Add scene-root creation and traversal trigger

The quad-view setup creates multiple views.  Each should call
`bsg_scene_root_create(v)` after `bsg_view_init`.  The per-frame repaint
should use `bsg_view_traverse` rather than flat-table access.

### 3.5 `src/libqtcad/QgSelectFilter.cpp`, `QgMeasureFilter.cpp` **[P2]** – Use `bsg_view_shapes` → scene-root lookup

These mouse-event filters call `bsg_view_shapes(v, …)` to find the shapes
under the cursor.  Once the scene root is established, they can use
`bsg_view_traverse` with a pick visitor instead.

---

## 4. `qged` — Qt GED Application

### 4.1 `src/qged/QgEdApp.cpp` **[P2]** – Trigger redraws via sensors instead of manual view iteration

Lines 332–345 iterate all views in `ged_views` and call `bv_it->first->redraw(…)`.
Register a `bsg_sensor` on the scene root (or on `ged_views`) so that any
shape mutation automatically schedules a repaint via Qt's update mechanism:
```c
bsg_shape_add_sensor(scene_root, schedule_qt_repaint, widget);
```

### 4.2 `src/qged/plugins/polygon/*.cpp` **[P2]** – Replace flat-table polygon searches with typed node queries

Multiple `QPolyCreate.cpp` / `QPolyMod.cpp` functions iterate
`bsg_view_shapes(v, BSG_VIEW_OBJS)` searching for `BSG_NODE_POLYGONS` nodes.
Introduce a typed-query traversal helper:
```c
/* Proposed helper */
BSG_EXPORT void bsg_view_find_by_type(bsg_view *v,
    unsigned long long type_flag,
    struct bu_ptbl *result);
```
That helper would be a thin wrapper around `bsg_view_traverse` that collects
all nodes whose `s_type_flags & type_flag` is non-zero.

### 4.3 `src/qged/plugins/edit/ell/QEll.cpp` **[P2]** – Use `BSG_NODE_SEPARATOR` for label group

Lines 175–227 create a shape and add label children.  Wrap the group in a
`BSG_NODE_SEPARATOR` node so that label-specific state does not leak to
adjacent objects:
```c
bsg_shape *sep = bsg_node_alloc(BSG_NODE_SEPARATOR);
/* add label shapes as children of sep */
/* add sep to view as child of scene root */
```

### 4.4 `src/qged/plugins/view/info/CADViewModel.cpp` **[P2]**

Reads camera-related fields from `bsg_view` directly (gv_aet, gv_scale, etc.).
Should go through `bsg_view_get_camera()` for camera fields and the existing
`bsg_view` accessors for non-camera fields.

---

## 5. `mged` — Classic Interactive Editor

### 5.1 `src/mged/attach.c` lines 720–724 **[P1]** – Remove manual `gv_objs` initialisation

```c
BU_GET(view_state->vs_gvp->gv_objs.db_objs, struct bu_ptbl);
bu_ptbl_init(view_state->vs_gvp->gv_objs.db_objs, 8, "view_objs init");
BU_GET(view_state->vs_gvp->gv_objs.view_objs, struct bu_ptbl);
bu_ptbl_init(view_state->vs_gvp->gv_objs.view_objs, 8, "view_objs init");
```
This bypasses `bsg_view_init`.  Remove the manual init and call
`bsg_view_init(view_state->vs_gvp, &ged_set)` followed by
`bsg_scene_root_create(view_state->vs_gvp)`.

### 5.2 `src/mged/chgtree.c`, `chgview.c`, `buttons.c`, `cmd.c` **[P1]** – Replace `dl_head_scene_obj` iteration

Multiple loops iterate `gdlp->dl_head_scene_obj` (`BU_LIST_FOR(sp, bsg_shape, …)`).
These are the same legacy list patterns as in `libged/display_list.c` and
should migrate to tree traversal once `display_list` is refactored.

### 5.3 `src/mged/cmd.c` struct `_view_cache` **[P2]** – Migrate camera cache to `bsg_camera`

Lines 2236–2257:
```c
struct _view_cache {
    fastf_t gv_perspective;
    vect_t  gv_aet;
    mat_t   gv_model2view;
    bsg_knobs k;
    …
};
```
This is a manual snapshot of `bsg_camera` fields.  Replace with:
```c
struct _view_cache {
    struct bsg_camera camera;
    bsg_knobs         k;
};
```
Use `bsg_view_get_camera` / `bsg_view_set_camera` for save/restore.

### 5.4 `src/mged/set.c` lines 459–461 **[P3]** – Remove direct `s_dlist` range arithmetic

```c
BU_LIST_FIRST(bv_scene_obj, &gdlp->dl_head_scene_obj)->s_dlist,
BU_LIST_LAST(bv_scene_obj, &gdlp->dl_head_scene_obj)->s_dlist - … + 1
```
This is a GL display-list range calculation that assumes a contiguous list.
Once shapes are managed through the scene-graph tree, this calculation should
use the display-list IDs registered in the node and be driven by a sensor
callback.

### 5.5 `src/mged/chgview.c` **[P2]** – Use camera node for view preset storage

The `_view_cache_save` helper (cmd.c line 2261) and related preset-view code
copies camera fields to local variables.  Save/restore should instead snapshot
a `bsg_camera_node_alloc(v)` and restore via `bsg_view_set_camera`.

---

## 6. `libtclcad` — Tcl CAD Bindings

### 6.1 `src/libtclcad/commands.c` **[P2]** – Replace inline `gv_model2view` reads

~20 occurrences of `gdvp->gv_model2view` used for coordinate transformation.
All should use `bsg_view_get_camera(gdvp, &cam)` and `cam.model2view`.

### 6.2 `src/libtclcad/commands.c` — `bsg_scene_views` loop **[P3]**

Lines 1100–1103 iterate all views via `bsg_scene_views`.  Once sensors are
established, view-iteration-based redraws should be replaced by sensor
callbacks on the scene root.

---

## 7. `librt` — Ray Trace Library (primitive editors)

### 7.1 `src/librt/edit.cpp`, `src/librt/primitives/*/ed*.c` **[P2]** – Use camera accessor in primitive editors

~40 occurrences across `edit.cpp`, `edarb.c`, `edars.c`, `edbot.c`,
`edbspline.c`, `edcline.c`, `edextrude.c`, `edmetaball.c`, `ednmg.c`,
`edgeneric.c` read `s->vp->gv_model2view`, `gv_view2model`,
`gv_rotation`, `gv_center`, `gv_rotate_about` directly.

Replace all with `bsg_view_get_camera(s->vp, &cam)` then use `cam.*`.

---

## 8. `gtools/gsh` — GED Shell

### 8.1 `src/gtools/gsh/gsh.cpp` line 593 **[P2]**

```c
matp_t mat = gedp->ged_gvp->gv_model2view;
```
Replace with:
```c
struct bsg_camera cam;
bsg_view_get_camera(gedp->ged_gvp, &cam);
matp_t mat = cam.model2view;
```

---

## 9. New BSG API functions needed to support the migration

These helpers do not yet exist and should be added to `bsg/util.h` /
`scene_graph.cpp` as the above migration work proceeds.

| Function | Purpose |
|----------|---------|
| `bsg_view_find_by_type(v, flags, result_ptbl)` | Collect all nodes of a given type under a view root (Issue 4.2) |
| `bsg_scene_root_camera(v)` | Convenience: return the `BSG_NODE_CAMERA` child of a view's scene root |
| `bsg_view_mat_aet_camera(cam)` | Like `bsg_view_mat_aet` but operates on a `bsg_camera` struct directly |
| `bsg_sensor_fire(root, type_mask)` | Walk the sub-tree of `root` and fire sensors on all nodes matching `type_mask` |
| `bsg_dm_draw_visitor` | Standard draw visitor callback for `libdm` to pass to `bsg_view_traverse` |

---

## 10. Summary table

| Subsystem | Issues addressed | P1 items | P2 items | P3 items |
|-----------|-----------------|----------|----------|----------|
| `libdm/view.c` | 1,2,3,4 | 2 | 1 | 0 |
| `libdm/dm-gl.c` | 1,3 | 1 | 0 | 0 |
| `libdm/dm-gl_lod.cpp` | 3,4,5 | 1 | 1 | 0 |
| `libdm/swrast` | 2 | 0 | 1 | 0 |
| `libged/ged.cpp` | 2 | 1 | 0 | 0 |
| `libged/display_list.c` | 1,3 | 1 | 0 | 0 |
| `libged/draw.cpp` | 4 | 1 | 0 | 0 |
| `libged/draw/draw2.cpp` | 3 | 1 | 0 | 0 |
| `libged/draw/loadview,preview` | 1 | 0 | 1 | 0 |
| `libged/view/*.cpp` | 3 | 0 | 1 | 0 |
| `libged/adc/adc.c` | 1 | 0 | 1 | 0 |
| `libged/eye_pos` etc. | 1 | 0 | 1 | 0 |
| `libged/view/lod.cpp` | 5 | 1 | 0 | 0 |
| `libged/dbi_state.cpp` | 4 | 0 | 1 | 0 |
| `libqtcad/QgGL.cpp` | 1,2 | 1 | 1 | 0 |
| `libqtcad/QgModel.cpp` | 2 | 0 | 1 | 0 |
| `libqtcad/QgQuadView` etc. | 2,3 | 0 | 1 | 0 |
| `qged/QgEdApp.cpp` | 4 | 0 | 1 | 0 |
| `qged/plugins/polygon` | 3 | 0 | 1 | 0 |
| `qged/plugins/edit/ell` | 2 | 0 | 1 | 0 |
| `mged/attach.c` | 2 | 1 | 0 | 0 |
| `mged/chgtree,chgview,buttons,cmd` | 1,3 | 1 | 0 | 0 |
| `mged/cmd.c _view_cache` | 1 | 0 | 1 | 0 |
| `mged/set.c` | 4 | 0 | 0 | 1 |
| `libtclcad/commands.c` | 1 | 0 | 1 | 1 |
| `librt/edit.cpp` + prim eds | 1 | 0 | 1 | 0 |
| `gtools/gsh/gsh.cpp` | 1 | 0 | 1 | 0 |

**P1 total**: ~10 items, all blocking Obol integration  
**P2 total**: ~20 items, clean up and future-proof  
**P3 total**: 3 items, cosmetic / debt

---

## 11. Suggested work order

1. **Phase 2a** (unblocking correctness):
   - Add `bsg_scene_root_create` calls at all view-init sites (ged.cpp, attach.c, QgGL.cpp, swrast).
   - Add `bsg_view_set_camera` to `loadview.cpp`, `preview.cpp` to keep camera nodes in sync.
   - Migrate `libdm/view.c` render loop to `bsg_view_traverse` using a new `bsg_dm_draw_visitor`.

2. **Phase 2b** (sensor system):
   - Register display-list rebuild sensors in `dm-gl_lod.cpp`.
   - Register repaint sensors from `libqtcad`'s view widget.
   - Remove remaining raw `s_dlist_stale` reads outside `libbv`.

3. **Phase 2c** (camera field accessor hygiene):
   - Replace all direct `gv_*` camera field reads with `bsg_view_get_camera` /
     `bsg_view_set_camera` throughout `libged`, `librt`, `libtclcad`, `mged`,
     `gtools`.

4. **Phase 2d** (LoD group nodes):
   - Add `BSG_NODE_LOD_GROUP` support to `libged/view/lod.cpp`.
   - Handle `BSG_NODE_LOD_GROUP` in `dm-gl_lod.cpp` draw path.

5. **Phase 2e** (display_list decommission):
   - Migrate `libged/display_list.c` shapes to scene-root children.
   - Remove `dl_head_scene_obj` linked list from `display_list` struct.
   - Update `mged` callers.

---

*Last updated: 2026-03-05 (generated from survey of `copilot/update-bsg-apis-for-obol` branch)*
