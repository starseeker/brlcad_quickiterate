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
enum bv_node_type bv_node_type_get(const struct bv_node *node);
const char       *bv_node_name_get(const struct bv_node *node);
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

/* Viewport, appearance, overlay, pick set */
void bview_viewport_set(struct bview_new *view, const struct bview_viewport *vp);
const struct bview_viewport *bview_viewport_get(const struct bview_new *view);

void bview_appearance_set(struct bview_new *view, const struct bview_appearance *app);
const struct bview_appearance *bview_appearance_get(const struct bview_new *view);

void bview_overlay_set(struct bview_new *view, const struct bview_overlay *ov);
const struct bview_overlay *bview_overlay_get(const struct bview_new *view);

void bview_pick_set_set(struct bview_new *view, const struct bview_pick_set *ps);
const struct bview_pick_set *bview_pick_set_get(const struct bview_new *view);

/* Redraw callback */
typedef void (*bview_redraw_cb)(struct bview_new *, void *);
void bview_redraw_callback_set(struct bview_new *view, bview_redraw_cb cb, void *data);
void bview_redraw(struct bview_new *view);

/* Migration helpers (bridge to old API during transition) */
void           bview_from_old(struct bview_new *view, const struct bview *old);
void           bview_to_old(const struct bview_new *view, struct bview *old);
struct bview  *bview_old_get(const struct bview_new *view);

/* Default initialization (replaces bv_init + bv_settings_init) */
void bview_settings_apply(struct bview_new *view);

/* Auto-fit camera to scene geometry (replaces bv_autoview) */
#define BV_AUTOVIEW_SCALE_DEFAULT -1
int  bview_autoview_new(struct bview_new *view, const struct bv_scene *scene,
                        double scale_factor);

/* LoD per-node update hook */
int  bview_lod_node_update(struct bv_node *node, const struct bview_new *view);
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

### Phase 1 – View lifecycle (LARGELY COMPLETE)

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

Files updated:
- `src/libbv/scene.cpp` – `bview_settings_apply()`; enhanced `bview_from_old`
  and `bview_to_old`
- `include/bv/util.h` – `DEPRECATED` comments on `bv_init`, `bv_free`,
  `bv_settings_init`, `bv_mat_aet`
- `include/bv/view_sets.h` – `DEPRECATED` comments on all `bv_set_*` functions
- `src/libbv/tests/scene.c` – `settings_apply`, `settings_apply_null`,
  `settings_apply_idempotent`, `to_old_appearance`, `to_old_overlay`, `to_old_null`

Remaining for Phase 1 completion:
- Migrate internal callers of `bv_init` / `bv_free` to use `bview_create()` /
  `bview_destroy()` (tracked in Phase 1 callers list below)

### Phase 2 – Scene objects (IN PROGRESS)

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
  visible `BV_NODE_GEOMETRY` nodes and reading their wrapped `bv_scene_obj`
  bounding sphere (`s_center` + `s_size`).  Returns 0 for empty/invisible
  subtrees.
- `bv_scene_bbox(const struct bv_scene *, point_t *, point_t *)` implemented:
  computes AABB of an entire scene (all top-level nodes and their subtrees).
  Equivalent to merging `bv_node_bbox()` results for each top-level node.
- `bview_autoview_new(struct bview_new *, const struct bv_scene *, double)`
  implemented: analog of `bv_autoview()` for the new scene graph API.  Uses
  `bv_scene_bbox()` to compute the scene extent, then repositions the camera
  so the scene fits the viewport.  `BV_AUTOVIEW_SCALE_DEFAULT` (−1) uses the
  same 2× radial factor as the legacy function.  Returns 0 if the scene is
  empty (camera unchanged).

Files updated:
- `include/bv/defines.h` – `bv_node_bbox`, `bv_scene_bbox`,
  `bview_autoview_new`, `bview_lod_node_update`, `BV_AUTOVIEW_SCALE_DEFAULT`
  macro declarations; forward declarations for `struct bv_scene_obj` and
  `struct bview_set`; corrected doc comment for `bview_autoview_new`
- `src/libbv/scene.cpp` – implement all four functions; add `_bbox_state`
  internal struct and `_bbox_cb` traverse callback
- `src/libbv/tests/scene.c` – add `obj_to_node_null`, `obj_to_node_basic`,
  `obj_to_node_children`, `scene_from_view_null`, `scene_from_view_empty`,
  `scene_from_view_objs`, `node_bbox_null`, `node_bbox_no_geom`,
  `scene_bbox_null`, `scene_bbox_empty`, `scene_bbox_invisible`,
  `scene_bbox_visible`, `autoview_null`, `autoview_empty`,
  `autoview_single_obj`, `autoview_scale_factor` tests

Remaining for Phase 2 completion:
- New geometry creation: `bv_node_create(BV_NODE_GEOMETRY)` + `bv_node_geometry_set()`
  instead of `bv_obj_get()` + direct struct mutation
- Replace direct `bu_ptbl_ins()` into `gv_objs.db_objs` with `bv_scene_add_node()`
- Extend `bv_node_bbox` to handle nodes without a wrapped `bv_scene_obj` (once
  native geometry storage is added in a later phase)

### Phase 3 – View sets (IN PROGRESS)

Goals and status:
- `bv_scene_from_view_set(const struct bview_set *)` implemented: creates a
  `bv_scene` from all shared scene objects in a `bview_set`.  This is the
  bridge from the old multi-view set concept to the new scene graph model.
- All `bv_set_*` functions marked `DEPRECATED` in `include/bv/view_sets.h`.

Files updated:
- `include/bv/defines.h` – `bv_scene_from_view_set` declaration
- `src/libbv/scene.cpp` – implement function
- `src/libbv/tests/scene.c` – add `scene_from_vset_null`, `scene_from_vset_empty`

Remaining for Phase 3 completion:
- Migrate callers of `bv_set_add_view` / `bv_set_rm_view` to use
  `bview_scene_set(view, scene)` / `bview_scene_set(view, NULL)`
- Multiple `bview_new` instances sharing one `bv_scene` replaces the concept of
  a `bview_set` with a flat list of views

### Phase 4 – LoD integration (STUB COMPLETE)

Goals:
- `bview_lod_update()` wired into the geometry node update pipeline.
- `lod.cpp` logic refactored to operate on `bv_node` trees instead of raw
  `bv_scene_obj` lists.

Status:
- `bview_lod_node_update(struct bv_node *, const struct bview_new *)` stubbed:
  accepts a `BV_NODE_GEOMETRY` node, retrieves the wrapped `bv_scene_obj`, and
  marks `s_dlist_stale = 1` so the legacy rendering path regenerates it.
  The TODO comment is in place for wiring into `lod.cpp`.  4 tests added.

Files updated:
- `include/bv/defines.h` – `bview_lod_node_update` declaration with full doc
- `src/libbv/scene.cpp` – stub implementation
- `src/libbv/tests/scene.c` – add `lod_node_update_null`,
  `lod_node_update_non_geom`, `lod_node_update_no_obj`,
  `lod_node_update_stale` tests

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
| `bview_settings_apply()` — default initialization | Phase 1 | ✅ COMPLETE |
| `bview_to_old()` enhanced — full appearance + overlay round-trip | Phase 1 | ✅ COMPLETE |
| DEPRECATED annotations on `bv_init` / `bv_free` / `bv_settings_init` | Phase 1 | ✅ COMPLETE |
| DEPRECATED annotations on all `bv_set_*` functions | Phase 1+3 | ✅ COMPLETE |
| `bv_scene_obj_to_node()` — wrap legacy obj in new node | Phase 2 | ✅ COMPLETE |
| `bv_scene_from_view()` — build scene from legacy bview | Phase 2 | ✅ COMPLETE |
| `bv_node_bbox()` — AABB of a node subtree | Phase 2 | ✅ COMPLETE |
| `bv_scene_bbox()` — AABB of entire scene | Phase 2 | ✅ COMPLETE |
| `bview_autoview_new()` — fit camera to scene geometry | Phase 2 | ✅ COMPLETE |
| `bv_scene_from_view_set()` — build scene from bview_set | Phase 3 | ✅ COMPLETE |
| `bview_lod_node_update()` — per-node LoD update hook (stub) | Phase 4 | ✅ COMPLETE (stub) |
| Unit tests (72 total — 29 new Phase 1–4 tests) | Phase 1–4 | ✅ COMPLETE |
| Internal callers migrated to new lifecycle API | Phase 1 | 🔲 PLANNED |
| New geometry creation via `bv_node_create()` | Phase 2 | 🔲 PLANNED |
| `bv_set_add_view` callers migrated to `bview_scene_set` | Phase 3 | 🔲 PLANNED |
| `lod.cpp` wired into `bview_lod_node_update()` | Phase 4 | 🔲 PLANNED |
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

To mark a geometry node as needing LoD update (Phase 4 stub):

```c
/* Iterate over the scene and mark all geometry stale for the current view */
struct bv_node *root = bv_scene_root(scene);
/* (walk the tree yourself, or wait for the Phase 4 traverse-based update) */
bview_lod_node_update(geom_node, active_view);
/* geom_node's wrapped bv_scene_obj will have s_dlist_stale set, triggering
 * regeneration in the legacy rendering path on the next draw cycle. */
```
