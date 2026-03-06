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

1. **Phase 2a** (unblocking correctness): ✅ COMPLETE
   - Add `bsg_scene_root_create` calls at all view-init sites (ged.cpp, attach.c, QgGL.cpp, swrast).
   - Add `bsg_view_set_camera` to `loadview.cpp`, `preview.cpp` to keep camera nodes in sync.
   - Migrate `libdm/view.c` render loop to `bsg_view_traverse` using a new `bsg_dm_draw_visitor`.

2. **Phase 2b** (sensor system): ✅ PARTIAL
   - ✅ Register display-list rebuild sensors in `dm-gl_lod.cpp` (`gl_register_dlist_sensor` / `gl_deregister_dlist_sensor` + `gl_dlist_stale_cb`).
   - Register repaint sensors from `libqtcad`'s view widget. (future work)
   - ✅ Remaining raw `s_dlist_stale` reads are intentional fallbacks in dm-gl_lod.cpp; removed where sensors now handle stale notification.

3. **Phase 2c** (camera field accessor hygiene): ✅ COMPLETE (session 5: zero direct gv_* uses outside libbv)
   - Replaced ALL direct `gv_*` camera field reads/writes with `bsg_view_get_camera` /
     `bsg_view_set_camera` throughout `libged`, `librt`, `libtclcad`, `mged`,
     `libdm`, `libqtcad`, `gtools`. No non-libbv file accesses camera fields directly.
   - Session 5 additions: `view/eye.c`, `view/align.c` missing `bsg_view_set_camera` bug fixes;
     `view/lookat.c`, `view/center.cpp`, `view/ypr.c`, `view/qvrot.c`, `view/quat.c`,
     `view/autoview.c`, `view/saveview.c`, `view/viewdir.c`, `view/view.c`, `view/labels.c`,
     `view/knob.c`, `rot/rotate_about.c`, `keypoint.c`, `orient.c`, `setview.c`,
     `rtwizard.c`, `nirt.cpp`, `pipe.c`, `draw/preview.cpp`, `draw/loadview.cpp`,
     `rt/rt.c`, `dm/ert.cpp`, `grid2model_lu.c`, `grid2view_lu.c`, `view2grid_lu.c`,
     `model2view.c`, `model2view_lu.c`, `view2model_lu.c`, `v2m_point.c`,
     `move_arb_edge.c`, `rot_point.c`, `nmg.c`, `bot/edbot.c`, `plot/plot.c`,
     `ps/ps.c`, `png/png.c`, `draw.cpp`, `tests/draw/aet.cpp`;
     `mged/edsol.c`; `libdm/view.c`; `libqtcad/bindings.cpp`; `librt/tests/edit/tor.cpp`.

4. **Phase 2d** (LoD group nodes): ✅ COMPLETE
   - ✅ Add `BSG_NODE_LOD_GROUP` support to `libged/view/lod.cpp` (group create/add/rm/distances subcommands).
   - ✅ Handle `BSG_NODE_LOD_GROUP` in `dm-gl_lod.cpp` draw path (`gl_draw_obj` selects child by eye-to-model-center distance).

5. **Phase 2e** (display_list decommission): ✅ COMPLETE (session 6)
   - ✅ Migrate `libged/display_list.c` shapes to scene-root children.
   - ✅ Remove `dl_head_scene_obj` linked list from `struct display_list` (tcl_data.h).
   - Update `mged` callers. (**COMPLETE**: buttons.c, chgtree.c, chgview.c, cmd.c,
     edsol.c, plot.c, rtif.c, usepen.c, set.c migrated to `root->children`.)
   - Update `libged` callers. (**COMPLETE**: zap.c, illum.c, solid_report.c,
     ged_util.cpp, how.c, nirt.cpp, select.c, set_transparency.c, rtcheck.c,
     ps.c, png.c, nmg.c, view/objs.cpp, plot/plot.c, bot/dump/bot_dump.cpp,
     libtclcad/view/draw.c migrated.)
   - **Phase 2e session 4 additions**:
     - ✅ `display_list.c`: correctness — `bu_ptbl_rm` in all shape-freeing paths
     - ✅ `display_list.c`: dual-write — `bu_ptbl_ins` at `invent_solid` insertion site (line 839)
     - ✅ `draw/draw.c`: dual-write at both `dl_add_path` (line 131) and `append_solid_to_display_list` (line 424)
     - ✅ `dodraw.c`: dual-write at line 159
     - ✅ BSG reader helpers added: `bsg_bounding_sph`, `bsg_color_soltab`, `bsg_set_iflag`, `dl_name_hash`, `ged_find_shapes_by_path`
     - ✅ `vutil.c:ged_dl_hash` and `scene_graph.cpp:bsg_dl_hash` use root->children (legacy fallback kept)
     - ✅ `dm-generic.c`: `dm_draw_bsg_view()` added as BSG version of `dm_draw_head_dl`
     - ✅ `dm-gl.c:gl_draw_display_list`: uses root->children when available (legacy dl_head_scene_obj fallback kept)
     - ✅ `mged/dozoom.c:createDListAll`: uses root->children when available (legacy fallback kept)
     - ✅ `libtclcad/commands.c:to_create_vlist_callback`: uses root->children when available (legacy fallback kept)
     - ✅ `mged/dozoom.c`, `gtools/gsh/gsh.cpp`: render loops use `dm_draw_bsg_view` with legacy fallback
     - ✅ `libged/tests/test_gqa.c`: migrated to root->children (legacy fallback kept)
   - **Phase 2e session 6 additions (FINAL DECOMMISSION)**:
     - ✅ `bv/tcl_data.h`: removed `dl_head_scene_obj` field from `struct display_list`
     - ✅ `display_list.c`: added `dl_match_shapes`, `dl_gdlp_shapes`, `dl_free_shape` helpers
     - ✅ `display_list.c`: rewrote all shape erasure/free functions to use `root->children`
     - ✅ `display_list.c`: rewrote `headsolid_split`/`headsolid_splitGDL` (new `gedp` signature, path-based split)
     - ✅ Removed all dual-write: `invent_solid`, `draw/draw.c`, `dodraw.c`
     - ✅ All remaining `dl_head_scene_obj` sites cleaned up in 10+ files
     - ✅ `bsg_scene_fsos()` + `bsg_view_center_linesnap()` BSG wrappers added to `bsg/util.h`
     - ✅ `bsg/compat.h` aliases for `bv_set_fsos` and `bv_view_center_linesnap`
     - ✅ Full build passes with zero errors

---

## 12. MGED `dl_head_scene_obj` migration analysis

This section documents the usage patterns of `dl_head_scene_obj` in `mged` and
their BSG equivalents, to make the Phase 2e migration more tractable.

### Usage pattern inventory

| File | Line(s) | Pattern | BSG equivalent |
|------|---------|---------|----------------|
| `buttons.c` | 563, 577 | **Empty check + first item** (`illump`) | `BU_PTBL_LEN(&root->children) > 0` + `BU_PTBL_GET(&root->children, 0)` |
| `chgtree.c` | 157 | **Full iteration by path** | `bsg_view_traverse` with `s_fullpath` match |
| `chgtree.c` | 228, 263 | **Empty check + first item** | Same as buttons.c |
| `chgview.c` | 726, 911, 1539 | **Empty check** (screen blank detection) | `BU_PTBL_LEN(&root->children) == 0` |
| `chgview.c` | 1390 | **Full iteration** (solid highlighting) | `bsg_view_traverse` with highlight visitor |
| `cmd.c` | 1983 | **Empty check** | `BU_PTBL_LEN(&root->children) == 0` |
| `dodraw.c` | 158 | **Insertion** | `bu_ptbl_ins(&root->children, (long *)sp)` |
| `dozoom.c` | 295 | **Full iteration** (create display lists) | `bsg_view_traverse` with dlist-create visitor |
| `edsol.c` | 1110, 5992, 6024, 6283 | **Full iteration** (solid editing replot/unlock) | `bsg_view_traverse` with edit visitor |
| `usepen.c` | 68, 304 | **Full iteration** (cursor selection by path/name) | `bsg_view_find_by_type` + path filter |
| `usepen.c` | 142–161 | **Navigation** (forward/backward cycling of `illump`) | Index arithmetic on `root->children` ptbl |
| `plot.c` | 97, 114, 205 | **Empty check + full iteration** (vlist export) | `bsg_view_traverse` with vlist-export visitor |
| `rtif.c` | 221 | **Full iteration** (ray-trace vlist copy) | `bsg_view_traverse` with raytrace visitor |
| `set.c` | 464–466 | **GL dlist range arithmetic** (`first.s_dlist` – `last.s_dlist`) | Sensor-based dlist notification (post Phase-2b) |

### Migration complexity by pattern

**A) Empty check (6 sites) — LOW**
Direct mapping to `BU_PTBL_LEN(&root->children) == 0`.  No iteration needed.
Context available: `s->gedp` provides GED → `bsg_scene_root_get(gvp)`.

**B) Full iteration (12 sites) — MEDIUM**
Maps to a `bsg_view_traverse` visitor callback or simple `bu_ptbl` for-loop
over `root->children`.  Key requirement: a scene-root pointer must be passed
through functions that currently only receive a `display_list *`.

**C) First item / `illump` (4 sites) — LOW**
Maps to `(bsg_shape *)BU_PTBL_GET(&root->children, 0)`.  The global `illump`
pointer continues to work; only the acquisition changes.

**D) Insertion (1 site, `dodraw.c:158`) — LOW**
`BU_LIST_APPEND` becomes `bu_ptbl_ins`.  The function already receives
`struct mged_state *s`, giving access to `view_state->vs_gvp`.

**E) Navigation / `illump` cycling (4 sites, `usepen.c`) — MEDIUM-HIGH**
Forward/backward cycling wraps at display-list boundaries.  After migration the
"current illuminated solid" is an index into `root->children`.  Wrap-around
needs to span multiple display lists' shapes, which requires either (a) a
consolidated `root->children` array per view (after Phase 2e) or (b) a helper
that walks the flat `gv_objs.db_objs` table.

**F) GL dlist range arithmetic (2 sites, `set.c`) — HIGH**
These assume dlists are allocated in a contiguous block.  After Phase 2e the
dlist-per-shape model managed by sensors (`gl_register_dlist_sensor`) makes
contiguous allocation an implementation detail.  These sites should be converted
to iterate `root->children` and call `glDeleteLists(sp->s_dlist, 1)` per shape.

### Prerequisites for mged migration

1. **Scene root available in `mged_state`**: `view_state->vs_gvp` already gives
   the view; `bsg_scene_root_get(view_state->vs_gvp)` returns the scene root
   once Phase 2a initialisation (`bsg_scene_root_create`) is in place (already done).

2. **Shapes registered in scene root**: Today shapes are only in
   `dl_head_scene_obj`.  `dodraw.c:158` must also call
   `bu_ptbl_ins(&root->children, (long *)sp)` to register in the scene root
   (**already implemented** as Phase 2e dual-write).

3. **Shape removal from scene root on erase**: Each `FREE_BV_SCENE_OBJ` site in
   `libged/display_list.c` and `mged/dodraw.c` must also call
   `bu_ptbl_rm(&root->children, sp)` (**already implemented** in display_list.c).

4. **`illump` cycling refactor** (`usepen.c`): Rework to use index into
   `root->children` instead of linked-list prev/next.

---

## 13. Can MGED's display-list accesses be recast as scene-object interactions?

**Short answer: Yes, completely** — with no loss of user-visible functionality.

The key architectural insight is that the current two-level iteration pattern:
```c
/* Today: nested loop over all display lists × all shapes per list */
gdlp = BU_LIST_NEXT(display_list, ged_dl(s->gedp));
while (BU_LIST_NOT_HEAD(gdlp, ged_dl(s->gedp))) {
    for (BU_LIST_FOR(sp, bsg_shape, &gdlp->dl_head_scene_obj)) {
        /* do something */
    }
}
```
collapses to a **single flat loop** after Phase 2e:
```c
/* After Phase 2e: shapes are in scene-root children, no dl_head_scene_obj */
bsg_shape *root = bsg_scene_root_get(view_state->vs_gvp);
for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
    bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&root->children, i);
    /* do something */
}
```

The `display_list` struct retains its identity as a **named drawn path** (the
`dl_path` name, the `dl_dp` pointer, etc.) but no longer holds a shape list.
The relationship between a shape and its logical display list is already captured
by `ged_bv_data->s_fullpath` stored in `sp->s_u_data`.

### Pattern-by-pattern recast

#### A — Empty check → `BU_PTBL_LEN` test
```c
/* Before */
if (BU_LIST_NON_EMPTY(&gdlp->dl_head_scene_obj)) { … }

/* After — no display list loop needed at all */
bsg_shape *root = bsg_scene_root_get(view_state->vs_gvp);
if (root && BU_PTBL_LEN(&root->children) > 0) { … }
```

#### B — Full iteration by path → `ged_find_shapes_by_path()` helper
```c
/* Before — chgtree.c, edsol.c: find all shapes matching a db_full_path */
for each gdlp:
    for each sp in gdlp->dl_head_scene_obj:
        if (db_identical_full_paths(pathp, &bdata->s_fullpath)) …

/* After — single flat loop in a libged helper (see Section 14) */
struct bu_ptbl matches = BU_PTBL_INIT_ZERO;
ged_find_shapes_by_path(gedp, view_state->vs_gvp, pathp, &matches);
for (size_t i = 0; i < BU_PTBL_LEN(&matches); i++) {
    bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&matches, i);
    /* operate on sp */
}
bu_ptbl_free(&matches);
```

#### B — Full iteration for editing → `bsg_view_traverse` visitor
```c
/* Before — edsol.c: replot all illuminated solids */
for each gdlp:
    for each sp in gdlp->dl_head_scene_obj:
        if (sp->s_iflag == DOWN) continue;
        replot_original_solid(s, sp);

/* After — one traversal, no display list walking */
bsg_view_traverse(view_state->vs_gvp, replot_visitor, s);
/* where replot_visitor checks state->node->s_iflag */
```

#### C — First-item / `illump` initialisation → `BU_PTBL_GET` index 0
```c
/* Before — buttons.c, chgtree.c */
illum_gdlp = gdlp;
illump = (bsg_shape *)BU_LIST_NEXT(bsg_shape, &gdlp->dl_head_scene_obj);

/* After */
bsg_shape *root = bsg_scene_root_get(view_state->vs_gvp);
illump = (root && BU_PTBL_LEN(&root->children) > 0)
       ? (bsg_shape *)BU_PTBL_GET(&root->children, 0)
       : NULL;
/* illum_gdlp is no longer needed: the display list name is
 * recoverable from ged_bv_data->s_fullpath if required. */
```

#### E — Forward/backward navigation → index arithmetic
```c
/* Before — usepen.c: advance illump, wrapping across display-list boundaries */
if (BU_LIST_NEXT_IS_HEAD(sp, &gdlp->dl_head_scene_obj)) {
    gdlp = BU_LIST_PNEXT(display_list, gdlp);   /* next gdlp */
    sp = BU_LIST_NEXT(bsg_shape, &gdlp->dl_head_scene_obj);
} else {
    sp = BU_LIST_PNEXT(bsg_shape, sp);
}

/* After — display-list boundaries vanish; one flat array */
bsg_shape *root = bsg_scene_root_get(view_state->vs_gvp);
int idx = (int)bu_ptbl_locate(&root->children, (long *)illump);
idx = forward ? idx + 1 : idx - 1;
if (idx < 0) idx = (int)BU_PTBL_LEN(&root->children) - 1;
if (idx >= (int)BU_PTBL_LEN(&root->children)) idx = 0;
illump = (bsg_shape *)BU_PTBL_GET(&root->children, idx);
/* illum_gdlp eliminated entirely */
```

#### F — GL dlist range arithmetic → per-shape sensor callbacks
```c
/* Before — set.c: free a range of display lists by first/last ID */
glDeleteLists(BU_LIST_FIRST(bv_scene_obj, &gdlp->dl_head_scene_obj)->s_dlist,
              BU_LIST_LAST(bv_scene_obj,  &gdlp->dl_head_scene_obj)->s_dlist
              - BU_LIST_FIRST(...)->s_dlist + 1);

/* After — iterate root->children and free each individually;
 * the Phase 2b sensor system fires gl_dlist_stale_cb per shape */
bsg_shape *root = bsg_scene_root_get(view_state->vs_gvp);
for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
    bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&root->children, i);
    if (sp->s_dlist) { glDeleteLists(sp->s_dlist, 1); sp->s_dlist = 0; }
}
```

### What about `illum_gdlp`?

`illum_gdlp` was introduced to remember *which* display list a solid belongs to,
needed when erasing it (`dl_erasePathFromDisplay`).  After Phase 2e it is
redundant: the display list path is `ged_bv_data->s_fullpath`, and erasing can
use that path directly:
```c
/* Before */
dl_erasePathFromDisplay(gedp, bu_vls_cstr(&illum_gdlp->dl_path), 0);

/* After — derive the erase path from the shape itself */
struct ged_bv_data *bdata = (struct ged_bv_data *)illump->s_u_data;
char *path_str = db_path_to_string(&bdata->s_fullpath);
dl_erasePathFromDisplay(gedp, path_str, 0);
bu_free(path_str, "path str");
```
`illum_gdlp` can therefore be **removed** once Phase 2e is complete.

### New libged helper: `ged_find_shapes_by_path`

The most common pattern in MGED — finding all shapes that correspond to a
given `db_full_path` — is replaced by a helper in `libged`.  This helper
iterates `root->children` (O(n) like the current code but without the outer
display-list loop) and collects matches into a `bu_ptbl`:
```c
/* Declared in libged_private.h or display_list.h */
GED_EXPORT void ged_find_shapes_by_path(struct ged *gedp,
                                        bsg_view *v,
                                        const struct db_full_path *path,
                                        struct bu_ptbl *result);
```
See Section 14 for the implementation.

### Conclusion

Every MGED operation that currently accesses `dl_head_scene_obj` can be
expressed directly against the BSG scene root with **no loss of functionality**:

| Feature | Before | After |
|---------|--------|-------|
| Object highlighting (`illump`) | BU_LIST_NEXT on dl_head_scene_obj | BU_PTBL_GET(root->children, 0) |
| Forward/back solid navigation | Linked-list prev/next + gdlp wrap | Index arithmetic on ptbl |
| Find solid by path | Nested for loop over all gdlps | `ged_find_shapes_by_path` |
| Empty view check | BU_LIST_NON_EMPTY per gdlp | BU_PTBL_LEN(root->children) |
| Replot all illuminated | Nested for loop | bsg_view_traverse visitor |
| GL dlist cleanup | Contiguous range delete | Per-shape delete via sensor |
| illum_gdlp | Required for erase path | Replaced by s_fullpath on illump |

---

## 14. New `ged_find_shapes_by_path` helper

**Purpose**: replace all nested `for-over-gdlp / for-over-dl_head_scene_obj`
loops that search for shapes by `db_full_path`.

**Declaration** (`src/libged/display_list.h` or similar):
```c
GED_EXPORT void ged_find_shapes_by_path(struct ged *gedp,
                                        bsg_view *v,
                                        const struct db_full_path *path,
                                        struct bu_ptbl *result);
```

**Implementation** (flat loop over scene-root children — `O(n)`, same
complexity as current nested loops):
```c
void
ged_find_shapes_by_path(struct ged *gedp, bsg_view *v,
                        const struct db_full_path *path, struct bu_ptbl *result)
{
    if (!gedp || !v || !path || !result) return;
    bsg_shape *root = bsg_scene_root_get(v);
    if (!root) return;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
        bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&root->children, i);
        if (!sp->s_u_data) continue;
        struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
        if (db_identical_full_paths(path, &bdata->s_fullpath))
            bu_ptbl_ins(result, (long *)sp);
    }
}
```

**Sites in mged that collapse to this helper after Phase 2e**:
- `chgtree.c:157` — `find_solid_with_path`
- `edsol.c:1110` — find solids sharing `LAST_SOLID(bdata) == illdp`
- `edsol.c:5992`, `6024`, `6283` — path-top matching for tree replot/lock
- `usepen.c:304` — cursor path-based selection

---

## Architectural Pivot — `libbsg`: a Fully Independent Scene-Graph Library

**Decision (Session 11):** The previous approach of incrementally patching
`libbv` internals to simultaneously support the legacy flat-table API *and* the
new scene-graph API produced mixed intermediate states that are very difficult
to validate.  The draw-test control images became wrong (blank shaded renders,
wrong camera angles) precisely because the same code paths were being exercised
for both semantics at once.

**New goal**: create `libbsg` as a **completely independent library** that does
*not* depend on `libbv` in any way.  `libbv` remains unchanged for backward
compatibility / deprecation; all new scene-graph logic is implemented fresh in
`libbsg`.

### Rationale

| Concern | Mixed approach (old) | `libbsg` (new) |
|---------|----------------------|----------------|
| Rendering correctness | Partial — flat tables and graph fire simultaneously, result undefined | Clear — only `libbsg` traversal path active |
| Test validation | Control images broken by intermediate state | Control images stable; libbv tests unchanged |
| Scope of change per session | Large (touches libbv internals) | Small and incremental — build up libbsg from scratch |
| Risk of regression | High (libbv callers impacted) | None — libbv untouched |

### Revised plan — Session 14 (course correction)

**User clarification**: The `libbsg_*` naming convention introduced in Sessions 12–13
was wrong.  The correct approach is to **fold the existing `bsg_*` definitions** from
`bsg/defines.h` and `bsg/util.h` into `libbsg`, keeping the `bsg_*` naming and header
convention.  Effectively: copy `libbv` → `libbsg`, rename, remove non-BSG elements.

**Session 14 implementation**:
- ✅ Removed the session 12–13 `libbsg_*` third naming convention (`libbsg_shape`,
  `libbsg_camera`, `LIBBSG_NODE_*`, `libbsg_dm` adapter) — these are gone.
- ✅ Moved `scene_graph.cpp` from `libbv/` to `libbsg/`.  All 101 `bsg_*` symbols
  (traversal, shape management, view, camera, LoD, sensors, polygon, scene root) are
  now exported directly from `libbsg.so`.
- ✅ Removed `scene_graph.cpp` from `libbv/CMakeLists.txt`.
- ✅ `libbv` now links `libbsg` for backward-compat (callers that link only `libbv`
  automatically gain `bsg_*` symbols via `libbv`'s link dependency).
- ✅ `bsg/defines.h` `BSG_EXPORT` macro updated to use `BSG_DLL_EXPORTS` when building
  `libbsg`, keeping `BV_EXPORT` as a fallback for the transition period.
- ✅ `include/libbsg/libbsg.h` rewritten as umbrella include for `bsg/defines.h`,
  `bsg/util.h`, `bsg/lod.h`, `bsg/polygon.h` — the canonical entry point for
  BSG-based code.
- ✅ `bsg_private.h` in `libbsg/` provides `bsg_scene_obj_internal` and
  `bsg_scene_set_internal` as Phase-1 aliases of the `bv_*` internals.
- ✅ `libbsg.so` builds clean with 101 `bsg_*` exported symbols.

### Incremental build-up plan for `libbsg`

1. **Stand-up skeleton** ✅ — `src/libbsg/CMakeLists.txt`, `include/libbsg/libbsg.h`
   public header.

2. **Core `bsg_*` implementations** ✅ — `scene_graph.cpp` moved to `libbsg/`.
   All `bsg_*` API functions (Phase 1 and Phase 2) now live in `libbsg`.
   Type names follow the existing `bsg_*` convention from `bsg/defines.h`.

3. **Remove remaining libbv BSG content** — copy remaining BSG-related files from
   `libbv/` to `libbsg/`, removing the `bv_*` counterparts from the copies:
   - `util.cpp` — contains remaining `bv_*` functions called by `scene_graph.cpp`
   - `hash.c` — `bv_obj_hash()` etc.
   - `diff.c` — `bv_diff()`

4. **Decouple from `bv_private.h`** — once `bsg_shape` and `bsg_view` are independent
   structs (no longer aliases), `bsg_private.h` no longer needs to include
   `libbv/bv_private.h`.

5. **Make `bsg_*` types independent** — replace typedef aliases in `bsg/defines.h`
   with standalone struct definitions (breaking change; `bv_*` becomes the compat
   wrapper layer).

6. **Disable `bv.h` globally** — `BRLCAD_DISABLE_LIBBV_INCLUDES` CMake option ✅
   (OFF by default) added to `misc/CMake/BRLCAD_User_Options.cmake`.

### Next steps (future sessions)

- **Step 3**: Copy `util.cpp`, `hash.c`, `diff.c` → `libbsg/`, remove BSG parts from
  `libbv`.  This further reduces `libbv`'s surface area.
- **Step 4**: Remove the `bv_private.h` dependency from `bsg_private.h` by making
  `bsg_scene_obj_internal` an independent C++ struct.
- **Step 5**: Make `bsg_shape` an independent struct (not a typedef of `bv_scene_obj`).
  This is the major structural break — enables `libbsg` to evolve its layout freely.
- **Step 6**: Progressively migrate consumers from `#include "bv/..."` to
  `#include "libbsg/libbsg.h"`, driving toward enabling `BRLCAD_DISABLE_LIBBV_INCLUDES`.
- **Step 7**: Remove `libbv` once all consumers have migrated.

### Session 10 partial work (superseded by pivot)

Session 10 attempted to fully separate the BSG/bv stacks within the existing
`libbv` code (`bsg_shape_get`, `bsg_view_autoview`, `bsg_view_clear`,
`polygon.c`, `vlist.c`, `view.c`).  This work is committed on the branch and
**not reverted** — it is a useful reference for what the `libbsg` equivalents
should implement — but it no longer drives the primary migration path.  The
control images have been restored from commit `c9f865cd` so that the draw-test
suites provide a clean baseline again.

---

*Last updated: 2026-03-06 (Phases 2a–2e complete; **Session 6**: Phase 2e FINAL DECOMMISSION — `dl_head_scene_obj` field removed from `struct display_list`; all shape tracking now exclusively via `root->children` in scene-root; `bsg_scene_fsos` and `bsg_view_center_linesnap` BSG wrappers added; full build passes clean)  
**Session 8 (bug-fix)**: MGED NULL-crash guards in `titles.c`, `edebm.c`, `eddsp.c`, `edvol.c` (`MEDIT(s)` NULL after sedit_accept).  
**Session 9 (shape tracking correctness)**:  
- `bsg_shape_get/put`: skip `cv->independent` co-views when broadcasting shared object registration.  
- `bsg_view_clear` filter: correctly gates LOCAL shapes on `BSG_LOCAL_OBJS` flag; no longer removes live shared pointers.  
- `bsg_view_clear` co-view loop: skip independent views.  
- Swrast DM plugin dependency resolved (must be built before draw tests).  
- All five draw test suites regenerated and passing: `ged_test_draw`, `ged_test_faceplate`, `ged_test_quad`, `ged_test_select_draw`, `ged_test_lod`.  
**Session 10 (BSG/bv stack separation attempt — superseded)**:  
- `bsg_shape_get` routed via `bv_obj_create` + `otbl=NULL`; `bsg_view_autoview` walks `root->children`; `bsg_view_clear` freed from `bv_clear` dependency; `polygon.c`/`vlist.c` use `bsg_shape_get`; `view.c` flat-list else-branches removed.  
- Control images were wrongly regenerated from this intermediate state; restored to `c9f865cd` baseline in Session 11.  
**Session 11 (architectural pivot)**:  
- Control images restored to `c9f865cd` baseline.  
- New direction: build `libbsg` as a fully independent library (no `libbv` dependency); see section above.  
**Session 12 (libbsg skeleton — Steps 1–4 COMPLETE)**:  
- ✅ Step 1 — Skeleton: `src/libbsg/` directory, `CMakeLists.txt`, library target `libbsg` linking only `libbu` + `libbn`. Added to `src/source_dirs.cmake`.
- ✅ Step 2 — Core node types in `include/libbsg/libbsg.h`: `bsg_node` (base, independent), `bsg_separator` / `bsg_transform` (typedefs of `bsg_node`), `libbsg_camera` (clean struct), `libbsg_shape` (leaf geometry, independent of `bv_scene_obj`), `libbsg_view_params` (plain-C viewport struct, no `bview`).
- ✅ Step 3 — Scene root + view binding: `libbsg_scene_root_create(params)`, `libbsg_camera_node_alloc/get/set()`, `libbsg_scene_root_camera()`, `libbsg_view_bind()` in `src/libbsg/scene.c`.
- ✅ Step 4 — Traversal engine: `libbsg_traversal_state_init()`, `libbsg_traverse()` with full separator save/restore, camera tracking, and LoD child selection in `src/libbsg/traverse.c`.
- ✅ Additional: `libbsg_find_by_type()` typed-query helper.
- ✅ `libbsg.so` builds clean; all 24 API symbols exported; 7-test smoke suite passes.  
**Session 13 (Steps 5–7 COMPLETE)**:
- ✅ Naming fix: renamed `struct bsg_shape` → `struct libbsg_shape`, `struct bsg_camera` → `struct libbsg_camera`, `BSG_NODE_*` → `LIBBSG_NODE_*` in all libbsg headers and implementation files to eliminate C redefinition conflicts with the legacy `bsg/defines.h` / `bv/defines.h` headers.
- ✅ Removed the `#error` mutual-exclusion guard (no longer needed now that type names are unique).
- ✅ Step 5 — `libbsg_dm` adapter library (`src/libbsg_dm/`, `include/libbsg_dm/libbsg_dm.h`): `libbsg_dm_load_camera()` and `libbsg_dm_draw_scene()` bridge libbsg nodes to `dm_draw_vlist()`. Added `set_deps(libbsg_dm "libbsg;libdm;librt;libbn;libbu")` to `source_dirs.cmake`.
- ✅ Step 6 — Geometry ingestion (`src/libbsg_dm/geom.c`): `libbsg_shape_wireframe(s, ip, ttol, tol)` calls `ft_plot()` from the primitive functab to populate `libbsg_shape.vlist` from an `rt_db_internal`.
- ✅ Step 7 — Selection state (`src/libbsg/select.c`): `libbsg_select_state`, `libbsg_select_alloc/free/clear/add_path/rm_path/has_path/count/sync_highlight` — clean re-implementation, no `DbiState` coupling.
- ✅ Both `libbsg.so` (24 symbols) and `libbsg_dm.a` (3 symbols) build cleanly.
- Remaining: Step 8 (optional CMake flag `BRLCAD_DISABLE_LIBBV_INCLUDES`).*
