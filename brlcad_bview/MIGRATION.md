# BRL-CAD bview → bv_node/bv_scene/bview_new Migration Guide

## Overview

This document tracks the phased migration from BRL-CAD's legacy `struct bview` /
`struct bv_scene_obj` view API to the new `bv_node` / `bv_scene` / `bview_new`
scene graph API introduced in `brlcad_bview/include/bv/defines.h`.

The migration is **incremental**: the old API is preserved and continues to work.
New code must use the new API.  Old code will be updated per-subsystem over
multiple phases and old symbols will go through the normal BRL-CAD deprecation
policy before removal.

---

## Motivation

The existing `struct bview` and `struct bv_scene_obj` hierarchy was designed
organically over many years and has several properties that make it difficult to
maintain and extend:

* **Direct struct-member access** is the norm, making it impossible to trigger
  side-effects (callbacks, invalidation, LoD updates) when state changes.
* **God-struct anti-pattern**: `struct bview` mixes camera, viewport, settings,
  scene objects, LoD state, Tcl/display-manager handles, and grid settings in a
  single giant structure.
* **No clean scene-graph boundary**: scene objects live in flat `bu_ptbl` tables
  keyed by view rather than a proper hierarchical graph.
* **Coin3D/obol alignment**: the long-term goal is to render using obol (an
  Inventor-compatible scene graph library) as the back-end.  The new API is
  explicitly designed so that each `bv_node` can be translated to/from a
  corresponding `SoNode` without BRL-CAD core libraries ever needing to include
  obol headers.

---

## API Correspondence

### Scene graph nodes

| Old concept | New type | Inventor analog |
|---|---|---|
| `struct bv_scene_obj` (geometry leaf) | `bv_node` (BV_NODE_GEOMETRY) | `SoShape` |
| `struct bv_scene_group` (display list) | `bv_node` (BV_NODE_GROUP) | `SoGroup` |
| — (no direct equivalent) | `bv_node` (BV_NODE_SEPARATOR) | `SoSeparator` |
| `gv_center` / `gv_rotation` matrices | `bv_node` (BV_NODE_TRANSFORM) | `SoTransform` |
| Camera fields in `struct bview` | `struct bview_camera` + `bv_node` (BV_NODE_CAMERA) | `SoCamera` |
| `gv_tcl.gv_prim_labels` colors etc. | `struct bview_material` + `bv_node` (BV_NODE_MATERIAL) | `SoMaterial` |

### View / scene containers

| Old concept | New type | Inventor analog |
|---|---|---|
| `struct bview` | `struct bview_new` | `SoSceneManager` / `SoRenderArea` |
| `struct bview_set` | `struct bv_scene` (shared by multiple views) | scene root (`SoSeparator`) |
| `gv_width`, `gv_height` | `struct bview_viewport` | `SbViewportRegion` |
| `gv_ls` / `gv_s` settings | `struct bview_appearance` | `SoEnvironment` / `SoDrawStyle` |
| `gv_tcl` faceplate | `struct bview_overlay` | HUD layer (no direct Inventor analog) |
| `gv_objs.db_objs` / `view_objs` | `bv_scene_nodes` flat table + scene tree | `SoGroup` children |

---

## New API Summary

All new types and functions are declared in `brlcad_bview/include/bv/defines.h`
inside the `EXPERIMENTAL` block and implemented in
`brlcad_bview/src/libbv/scene.cpp`.

### `bv_node` (scene graph node — analogous to `SoNode`)

```c
/* Lifecycle */
struct bv_node *bv_node_create(const char *name, enum bv_node_type type);
void            bv_node_destroy(struct bv_node *node);          /* recursive */

/* Hierarchy (SoGroup::addChild / removeChild analogs) */
void                   bv_node_add_child(struct bv_node *parent, struct bv_node *child);
void                   bv_node_remove_child(struct bv_node *parent, struct bv_node *child);
const struct bu_ptbl  *bv_node_children(const struct bv_node *node);
struct bv_node        *bv_node_parent_get(const struct bv_node *node);

/* Transform (SoTransform analog) */
void             bv_node_transform_set(struct bv_node *node, const mat_t xform);
const mat_t     *bv_node_transform_get(const struct bv_node *node);
const mat_t     *bv_node_world_transform_get(const struct bv_node *node); /* accumulated */

/* Geometry / material (SoShape + SoMaterial analogs) */
void                          bv_node_geometry_set(struct bv_node *node, const void *geom);
void                          bv_node_material_set(struct bv_node *node,
                                                    const struct bview_material *mat);
const struct bview_material  *bv_node_material_get(const struct bv_node *node);

/* Visibility, type, name, user data */
void              bv_node_visible_set(struct bv_node *node, int visible);
int               bv_node_visible_get(const struct bv_node *node);

/* Geometry and selection */
void              bv_node_geometry_set(struct bv_node *node, const void *geometry);
const void       *bv_node_geometry_get(const struct bv_node *node);
void              bv_node_selected_set(struct bv_node *node, int selected);
int               bv_node_selected_get(const struct bv_node *node);

/* Native axis-aligned bounding box (Phase 2) */
void              bv_node_bounds_set(struct bv_node *node, const point_t min, const point_t max);
void              bv_node_bounds_clear(struct bv_node *node);
int               bv_node_bounds_get(const struct bv_node *node,
                                      point_t *out_min, point_t *out_max);

/* Vlist geometry (Phase 2 — caller manages vlist lifecycle) */
void              bv_node_vlist_set(struct bv_node *node, struct bu_list *vlist);
struct bu_list   *bv_node_vlist_get(const struct bv_node *node);
int               bv_node_vlist_bounds(const struct bv_node *node,
                                        point_t *out_min, point_t *out_max);

/* Render-backend draw state (Phase 2 — analog of s_dlist / s_dlist_stale) */
void              bv_node_dlist_set(struct bv_node *node, unsigned int dlist);
unsigned int      bv_node_dlist_get(const struct bv_node *node);
void              bv_node_dlist_stale_set(struct bv_node *node, int stale);
int               bv_node_dlist_stale_get(const struct bv_node *node);

/* Per-node update callback (Phase 2 — analog of s_update_callback) */
typedef int (*bv_node_update_cb)(struct bv_node *node, struct bview_new *view, int flags);
void              bv_node_update_cb_set(struct bv_node *node,
                                         bv_node_update_cb cb, void *data);
bv_node_update_cb bv_node_update_cb_get(const struct bv_node *node);
void             *bv_node_update_cb_data_get(const struct bv_node *node);

/* Raw geometry source data (Phase 4 — analog of s->draw_data for LoD meshes) */
void  bv_node_draw_data_set(struct bv_node *node, void *draw_data);
void *bv_node_draw_data_get(const struct bv_node *node);

/* Level-of-detail index (Phase 4) */
void              bv_node_lod_level_set(struct bv_node *node, int level);
int               bv_node_lod_level_get(const struct bv_node *node);

enum bv_node_type bv_node_type_get(const struct bv_node *node);
const char       *bv_node_name_get(const struct bv_node *node);
void              bv_node_name_set(struct bv_node *node, const char *name);
void              bv_node_user_data_set(struct bv_node *node, void *user_data);
void             *bv_node_user_data_get(const struct bv_node *node);

/* Traversal (SoAction traversal analog) */
typedef void (*bv_scene_traverse_cb)(struct bv_node *, void *);
void bv_node_traverse(const struct bv_node *node, bv_scene_traverse_cb cb, void *udata);
```

### `bv_scene` (scene graph root — analogous to a `SoSeparator`-rooted scene)

```c
struct bv_scene *bv_scene_create(void);
void             bv_scene_destroy(struct bv_scene *scene);

struct bv_node        *bv_scene_root(const struct bv_scene *scene);
const struct bu_ptbl  *bv_scene_nodes(const struct bv_scene *scene); /* flat lookup */
size_t                 bv_scene_node_count(const struct bv_scene *scene);
void                   bv_scene_clear(struct bv_scene *scene); /* remove+destroy all top-level nodes */

void            bv_scene_add_node(struct bv_scene *scene, struct bv_node *node);
void            bv_scene_remove_node(struct bv_scene *scene, struct bv_node *node);
void            bv_scene_add_child(struct bv_scene *scene, struct bv_node *parent,
                                    struct bv_node *child);
void            bv_scene_remove_child(struct bv_scene *scene, struct bv_node *parent,
                                       struct bv_node *child);

struct bv_node *bv_scene_find_node(const struct bv_scene *scene, const char *name);
void            bv_scene_traverse(const struct bv_scene *scene,
                                   bv_scene_traverse_cb cb, void *udata);

struct bv_node *bv_scene_default_camera(const struct bv_scene *scene);
void            bv_scene_default_camera_set(struct bv_scene *scene, struct bv_node *cam);

/* Bounding box helpers (Phase 2) */
int bv_scene_bbox(const struct bv_scene *scene, point_t *out_min, point_t *out_max);
int bv_node_bbox(const struct bv_node *node, point_t *out_min, point_t *out_max);

/* Selection management (Phase 3) */
int  bv_scene_selected_nodes(const struct bv_scene *scene, struct bu_ptbl *out);
void bv_scene_select_node(struct bv_node *node, int selected, struct bview_new *view);
int  bv_scene_deselect_all(struct bv_scene *scene, struct bview_new *view);

/* Multi-view sharing query (Phase 3) */
size_t                  bv_scene_view_count(const struct bv_scene *scene);
const struct bu_ptbl   *bv_scene_views(const struct bv_scene *scene);

/* LoD update (Phase 4) */
int bv_scene_lod_update(struct bv_scene *scene, const struct bview_new *view);
```

### `bview_new` (render/view context — analogous to `SoSceneManager`)

```c
struct bview_new *bview_create(const char *name);
void              bview_destroy(struct bview_new *view);

/* Scene association */
void              bview_scene_set(struct bview_new *view, struct bv_scene *scene);
struct bv_scene  *bview_scene_get(const struct bview_new *view);

/* Camera */
void                     bview_camera_set(struct bview_new *view, const struct bview_camera *cam);
const struct bview_camera *bview_camera_get(const struct bview_new *view);
void                     bview_camera_node_set(struct bview_new *view, struct bv_node *node);
struct bv_node          *bview_camera_node_get(const struct bview_new *view);
/* View scale convenience accessors (Phase 4 — analog of gv_scale) */
void   bview_camera_scale_set(struct bview_new *view, double scale);
double bview_camera_scale_get(const struct bview_new *view);

/* Viewport, appearance, overlay, pick set */
void bview_viewport_set(struct bview_new *view, const struct bview_viewport *vp);
const struct bview_viewport *bview_viewport_get(const struct bview_new *view);

void bview_appearance_set(struct bview_new *view, const struct bview_appearance *app);
const struct bview_appearance *bview_appearance_get(const struct bview_new *view);

void bview_overlay_set(struct bview_new *view, const struct bview_overlay *ov);
const struct bview_overlay *bview_overlay_get(const struct bview_new *view);

void bview_pick_set_set(struct bview_new *view, const struct bview_pick_set *ps);
const struct bview_pick_set *bview_pick_set_get(const struct bview_new *view);

/* Name */
const char *bview_name_get(const struct bview_new *view);
void        bview_name_set(struct bview_new *view, const char *name);

/* Redraw callback */
typedef void (*bview_redraw_cb)(struct bview_new *, void *);
void             bview_redraw_callback_set(struct bview_new *view, bview_redraw_cb cb, void *data);
bview_redraw_cb  bview_redraw_callback_get(const struct bview_new *view);
void            *bview_redraw_callback_data_get(const struct bview_new *view);
void             bview_redraw(struct bview_new *view);

/* LoD update (Phase 4 — wired to bv_scene_lod_update + bv_mesh_lod_view) */
void bview_lod_update(struct bview_new *view);
int  bview_lod_node_update(struct bv_node *node, const struct bview_new *view);

/* Auto-fit camera to scene geometry (Phase 2) */
#define BV_AUTOVIEW_SCALE_DEFAULT -1
int bview_autoview_new(struct bview_new *view, const struct bv_scene *scene,
                       double scale_factor);

/* Migration helpers (bridge to old API during transition) */
void           bview_from_old(struct bview_new *view, const struct bview *old);
void           bview_to_old(const struct bview_new *view, struct bview *old);
struct bview  *bview_old_get(const struct bview_new *view);
void           bview_old_set(struct bview_new *view, struct bview *old);
/* Convenience: create(name) + from_old + old_set — standard first migration step */
struct bview_new *bview_companion_create(const char *name, struct bview *old);

/* Default initialization (replaces bv_init + bv_settings_init) */
void bview_settings_apply(struct bview_new *view);

/* Auto-fit camera to scene geometry (replaces bv_autoview) */
#define BV_AUTOVIEW_SCALE_DEFAULT -1
int  bview_autoview_new(struct bview_new *view, const struct bv_scene *scene,
                        double scale_factor);

/* LoD per-node update hook */
int  bview_lod_node_update(struct bv_node *node, const struct bview_new *view);

/* Sync helpers — update loop convenience wrappers (Phase 1 migration) */
void bview_sync_from_old(struct bview_new *view);  /* bview_from_old(nv, bview_old_get(nv)) */
void bview_sync_to_old(struct bview_new *view);    /* bview_to_old(nv, bview_old_get(nv)) */

/* Legacy-obj CRUD (Phase 2 migration) */
struct bv_node *bv_scene_insert_obj(struct bv_scene *scene, struct bv_scene_obj *obj);
struct bv_node *bview_insert_obj(struct bview_new *view, struct bv_scene_obj *obj);
struct bv_node *bv_scene_find_obj(const struct bv_scene *scene, const struct bv_scene_obj *obj);
int             bv_scene_remove_obj(struct bv_scene *scene, const struct bv_scene_obj *obj);
int             bview_remove_obj(struct bview_new *view, const struct bv_scene_obj *obj);
```

### Bounding box helpers (Phase 2)

```c
/* AABB of a node subtree (geometry nodes only) */
int bv_node_bbox(const struct bv_node *node, point_t *out_min, point_t *out_max);

/* AABB of an entire scene */
int bv_scene_bbox(const struct bv_scene *scene, point_t *out_min, point_t *out_max);
```

### Migration bridges (Phase 1–3)

```c
/* Wrap a legacy bv_scene_obj tree in bv_node wrappers */
struct bv_node  *bv_scene_obj_to_node(struct bv_scene_obj *s);

/* Build a new bv_scene from a legacy bview or bview_set */
struct bv_scene *bv_scene_from_view(const struct bview *v);
struct bv_scene *bv_scene_from_view_set(const struct bview_set *s);
```

---

## Migration Plan

The migration will proceed in phases.  Each phase migrates one logical subsystem
while keeping the old API fully functional.  Deprecated symbols will carry a
`DEPRECATED` comment in the header but will **not** be removed until at least one
major BRL-CAD release after the deprecation notice is added.

### Phase 0 – Foundation (COMPLETE)

- [x] Define new types (`bv_node`, `bv_scene`, `bview_new`, `bview_camera`,
      `bview_viewport`, `bview_material`, `bview_appearance`, `bview_overlay`,
      `bview_pick_set`) in `include/bv/defines.h`.
- [x] Implement full API in `src/libbv/scene.cpp`.
- [x] Add `bv_node_parent_get` and `bv_scene_default_camera_set` to the public API.
- [x] Add `BV_EXPORT` to all new API declarations so symbols are exported from
      `libbv.so`.
- [x] Write unit tests covering every new API function (`src/libbv/tests/scene.c`,
      26 test cases registered with CTest).
- [x] Confirm all 26 scene tests pass; all existing tests continue to pass.

### Phase 1 – View lifecycle (COMPLETE)

Goals:
- New code creates views with `bview_create()` instead of allocating a
  `struct bview` and calling `bv_init()`.
- `bv_init()`, `bv_free()`, and `bv_settings_init()` are marked `DEPRECATED`
  in the header.
- `bview_settings_apply()` is implemented: sets the same initial camera,
  viewport, appearance, and overlay defaults that `bv_init()` / `bv_settings_init()`
  establish for a legacy `struct bview`.
- `bview_from_old()` copies camera, viewport, all appearance fields (grid, axes,
  colors) and overlay (show_fps) from a legacy `struct bview`.
- `bview_to_old()` is now a full round-trip helper: it pushes camera, viewport,
  appearance (grid/axes draw flags + colors), and overlay (show_fps) back to the
  legacy `struct bview`.

`bview_companion_create()` was added as the standard migration bridge (this
session).  It wraps `bview_create() + bview_from_old() + bview_old_set()` into a
single call so callers can add a `bview_new` companion alongside an existing
`struct bview` without boilerplate:

```c
// ---- existing code (kept unchanged) ----
BU_ALLOC(v, struct bview);
bv_init(v, &ged_views);
// ---- one-line addition for new-API callers ----
struct bview_new *nv = bview_companion_create("default", v);
// ... nv is a fully functional bview_new, bview_old_get(nv) == v
// ---- teardown ----
bview_destroy(nv);
bv_free(v);
BU_FREE(v, struct bview);
```

### Phase 1 – bv_init caller inventory

All `bv_init` / `bv_free` callers in `brlcad_bview/src/` and their migration status:

| File | Line | Pattern | Category | Status |
|------|------|---------|----------|--------|
| `libqtcad/QgGL.cpp` | 52–55 | `BU_GET + bv_init(v, NULL)` | A – widget view | ✅ companion added |
| `libqtcad/QgSW.cpp` | 54–57 | `BU_GET + bv_init(v, NULL)` | A – widget view | ✅ companion added |
| `libqtcad/QgModel.cpp` | 323–329 | `BU_GET + bv_init(v, &vset)` | A – model default view | ✅ companion added |
| `libdm/swrast/fb-swrast.cpp` | 389–390 | `BU_GET + bv_init(v, NULL)` | B – framebuffer view | ✅ companion added (`swrastinfo.canvas_nv`) |
| `mged/setup.c` | 554–555 | `BU_ALLOC + bv_init(v, NULL)` | C – long-lived MGED view | ✅ companion added (`_view_state.vs_nvp`) |
| `mged/cmd.c` | 2532 | `bu_calloc + bv_init(staging, NULL)` | B – ephemeral staging view | ✅ companion added (`staging_nv`) |
| `mged/cmd.c` | 2589,2612,2651 | `bv_free(staging)` | B – ephemeral staging cleanup | ✅ companion destroyed at all exit paths |
| `libtclcad/commands.c` | 4523 | `bv_init(new_gdvp, &vset)` | C – Tcl-created view | ✅ companion added to `ged_free_view_companions` |
| `libged/ged.cpp` | 123 | `BU_ALLOC + bv_init(gvp, &vset)` | C – GED primary view | ✅ companion added as `ged_gvnv` |
| `libged/ged.cpp` | 208+ | `bv_free(gdvp)` loop | C – GED view cleanup | ✅ iterates `ged_free_view_companions` |
| `libged/dm/dm.c` | 582 | `BU_GET + bv_init(v, &vset)` | C – DM-attached view | ✅ companion added to `ged_free_view_companions` |
| `libged/tests/draw/quad.cpp` | 458 | `bv_init(v, &vset)` in test | D – test scaffolding | ✅ companion added to `ged_free_view_companions` |
| `libged/tests/draw/aet.cpp` | 204 | `bv_init(v, &vset)` in test | D – test scaffolding | ✅ companion added to `ged_free_view_companions` |
| `librt/tests/bv_poly_sketch.c` | 62 | `bv_init(v, NULL)` in test | D – test scaffolding | ✅ companion added + cleanup |
| `librt/tests/edit/tor.cpp` | 117 | `bv_init(v, NULL)` in test | D – test scaffolding | ✅ companion added + cleanup |

Category A = widget/local view with no external dependencies → companion is safe
Category B = ephemeral view → needs per-callsite analysis
Category C = long-lived GED/Tcl view → requires wider struct changes
Category D = test scaffolding

**All 15 callsites are now migrated.**

### Companion tracking strategy

- `struct ged` gains two new fields (declared in `include/ged/defines.h`):
  - `struct bview_new *ged_gvnv` — companion for `ged_gvp` (primary view)
  - `struct bu_ptbl ged_free_view_companions` — parallel table: one entry per entry
    in `ged_free_views`, in the same order.  Entry is `NULL` for the `ged_gvp` slot
    (whose companion is managed by `ged_gvnv` directly) and a valid `bview_new*` for
    all other GED-owned views.
- `struct _view_state` gains `vs_nvp` — companion for the mged primary view.
- `struct swrastinfo` gains `canvas_nv` — companion for the swrast framebuffer view.
- Ephemeral staging views in `mged/cmd.c` use a local `staging_nv` destroyed at
  every exit path.

Files updated (previous two sessions — Categories A, B, C, D):
- `src/libbv/scene.cpp` – implement `bview_companion_create()`
- `include/bv/defines.h` – declare `bview_companion_create()`
- `include/ged/defines.h` – add `ged_gvnv`, `ged_free_view_companions` to `struct ged`
- `include/qtcad/QgGL.h` / `QgSW.h` / `QgModel.h` – add companion members
- `src/libqtcad/QgGL.cpp` / `QgSW.cpp` / `QgModel.cpp` – create/destroy companions
- `src/libged/ged.cpp` – `ged_init` creates `ged_gvnv`; `ged_free` destroys all companions
- `src/libged/dm/dm.c` – adds companion to `ged_free_view_companions` for DM views
- `src/libtclcad/commands.c` – adds companion to `ged_free_view_companions` for Tcl views
- `src/mged/mged_dm.h` – add `vs_nvp` to `_view_state`
- `src/mged/setup.c` – create `vs_nvp` + add to `ged_free_view_companions`
- `src/mged/cmd.c` – add `staging_nv`, destroy at all 3 exit paths
- `src/libdm/swrast/fb-swrast.cpp` – add `canvas_nv` to `swrastinfo`, destroy in `qt_destroy`
- `src/libged/tests/draw/quad.cpp` – add companions for 4 quad views
- `src/libged/tests/draw/aet.cpp` – add companions for 4 aet views
- `src/librt/tests/bv_poly_sketch.c` – add companion + cleanup
- `src/librt/tests/edit/tor.cpp` – add companion + cleanup
- `src/libbv/tests/scene.c` – 4 new tests for `bview_companion_create`

### Phase 2 – Scene objects (COMPLETE)

Goals and status:
- `bv_scene_obj_to_node(struct bv_scene_obj *)` implemented: wraps a legacy
  scene object (and its children, recursively) in `bv_node` instances.
  Node type is `BV_NODE_GEOMETRY` for leaves, `BV_NODE_GROUP` for objects
  with children.  Original pointer preserved as `user_data`.
- `bv_scene_from_view(const struct bview *)` implemented: creates a full
  `bv_scene` from a legacy `bview` by wrapping all db_objs and view_objs.
  Uses `bv_view_objs()` to correctly handle both independent and shared views.
- `bv_node_bbox(const struct bv_node *, point_t *, point_t *)` implemented:
  computes the axis-aligned bounding box of a `bv_node` subtree by traversing
  visible `BV_NODE_GEOMETRY` nodes.  Bounding-box priority:
  1. Native AABB (set via `bv_node_bounds_set()`) is used when present.
  2. Bounding sphere of a wrapped `bv_scene_obj` (`s_center` + `s_size`) as
     fallback for nodes migrated from the legacy API.
  3. Nodes with neither native bounds nor a wrapped object are skipped.
  Returns 0 for subtrees that contribute no bounds.
- `bv_scene_bbox(const struct bv_scene *, point_t *, point_t *)` implemented:
  computes AABB of an entire scene (all top-level nodes and their subtrees).
- `bview_autoview_new(struct bview_new *, const struct bv_scene *, double)`
  implemented: analog of `bv_autoview()` for the new scene graph API.
- `bv_node_geometry_get()` — getter for the geometry pointer; symmetric with
  `bv_node_geometry_set()` which existed but had no paired getter.
- `bv_node_selected_set()` / `bv_node_selected_get()` — public accessors for
  the `selected` field (was in `bv_node` struct but had no public API).
- **Native AABB on `bv_node`** (this session):
  - Added `have_bounds`, `bounds_min`, `bounds_max` fields to `bv_node`.
  - `bv_node_bounds_set(node, min, max)` — store a native AABB; marks
    `have_bounds = 1`.
  - `bv_node_bounds_clear(node)` — reset native bounds; reverts to legacy
    bv_scene_obj fallback in bbox traversal.
  - `bv_node_bounds_get(node, out_min, out_max)` — returns 1 and fills the
    outputs if native bounds are set; 0 otherwise.
  - Updated `_bbox_cb` traverse callback to prefer native AABB over the
    legacy sphere.  This allows pure new-API geometry nodes (no wrapped
    `bv_scene_obj`) to participate in bounding-box queries.

Files updated:
- `include/bv/defines.h` – `bv_node_bbox`, `bv_scene_bbox`,
  `bview_autoview_new`, `bview_lod_node_update`, `BV_AUTOVIEW_SCALE_DEFAULT`
  macro, `bv_node_geometry_get`, `bv_node_selected_set`, `bv_node_selected_get`,
  `bv_node_bounds_set`, `bv_node_bounds_clear`, `bv_node_bounds_get`
  declarations; updated `bv_node_bbox` doc comment
- `src/libbv/bv_private.h` – added `have_bounds`, `bounds_min`, `bounds_max`
  fields to `struct bv_node`
- `src/libbv/scene.cpp` – implement all functions; initialize new fields in
  `bv_node_create()`; update `_bbox_cb` for native-bounds priority
- `src/libbv/tests/scene.c` – add `node_bounds_null`, `node_bounds_set_get`,
  `node_bbox_native`, `node_bbox_native_overrides` tests
- **Render state and per-node update callback** (this session):
  - Added `dlist` (display list handle), `dlist_stale` (needs-redraw flag),
    `update_cb` (per-node regenerate callback), `update_cb_data` (callback user
    data) fields to `bv_node`.  These are direct analogs of `s_dlist`,
    `s_dlist_stale`, and `s_update_callback` on `bv_scene_obj`, completing the
    set of fields needed to render a native node without a legacy wrapper.
  - Added `bv_node_update_cb` typedef in `defines.h` (documented analog of
    `bv_scene_obj::s_update_callback`).
  - `bv_node_dlist_set/get()` and `bv_node_dlist_stale_set/get()` — render-state
    accessors.
  - `bv_node_update_cb_set(node, cb, data)` / `bv_node_update_cb_get(node)` /
    `bv_node_update_cb_data_get(node)` — callback accessors.
- **Native vlist support** (this session):
  - `bv_node_vlist_set(node, vlist)` / `bv_node_vlist_get(node)` — typed wrappers
    around `bv_node_geometry_set/get()`.  Store a `struct bu_list *` (vlist head)
    as the node's geometry.  Caller retains vlist ownership.
  - `bv_node_vlist_bounds(node, out_min, out_max)` — compute AABB from the node's
    vlist by calling `bv_vlist_bbox()`.  Returns 1 on success, 0 if no vlist or
    empty.
  - Updated `_bbox_cb` traverse callback: priority 3 (after native AABB and legacy
    obj sphere) now computes bounds from vlist when the geometry pointer is non-NULL.
    This allows fully native geometry nodes to participate in `bv_scene_bbox()` and
    `bview_autoview_new()` without any legacy API calls.

Files updated (this session, Phase 2):
- `include/bv/defines.h` – `bv_node_update_cb` typedef; `bv_node_vlist_set/get`,
  `bv_node_vlist_bounds`, `bv_node_dlist_set/get`, `bv_node_dlist_stale_set/get`,
  `bv_node_update_cb_set/get`, `bv_node_update_cb_data_get` declarations; updated
  `bv_node_bbox` doc comment to document vlist fallback (priority 3)
- `src/libbv/bv_private.h` – added `dlist`, `dlist_stale`, `update_cb`,
  `update_cb_data` fields to `struct bv_node`
- `src/libbv/scene.cpp` – implement all new functions; initialize new fields in
  `bv_node_create()`; update `_bbox_cb` for vlist fallback; add `bv/vlist.h` include
- `src/libbv/tests/scene.c` – add `node_vlist_null`, `node_vlist_set_get`,
  `node_vlist_bounds`, `node_bbox_from_vlist`, `node_dlist`, `node_update_cb` tests;
  update `lod_node_update_no_obj` to match new native-node behavior

### Phase 2 completion – obj-insert helpers

The "replace `bu_ptbl_ins(db_objs)` with `bv_scene_add_node()`" goal is addressed by
three new convenience functions (this session):

| Function | Purpose |
|----------|---------|
| `bv_scene_insert_obj(scene, obj)` | Wrap `obj` in a node and add to scene in one call. Migration path for `bu_ptbl_ins(db_objs)`. |
| `bview_insert_obj(view, obj)` | Same as above but operates on a `bview_new*`; auto-creates a scene if the view has none. |
| `bv_scene_find_obj(scene, obj)` | Find the `bv_node` whose `user_data == obj` (linear scan). |

All three functions are declared in `include/bv/defines.h` and implemented in
`src/libbv/scene.cpp`.  4 new tests were added (`scene_insert_obj_null`,
`scene_insert_obj_basic`, `bview_insert_obj_creates_scene`,
`scene_find_obj_basic`).

Note: full `bu_ptbl_ins` call-site migration in the broader BRL-CAD source requires
the complete brlcad tree which is not part of `brlcad_bview`; the helpers are ready
for that integration.

**Additional Phase 1/2 migration conveniences added this session:**

| Function | Purpose |
|----------|---------|
| `bv_scene_remove_obj(scene, obj)` | Inverse of `bv_scene_insert_obj`: find node for obj, remove + destroy it. |
| `bview_remove_obj(view, obj)` | Same but via `bview_new*`; counterpart of `bview_insert_obj`. |
| `bview_sync_from_old(view)` | Re-sync companion from its legacy view. One-liner for the update loop. |
| `bview_sync_to_old(view)` | Push companion changes to the legacy view. Replaces manual `bview_to_old`. |

2 additional tests for sync functions (`bview_sync_from_old`, `bview_sync_to_old`)
and 2 for remove helpers bring the Phase 2 test total to 8 new tests (116 total).

### Phase 3 – View sets + selection (COMPLETE)

Goals and status:
- `bv_scene_from_view_set(const struct bview_set *)` implemented: creates a
  `bv_scene` from all shared scene objects in a `bview_set`.  This is the
  bridge from the old multi-view set concept to the new scene graph model.
- All `bv_set_*` functions marked `DEPRECATED` in `include/bv/view_sets.h`.
- **Selection API** implemented:
  - `bv_node_selected_set()` / `bv_node_selected_get()` — per-node selection.
  - `bv_scene_selected_nodes()` — collects all selected nodes via traverse
    callback into a caller-provided `bu_ptbl`.  Visits all nodes (visible and
    hidden).  Returns count.
  - `bv_scene_select_node()` — wraps `bv_node_selected_set()` with a view
    argument reserved for future pick_set integration.
  - `bv_scene_deselect_all()` — clears selection on every node in the scene;
    returns number of nodes cleared.
- **Multi-view sharing API** implemented (this session):
  - `bv_scene` now carries a `struct bu_ptbl views` list of all `bview_new*`
    instances sharing the scene.
  - `bview_scene_set(view, scene)` updated to register the view in
    `scene->views` (and unregister from the old scene if switching).
  - `bview_destroy()` updated to remove the view from `scene->views` before
    freeing, preventing dangling pointers.
  - `bv_scene_view_count(scene)` — returns the number of views sharing the
    scene.  This is the new-API replacement for iterating
    `bv_set_views()`.
  - `bv_scene_views(scene)` — returns the `bu_ptbl` of `bview_new*` pointers
    for broadcasting events (LoD updates, redraws) to all sharing views.

Files updated:
- `include/bv/defines.h` – `bv_scene_from_view_set`, `bv_scene_selected_nodes`,
  `bv_scene_select_node`, `bv_scene_deselect_all`, `bv_scene_view_count`,
  `bv_scene_views` declarations
- `src/libbv/bv_private.h` – added `struct bu_ptbl views` to `struct bv_scene`
- `src/libbv/scene.cpp` – implement all functions; wire `bview_scene_set` and
  `bview_destroy` to maintain the view list; init/free `scene->views` in
  `bv_scene_create`/`bv_scene_destroy`
- `src/libbv/tests/scene.c` – add `scene_from_vset_null`, `scene_from_vset_empty`,
  `selected_nodes_null`, `selected_nodes_empty`, `selected_nodes_count`,
  `deselect_all`, `select_node`, `scene_view_count_null`,
  `scene_view_count_empty`, `scene_view_count_one`,
  `scene_view_count_shared`, `scene_view_switch` tests

### Phase 4 – LoD integration (COMPLETE)

Goals:
- `bview_lod_update()` wired into the geometry node update pipeline.
- `lod.cpp` logic linked into `bview_lod_node_update()` for LoD-bearing objects.
- **New this session**: complete the native `bv_node` LoD path so the entire
  LoD pipeline can run without touching any legacy types.

Status:
- `bview_lod_node_update(struct bv_node *, const struct bview_new *)` **wired**:
  now calls `bv_mesh_lod_view()` (from `bv/lod.h`) when the wrapped
  `bv_scene_obj` carries `BV_MESH_LOD` data AND a legacy `bview` is reachable
  via `bview_old_get(view)`.
  Updated (Phase 4 completion): for **native** geometry nodes (no wrapped
  `bv_scene_obj`) that carry LoD mesh data in `draw_data`, calls
  `bv_mesh_lod_view_new()` directly.  Nodes with neither legacy obj nor LoD
  data fall back to `dlist_stale = 1`.
- `bv_scene_lod_update(struct bv_scene *, const struct bview_new *)` **added**:
  traverses all `BV_NODE_GEOMETRY` nodes in a scene via `bv_scene_traverse()`,
  calling `bview_lod_node_update()` on each.  Returns count of nodes processed.
- `bview_lod_update(struct bview_new *)` **wired**: now calls
  `bv_scene_lod_update(view->scene, view)` instead of being a no-op placeholder.
- `bv_node_lod_level_set(node, level)` / `bv_node_lod_level_get(node)` **added**:
  public accessor pair for the `lod_level` field.
- **`bv_mesh_lod_view_new(bv_node *n, bview_new *v, int reset)` added** (this
  session): the first fully native LoD function — operates on a `bv_node +
  bview_new` pair without any legacy bridge.  Gets the mesh LoD data from
  `bv_node_draw_data_get(n)` and the view scale from
  `bview_camera_scale_get(v)`, selects the LoD level, and marks
  `dlist_stale = 1` via `bv_node_dlist_stale_set()` if the level changed.
- **`bv_node_draw_data_set/get()` added** (this session): typed accessors for
  the new `draw_data` field on `bv_node` — the analog of
  `bv_scene_obj::draw_data`.  Stores source geometry (LoD mesh, tessellation
  cache, etc.) separately from the rendered display vlist.
- **`scale` field added to `bview_camera`** (this session):
  - `double scale` — half-size of the view in model-space units (analog of
    `gv_scale`; `gv_size = 2 * gv_scale`).  Default: 500.0.
  - `bview_camera_scale_set(view, scale)` / `bview_camera_scale_get(view)`.
  - `bview_from_old()` now copies `old->gv_scale` → `view->camera.scale`.
  - `bview_to_old()` now copies `view->camera.scale` → `old->gv_scale`,
    `old->gv_size`, `old->gv_isize`.
  - `bview_settings_apply()` initializes `camera.scale = 500.0`.
  - `bview_autoview_new()` sets `camera.scale = radius` after computing the
    camera position, so downstream LoD code receives a meaningful scale value.
- **`bview_old_set(view, old)` added** (this session): the symmetric setter for
  `bview_old_get()`.  Associates a legacy `struct bview *` with a `bview_new`
  without performing a full `bview_from_old()` copy.

Files updated:
- `include/bv/defines.h` – `scale` field in `struct bview_camera`;
  `bview_camera_scale_set/get()` declarations; `bview_old_set()` declaration;
  `bv_node_draw_data_set/get()` declarations; updated `bview_lod_node_update`
  doc comment
- `include/bv/lod.h` – `bv_mesh_lod_view_new()` declaration
- `src/libbv/bv_private.h` – `draw_data` field in `struct bv_node`
- `src/libbv/scene.cpp` – implement `bview_camera_scale_set/get()`,
  `bview_old_set()`, `bv_node_draw_data_set/get()`; update
  `bview_from_old()/bview_to_old()/bview_settings_apply()/bview_autoview_new()`
  for scale; update `bview_lod_node_update()` to call `bv_mesh_lod_view_new()`
- `src/libbv/lod.cpp` – implement `bv_mesh_lod_view_new()`
- `src/libbv/tests/scene.c` – add `camera_scale_set_get`, `from_old_scale`,
  `to_old_scale`, `bview_old_set`, `draw_data_set_get`, `lod_view_new_null`,
  `autoview_new_sets_scale` tests

### Phase 5 – obol/Coin3D bridge (LONG TERM)

Goals:
- Add `bv_node_to_sonode()` / `bv_node_from_sonode()` translation functions in
  a new `libbv_obol` bridge library (does NOT require obol to build libbv).
- `bview_new` → `SoSceneManager` adapter.
- Replace `libdm` rendering back-end with obol where feasible.

---

## Deprecation Policy

BRL-CAD policy requires that deprecated symbols:

1. Remain in the API and continue to compile without warnings for at least one
   release cycle after deprecation.
2. Carry a `/* DEPRECATED: use <new_symbol> instead */` comment in the header.
3. Are removed only after a notice in the release notes.

The old API functions (`bv_init`, `bv_free`, `bv_set_init`, etc.) will **not**
be removed until Phase 1 is fully complete and merged into the mainline BRL-CAD
repository.

---

## Current Status

| Subsystem | Phase | Status |
|---|---|---|
| New API definition + implementation | Phase 0 | ✅ COMPLETE |
| Unit tests (26 test cases) | Phase 0 | ✅ COMPLETE |
| `BV_EXPORT` decorators | Phase 0 | ✅ COMPLETE |
| `bview_from_old` basic bridge (camera + viewport) | Phase 0 | ✅ COMPLETE |
| `bview_from_old` appearance copy (grid, axes, colors) | Phase 1 | ✅ COMPLETE |
| `bview_from_old` overlay copy (show_fps) | Phase 1 | ✅ COMPLETE |
| `bview_from_old` scale bridge (`gv_scale` ↔ `camera.scale`) | Phase 1 | ✅ COMPLETE |
| `bview_settings_apply()` — default initialization | Phase 1 | ✅ COMPLETE |
| `bview_to_old()` enhanced — full appearance + overlay + scale round-trip | Phase 1 | ✅ COMPLETE |
| `bview_old_set()` — associate legacy bview without full copy | Phase 1 | ✅ COMPLETE |
| DEPRECATED annotations on `bv_init` / `bv_free` / `bv_settings_init` | Phase 1 | ✅ COMPLETE |
| DEPRECATED annotations on all `bv_set_*` functions | Phase 1+3 | ✅ COMPLETE |
| `bv_scene_obj_to_node()` — wrap legacy obj in new node | Phase 2 | ✅ COMPLETE |
| `bv_scene_from_view()` — build scene from legacy bview | Phase 2 | ✅ COMPLETE |
| `bv_node_bbox()` — AABB of node subtree (native AABB + legacy sphere + vlist) | Phase 2 | ✅ COMPLETE |
| `bv_scene_bbox()` — AABB of entire scene | Phase 2 | ✅ COMPLETE |
| `bview_autoview_new()` — fit camera to scene geometry (sets camera.scale) | Phase 2 | ✅ COMPLETE |
| `bv_node_geometry_get()` — getter for geometry pointer | Phase 2 | ✅ COMPLETE |
| `bv_node_selected_set()` / `bv_node_selected_get()` — selection accessors | Phase 2 | ✅ COMPLETE |
| `bv_node_bounds_set/get/clear()` — native AABB on bv_node | Phase 2 | ✅ COMPLETE |
| `bv_node_vlist_set/get()` — typed vlist accessor (geometry field) | Phase 2 | ✅ COMPLETE |
| `bv_node_vlist_bounds()` — compute AABB from node's vlist | Phase 2 | ✅ COMPLETE |
| `bv_node_dlist_set/get()` — render-backend display list handle | Phase 2 | ✅ COMPLETE |
| `bv_node_dlist_stale_set/get()` — needs-redraw flag | Phase 2 | ✅ COMPLETE |
| `bv_node_update_cb` typedef + `bv_node_update_cb_set/get` | Phase 2 | ✅ COMPLETE |
| `bv_node_draw_data_set/get()` — raw source data (LoD mesh, etc.) | Phase 4 | ✅ COMPLETE |
| `bview_camera.scale` field + `bview_camera_scale_set/get()` | Phase 4 | ✅ COMPLETE |
| `bv_scene_from_view_set()` — build scene from bview_set | Phase 3 | ✅ COMPLETE |
| `bv_scene_selected_nodes()` — collect selected nodes via traverse | Phase 3 | ✅ COMPLETE |
| `bv_scene_select_node()` — select/deselect a single node | Phase 3 | ✅ COMPLETE |
| `bv_scene_deselect_all()` — clear all selections in scene | Phase 3 | ✅ COMPLETE |
| `bv_scene_view_count()` / `bv_scene_views()` — multi-view sharing query | Phase 3 | ✅ COMPLETE |
| `bview_scene_set` tracks views in `bv_scene.views` | Phase 3 | ✅ COMPLETE |
| `bview_destroy` unregisters from scene view list | Phase 3 | ✅ COMPLETE |
| `bview_lod_node_update()` — wired (legacy + native draw_data paths) | Phase 4 | ✅ COMPLETE |
| `bv_scene_lod_update()` — update LoD for all geometry nodes | Phase 4 | ✅ COMPLETE |
| `bview_lod_update()` — wired to `bv_scene_lod_update()` | Phase 4 | ✅ COMPLETE |
| `bv_node_lod_level_set/get()` — public accessor for lod_level field | Phase 4 | ✅ COMPLETE |
| `bv_mesh_lod_view_new()` — native LoD view update (bv_node + bview_new) | Phase 4 | ✅ COMPLETE |
| `bview_companion_create()` — convenience bridge: create + from_old + old_set | Phase 1 | ✅ COMPLETE |
| `libqtcad/QgGL` companion `local_nv` | Phase 1 | ✅ COMPLETE |
| `libqtcad/QgSW` companion `local_nv` | Phase 1 | ✅ COMPLETE |
| `libqtcad/QgModel` companion `empty_nv` | Phase 1 | ✅ COMPLETE |
| `struct ged` gains `ged_gvnv` + `ged_free_view_companions` table | Phase 1 | ✅ COMPLETE |
| `libged/ged.cpp` `ged_init`/`ged_free` use companion table | Phase 1 | ✅ COMPLETE |
| `libged/dm/dm.c` DM-attached view companion | Phase 1 | ✅ COMPLETE |
| `libtclcad/commands.c` Tcl-created view companion | Phase 1 | ✅ COMPLETE |
| `mged/mged_dm.h` + `mged/setup.c` — `vs_nvp` companion in `_view_state` | Phase 1 | ✅ COMPLETE |
| `mged/cmd.c` — ephemeral staging view companion | Phase 1 | ✅ COMPLETE |
| `libdm/swrast/fb-swrast.cpp` — framebuffer view companion | Phase 1 | ✅ COMPLETE |
| Category D tests (quad.cpp, aet.cpp, bv_poly_sketch.c, tor.cpp) | Phase 1 | ✅ COMPLETE |
| Unit tests (110 total — 4 new for companion_create) | Phase 1 | ✅ COMPLETE |
| **Phase 1 COMPLETE** — all 15 bv_init callsites have bview_new companions | Phase 1 | ✅ COMPLETE |
| `bv_scene_insert_obj()` — wrap obj + add to scene in one call | Phase 2 | ✅ COMPLETE |
| `bview_insert_obj()` — insert obj into view's scene (auto-creates scene) | Phase 2 | ✅ COMPLETE |
| `bv_scene_find_obj()` — find node by legacy bv_scene_obj pointer | Phase 2 | ✅ COMPLETE |
| `bv_scene_remove_obj()` — inverse of insert_obj | Phase 2 | ✅ COMPLETE |
| `bview_remove_obj()` — remove from view's scene | Phase 2 | ✅ COMPLETE |
| `bview_sync_from_old()` — re-sync companion from legacy view | Phase 1 | ✅ COMPLETE |
| `bview_sync_to_old()` — push companion changes to legacy view | Phase 1 | ✅ COMPLETE |
| `bview_name_get()` / `bview_name_set()` — name accessor pair | Phase 1 | ✅ COMPLETE |
| `bview_redraw_callback_get()` / `bview_redraw_callback_data_get()` — getter pair | Phase 1 | ✅ COMPLETE |
| `bv_node_name_set()` — rename a node after creation | Phase 2 | ✅ COMPLETE |
| `bv_scene_node_count()` — convenience count of top-level nodes | Phase 2 | ✅ COMPLETE |
| `bv_scene_clear()` — remove and destroy all top-level nodes | Phase 2 | ✅ COMPLETE |
| `bv_scene_find_all_nodes()` — collect all nodes matching a name | Phase 2 | ✅ COMPLETE |
| `bv_node_is_descendant()` — test ancestor/descendant relationship | Phase 2 | ✅ COMPLETE |
| **Phase 2 COMPLETE** — all scene-object bridge helpers implemented | Phase 2 | ✅ COMPLETE |
| Replace `bu_ptbl_ins(gv_objs.db_objs)` with `bv_scene_add_node()` | Phase 2 | ℹ️ N/A in brlcad_bview (`bv_scene_add_node` already implemented; call-site migration requires full brlcad source) |
| obol/Coin3D bridge | Phase 5 | 🔲 PLANNED |

---

## How to Write New Code Against the New API

```c
#include "bv.h"   /* includes bv/defines.h which has the EXPERIMENTAL block */

/* Create a scene with two geometry nodes */
struct bv_scene  *scene = bv_scene_create();
struct bv_node   *geom1 = bv_node_create("part_A", BV_NODE_GEOMETRY);
struct bv_node   *geom2 = bv_node_create("part_B", BV_NODE_GEOMETRY);

bv_scene_add_node(scene, geom1);
bv_scene_add_node(scene, geom2);

/* Create a view pointing at the scene */
struct bview_new *view = bview_create("primary");
bview_scene_set(view, scene);

struct bview_viewport vp = { 1920, 1080, 96.0 };
bview_viewport_set(view, &vp);

struct bview_camera cam;
VSET(cam.position, 0.0, -500.0, 0.0);
VSET(cam.target,   0.0,    0.0, 0.0);
VSET(cam.up,       0.0,    0.0, 1.0);
cam.fov         = 0.0;
cam.perspective = 0;   /* orthographic */
bview_camera_set(view, &cam);

/* ... render ... */

bview_destroy(view);
bv_scene_destroy(scene);   /* also destroys geom1, geom2 */
```

For a new view without an existing `struct bview`, use `bview_settings_apply()` to
set sensible defaults matching the legacy `bv_init()` defaults:

```c
struct bview_new *view = bview_create("standalone");
bview_settings_apply(view);  /* replaces bv_init() + bv_settings_init() */
/* view is now ready with the same camera/viewport/appearance defaults */
```

For code that must interoperate with existing `struct bview` callers during the
transition, use the migration helpers:

```c
/* Wrap an existing bview in the new API (Phase 1) */
struct bview_new *nv = bview_create("compat_view");
bview_from_old(nv, existing_gvp);      /* copies camera, viewport, appearance, stores pointer */

/* ... use new API ... */

bview_to_old(nv, existing_gvp);       /* push changes back for legacy code */
bview_destroy(nv);
```

For incremental caller migration the convenience wrapper does the same in one call:

```c
/* ---- existing code (kept unchanged) ---- */
BU_ALLOC(v, struct bview);
bv_init(v, &ged_views);

/* ---- one-line addition for new-API callers ---- */
struct bview_new *nv = bview_companion_create("default", v);
/* bview_old_get(nv) == v; camera.scale synced from v->gv_scale */

/* ---- syncing during the per-frame update loop ---- */
bview_sync_from_old(nv);          /* re-pull from v (if legacy code modified v) */
/* ... call new-API rendering / LoD / selection functions ... */
bview_sync_to_old(nv);            /* push any new-API changes back to v */

/* ---- teardown ---- */
bview_destroy(nv);          /* companion destroyed */
bv_free(v);                 /* legacy view freed as before */
BU_FREE(v, struct bview);
```

To snapshot an entire legacy `struct bview` object list as a new-API scene (Phase 2):

```c
/* Convert a bview's scene objects to a new bv_scene (read-only snapshot) */
struct bv_scene *scene = bv_scene_from_view(gvp);
/* Each bv_scene_obj is wrapped as a bv_node; original pointer in user_data */
bv_scene_traverse(scene, my_render_callback, NULL);
bv_scene_destroy(scene);  /* destroys wrapper nodes; originals still in bview */
```

To wrap an individual `bv_scene_obj` (Phase 2):

```c
/* Convert one legacy scene object to a bv_node */
struct bv_node *n = bv_scene_obj_to_node(my_scene_obj);
/* Recover original: bv_node_user_data_get(n) == my_scene_obj */
bv_node_destroy(n);   /* destroys wrapper; original bv_scene_obj is NOT freed */
```

To insert a legacy object into a new-API scene (Phase 2 — replaces `bu_ptbl_ins`):

```c
/* Instead of: bu_ptbl_ins(&v->gv_objs.db_objs, (long *)obj); */

/* Option A — have a scene handle: */
bv_scene_insert_obj(scene, obj);
/* node = bv_scene_obj_to_node(obj) + bv_scene_add_node(scene, node) */

/* Option B — have a bview_new*: */
bview_insert_obj(nv, obj);
/* Creates a scene on demand if nv has none; returns the wrapper bv_node */

/* Find the node for a known obj: */
struct bv_node *n = bv_scene_find_obj(scene, obj);
/* Returns NULL if obj not in scene */
```

To migrate a multi-view application from `bview_set` to the new API (Phase 3):

```c
/* Convert shared scene objects to a new-API scene */
struct bv_scene *scene = bv_scene_from_view_set(&my_vset);

/* Create new-API view wrappers for each legacy view */
struct bu_ptbl *views = bv_set_views(&my_vset);
for (size_t i = 0; i < BU_PTBL_LEN(views); i++) {
    struct bview *old_v = (struct bview *)BU_PTBL_GET(views, i);
    struct bview_new *nv = bview_create(bu_vls_cstr(&old_v->gv_name));
    bview_from_old(nv, old_v);
    bview_scene_set(nv, scene);   /* all views share the same scene */
    /* ... register nv with new rendering pipeline ... */
}
bv_scene_destroy(scene);
```

To compute bounding boxes and auto-fit the camera (Phase 2):

```c
/* After converting a legacy bview to a scene ... */
struct bv_scene *scene = bv_scene_from_view(gvp);

/* Compute the scene's AABB (all visible geometry nodes) */
point_t bmin, bmax;
if (bv_scene_bbox(scene, &bmin, &bmax)) {
    printf("scene spans (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)\n",
	   bmin[X], bmin[Y], bmin[Z], bmax[X], bmax[Y], bmax[Z]);
}

/* Auto-fit the camera to show all geometry */
struct bview_new *view = bview_create("primary");
bview_settings_apply(view);
bview_scene_set(view, scene);
bview_autoview_new(view, scene, BV_AUTOVIEW_SCALE_DEFAULT);
/* Camera is now positioned to frame the entire scene */

bv_scene_destroy(scene);
bview_destroy(view);
```

To mark a geometry node as needing LoD update (Phase 4 — wired):

```c
/* Update LoD for all geometry nodes in a scene for the current view */
bview_lod_update(active_view);          /* scene-level: calls bv_scene_lod_update() */
bv_scene_lod_update(scene, active_view); /* same, explicit scene argument */

/* Per-node: if s->s_type_flags & BV_MESH_LOD, calls bv_mesh_lod_view();
 * otherwise marks s_dlist_stale = 1 for fallback regeneration. */
bview_lod_node_update(geom_node, active_view);
```

To manage node selection (Phase 3):

```c
/* Select a node */
bv_scene_select_node(n, 1, view);

/* Collect all selected nodes */
struct bu_ptbl selected;
bu_ptbl_init(&selected, 8, "selection");
int n_selected = bv_scene_selected_nodes(scene, &selected);
for (size_t i = 0; i < BU_PTBL_LEN(&selected); i++) {
    struct bv_node *sn = (struct bv_node *)BU_PTBL_GET(&selected, i);
    printf("selected: %s\n", bv_node_name_get(sn));
}
bu_ptbl_free(&selected);

/* Deselect everything */
bv_scene_deselect_all(scene, NULL);
```

To set native AABB bounds on a pure new-API geometry node (Phase 2 — no legacy obj needed):

```c
/* Geometry node for a unit cube */
struct bv_node *n = bv_node_create("unit_cube", BV_NODE_GEOMETRY);
point_t mn, mx;
VSET(mn, -0.5, -0.5, -0.5);
VSET(mx,  0.5,  0.5,  0.5);
bv_node_bounds_set(n, mn, mx);

/* The node now participates fully in bv_scene_bbox() and bview_autoview_new()
 * without wrapping a legacy bv_scene_obj. */
bv_scene_add_node(scene, n);

/* To clear (revert to legacy obj sphere fallback): */
bv_node_bounds_clear(n);
```

To share one scene across multiple views (Phase 3):

```c
struct bv_scene  *scene = bv_scene_create();
struct bview_new *v1    = bview_create("view_left");
struct bview_new *v2    = bview_create("view_right");

bview_scene_set(v1, scene);   /* registers v1 in scene->views */
bview_scene_set(v2, scene);   /* registers v2 in scene->views */

/* Both views share the same scene graph — this is the new-API equivalent of
 * bv_set_add_view() + bview_set.  The scene owns the geometry; the views own
 * their own cameras and viewports. */
printf("%zu views sharing scene\n", bv_scene_view_count(scene));   /* → 2 */

/* Broadcast a LoD update to all sharing views */
const struct bu_ptbl *views = bv_scene_views(scene);
for (size_t i = 0; i < BU_PTBL_LEN(views); i++) {
    struct bview_new *v = (struct bview_new *)BU_PTBL_GET(views, i);
    bv_scene_lod_update(scene, v);
}

/* When a view is no longer needed, destroy it — it unregisters automatically */
bview_destroy(v2);   /* scene->views now has only v1 */

bview_destroy(v1);
bv_scene_destroy(scene);
```

To create a fully native geometry node with a vlist (Phase 2 — no `bv_obj_get()` needed):

```c
#include "bv.h"   /* bv/vlist.h is pulled in via bv.h */

struct bu_list vlfree;
struct bu_list vlist;
BU_LIST_INIT(&vlfree);
BU_LIST_INIT(&vlist);

/* Build vlist content */
point_t p0, p1;
VSET(p0, 0.0, 0.0, 0.0);
VSET(p1, 1.0, 1.0, 1.0);
BV_ADD_VLIST(&vlfree, &vlist, p0, BV_VLIST_LINE_MOVE);
BV_ADD_VLIST(&vlfree, &vlist, p1, BV_VLIST_LINE_DRAW);

/* Create the node and attach the vlist */
struct bv_node *n = bv_node_create("my_line", BV_NODE_GEOMETRY);
bv_node_vlist_set(n, &vlist);

/* Optional: register an update callback so the rendering backend
 * can rebuild the vlist when dlist_stale is set */
bv_node_update_cb_set(n, my_update_fn, my_update_data);

/* bv_scene_bbox() and bview_autoview_new() include this node via the
 * vlist fallback in bv_node_bbox — no bv_node_bounds_set() call required. */
bv_scene_add_node(scene, n);

/* At teardown: detach vlist before destroying node (caller frees it) */
bv_node_vlist_set(n, NULL);
bv_node_destroy(n);   /* does NOT free the vlist */
BV_FREE_VLIST(&vlfree, &vlist);
bv_vlist_cleanup(&vlfree);
```
