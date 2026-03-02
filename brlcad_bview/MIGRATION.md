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

### Phase 1 – View lifecycle (PLANNED)

Goals:
- New code creates views with `bview_create()` instead of allocating a
  `struct bview` and calling `bv_init()`.
- `bv_init()` and `bv_free()` are marked `DEPRECATED` in the header.
- Migration helper `bview_from_old()` is enhanced to copy remaining fields
  (settings, knobs, etc.).
- Add `bview_settings_apply()` to set default appearance from `bv_settings_init`
  equivalents via the new API.

Files to update:
- `src/libbv/util.cpp` – add deprecation comments to `bv_init` / `bv_free`
- `include/bv/util.h` – add `DEPRECATED` annotations
- `src/libbv/scene.cpp` – enhance `bview_from_old` / `bview_to_old`
- `src/libbv/tests/scene.c` – add migration round-trip tests for all fields

### Phase 2 – Scene objects (PLANNED)

Goals:
- New geometry creation uses `bv_node_create(name, BV_NODE_GEOMETRY)` +
  `bv_node_geometry_set()` instead of `bv_obj_get()` / `bv_scene_obj` struct
  manipulation.
- `bv_scene_add_node()` replaces direct `bu_ptbl_ins()` into `gv_objs.db_objs`.
- `bv_scene_traverse()` replaces manual iteration over `gv_objs` tables.
- Display list (`struct display_list`) population migrated to traverse callbacks.

Files to update:
- `include/bv/defines.h` – mark old scene object accessors `DEPRECATED`
- `src/libbv/util.cpp` – update `bv_scene_obj_bound`, `bv_autoview`, etc.
- Callers in `libged`, `mged`, `archer` (tracked separately per-caller)

### Phase 3 – View sets (PLANNED)

Goals:
- `struct bview_set` (flat table of views) migrated to: one `bv_scene` (shared
  scene graph) + multiple `bview_new` instances pointing at it.
- `bv_set_init` / `bv_set_free` / `bv_set_add_view` / `bv_set_rm_view` marked
  `DEPRECATED`.
- `bv_set_views()` replaced by querying `bview_scene_get()` then inspecting which
  views reference that scene.

### Phase 4 – LoD integration (PLANNED)

Goals:
- `bview_lod_update()` wired into the geometry node update pipeline.
- `lod.cpp` logic refactored to operate on `bv_node` trees instead of raw
  `bv_scene_obj` lists.

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
| BV_EXPORT decorators | Phase 0 | ✅ COMPLETE |
| `bview_from_old` / `bview_to_old` basic bridge | Phase 1 | ⚙ PARTIAL |
| View lifecycle migration | Phase 1 | 🔲 PLANNED |
| Scene object migration | Phase 2 | 🔲 PLANNED |
| View set migration | Phase 3 | 🔲 PLANNED |
| LoD integration | Phase 4 | 🔲 PLANNED |
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

For code that must interoperate with existing `struct bview` callers during the
transition, use the migration helpers:

```c
/* Wrap an existing bview in the new API */
struct bview_new *nv = bview_create("compat_view");
bview_from_old(nv, existing_gvp);      /* copies camera, viewport, stores pointer */

/* ... use new API ... */

bview_to_old(nv, existing_gvp);       /* push changes back for legacy code */
bview_destroy(nv);
```
