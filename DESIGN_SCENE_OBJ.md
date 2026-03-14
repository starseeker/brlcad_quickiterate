# ft_scene_obj Refactoring Design

*This document records the design analysis and planned migration toward having
all primitive-specific drawing logic live in librt `ft_scene_obj` callbacks,
with libged's `draw_scene` reduced to a simple traversal driver.*

---

## 1. Current State (as of 2026-03-09)

### 1.1 Data structures

#### `bsg_shape` (`include/bsg/defines.h`)

The scene-object container.  Key fields relevant to drawing:

| Field | Type | Purpose |
|---|---|---|
| `s_i_data` | `void *` | Points to `draw_update_data_t` (libged-level, never NULL during draw) |
| `s_path` | `void *` | `struct db_full_path *` for the instance path |
| `dp` | `void *` | `struct directory *` fallback (no full path) |
| `s_mat` | `mat_t` | Cumulative path transform |
| `have_bbox` | `int` | 1 when `bmin`/`bmax`/`s_center`/`s_size` are valid |
| `bmin`, `bmax` | `point_t` | World-space AABB corners |
| `s_placeholder` | `int` | 0=real, 1=AABB wireframe, 2=OBB wireframe |
| `draw_data` | `void *` | LoD context pointer (per-view shape) |
| `mesh_c` | `bsg_mesh_lod_context *` | **NEW (2026-03-09)** — LoD context forwarded from pipeline |
| `s_obb_pts[24]` | `fastf_t` | **NEW (2026-03-09)** — 8 OBB corner points × 3 coords |
| `s_have_obb` | `int` | **NEW (2026-03-09)** — 1 when `s_obb_pts` is valid |
| `s_os` | `bsg_obj_settings *` | Drawing mode (`s_dmode`), colour, etc. |
| `csg_obj`, `mesh_obj` | `int` | Classification flags (set by drawing code) |
| `adaptive_wireframe` | `int` | 1 when view is in adaptive CSG wireframe mode |
| `vlfree` | `struct bu_list *` | Free-list for vlist memory |

#### `draw_update_data_t` (`include/ged/view.h`)

Set into `s->s_i_data` by the libged pipeline before any `ft_scene_obj` call.
This is the libged-to-librt interface for per-draw state:

```c
struct draw_update_data_t {
    struct db_i          *dbip;    /* database instance */
    struct db_full_path  *fp;      /* full instance path */
    const struct bn_tol  *tol;     /* raytracing tolerance */
    const struct bg_tess_tol *ttol; /* tessellation tolerance */
    bsg_mesh_lod_context *mesh_c;  /* LoD cache context */
    struct resource      *res;     /* per-thread rt resource */
    struct DbiState      *dbis;    /* may be NULL outside DbiState context */
};
```

The `dbis` pointer is the only remaining coupling to libged internals.

#### `ft_scene_obj` signature (`include/rt/functab.h`)

```c
int (*ft_scene_obj)(bsg_shape * /*s*/,
                    struct directory * /*dp*/,
                    struct db_i * /*dbip*/,
                    const struct bg_tess_tol * /*ttol*/,
                    const struct bn_tol * /*tol*/,
                    const bsg_view * /*v*/);
```

All the parameters needed to perform any drawing mode are present or reachable:
- `s` — full scene-object state including `s_mat`, `s_os`, `mesh_c`, `s_obb_pts`
- `dp` + `dbip` — enough to call `rt_db_get_internal` or `ft_mat`
- `ttol`/`tol` — tessellation and raytracing tolerances
- `v` — the target view (adaptive wireframe scale, LoD camera distance, etc.)

### 1.2 Current `draw_scene` dispatch flow (`src/libged/draw.cpp`)

```
draw_scene(s, v)
 │
 ├─ mode 3 (evaluated wireframe) → draw_m3(s)         [wireframe_eval.c]
 ├─ mode 5 (point cloud)         → draw_points(s)     [points_eval.c]
 │
 ├─ setup phase: propagate d->dbis results onto s
 │   ├─ s->mesh_c ← d->mesh_c
 │   ├─ late-set have_bbox from d->dbis->bboxes
 │   └─ cache OBB corners into s->s_obb_pts/s->s_have_obb
 │
 ├─ BoT + adaptive_plot_mesh    → bot_adaptive_plot(s, v)
 ├─ BREP + adaptive_plot_mesh   → brep_adaptive_plot(s, v)
 │
 ├─ mark csg_obj=1, mesh_obj=0 (CSG adaptive path)
 │
 ├─ lazy AABB guard (uses s->s_have_obb/s->s_obb_pts — no dbis access)
 │
 └─ rt_db_get_internal → switch(s_dmode)
     ├─ 0/1  → wireframe_plot
     ├─ 2/4  → prim_tess (shaded / hidden-line)
     ├─ 3    → [unreachable — handled above]
     └─ 5    → [unreachable — handled above]
```

### 1.3 `rt_generic_scene_obj` (`src/librt/primitives/generic.c`)

As of 2026-03-09 commit, now handles:
- `!have_bbox` early-return: draws OBB placeholder from `s->s_obb_pts` without
  touching `DbiState`.
- All `s_dmode` variants via `rt_wireframe_plot`, `rt_shaded_plot`, `rt_sample_pnts`.

---

## 2. Key Design Questions and Answers

### 2.1 Can `rt_comb_scene_obj` absorb `draw_m3` (mode-3 evaluated wireframes)?

**Yes, it can — and it should.**

`draw_m3` (`wireframe_eval.c`) uses:
- `s->s_path` — `db_full_path *` (on `bsg_shape`)
- `s->vlfree` — (on `bsg_shape`)
- `d->dbip` — available as `ft_scene_obj`'s `dbip` parameter
- `d->tol` / `d->ttol` — available as `tol`/`ttol` parameters
- `s->s_vlist` — (on `bsg_shape`)

All inputs are already present in the `ft_scene_obj` signature.  The comb
functab entry currently has `NULL` for `scene_obj`; wiring it to
`rt_comb_scene_obj` and moving `draw_m3`'s body there would:
1. Eliminate the `extern "C" int draw_m3(bsg_shape *s)` forward declaration.
2. Remove the `s_dmode == 3` special-case dispatch from `draw_scene`.
3. Place evaluated-wireframe logic with the comb primitive where it belongs.

**Structural problem with `draw_m3`:** It calls `rt_gettrees` / `rt_clean` on
a fresh `rt_i`, which means it re-parses the entire subtree every time.  This
is expensive and has no caching.  For a scene-graph architecture the evaluated
wireframe result should be cached in the `bsg_shape` (or a node attached to
it) so re-renders do not repeat the raytrace.

### 2.2 Does `ft_scene_obj` have enough to support adaptive CSG wireframe updates?

**Yes — resolved with the addition of `s_res` (session 8).**

Adaptive CSG wireframe (`adaptive_wireframe = 1`) updates when the view scale
changes.  The per-frame update path currently goes through
`csg_wireframe_update` in `BViewState::redraw()`, which needs:

| Needed | Available in ft_scene_obj? |
|---|---|
| `dbip` | ✓ parameter |
| `tol` / `ttol` | ✓ parameters |
| `v` (view, for scale/curve_scale) | ✓ parameter |
| `s_mat` | ✓ on `bsg_shape` |
| `struct resource *res` | ✓ now on `bsg_shape` as `s_res` (session 8) |

`s_res` is forwarded from `draw_update_data_t::res` by draw_scene's setup
phase.  `ft_scene_obj` callbacks call `rt_db_get_internal(dp, dbip, NULL,
s->s_res)` directly without seeing `draw_update_data_t`.

### 2.3 `!have_bbox` — what should `ft_scene_obj` do?

**Check the pipeline cache first; return without drawing if not yet available.**

The `ft_scene_obj` implementation should follow this pattern (already
implemented for the generic case):

```
if (!s->have_bbox) {
    /* Try to late-populate from shape fields pre-set by the setup phase */
    if (s->s_have_obb) {
        /* draw OBB placeholder wireframe (per-view child) */
    }
    return BRLCAD_OK;  /* not an error — shape is queued for retry */
}
/* bbox is valid → proceed to actual geometry */
```

The caller (`draw_scene`) is responsible for pre-populating `s->have_bbox` and
`s->s_obb_pts` from the pipeline cache (DbiState) **before** calling
`ft_scene_obj`.  The callback itself never needs to see DbiState.

### 2.4 `ft_mat` and matrix handling

`ft_mat` (`rt_comb_mat` for combs, `rt_generic_xform` for most primitives)
applies a 4×4 transform to an `rt_db_internal` in-place.  Historically the
only matrix path was `rt_db_get_internal(…, s_mat, …)`, which bakes the
transform into the cracked internal before use.

For the drawing pipeline the relevant trade-off is:

| Approach | Cost | World-space or object-space vlists? |
|---|---|---|
| `rt_db_get_internal(…, s_mat, …)` | crack + transform every call | world-space |
| `rt_db_get_internal(…, NULL, …)` then `ft_mat` | crack + apply transform | world-space |
| `rt_db_get_internal(…, NULL, …)` + renderer applies `s_mat` | crack once, cache, re-use | object-space |

For the OI-style scene graph the preferred approach is **object-space vlists
with the renderer applying `s_mat`** at draw time — this allows geometry to be
shared across instances (the same BoT drawn in multiple positions via different
`bsg_shape` nodes sharing one vlist payload).  `ft_mat` is not needed in this
model; `rt_db_get_internal(…, NULL, …)` provides the object-space internal.

**Short-term (this phase):** `rt_generic_scene_obj` currently uses
`rt_db_get_internal(…, NULL, …)` and does NOT apply `s_mat` — consistent with
the OI direction.  `draw_scene` historically called
`rt_db_get_internal(…, s->s_mat, …)` to get world-space vlists.  Both
approaches work for non-instanced rendering but only the object-space approach
scales to the scene-graph instancing model.

---

## 3. Target Architecture

### 3.1 `draw_scene` as a thin traversal driver

```
draw_scene(s, v)
 │
 ├─ setup phase (still in libged — only place dbis is touched)
 │   ├─ s->mesh_c  ← d->mesh_c
 │   ├─ s->s_res   ← d->res            [DONE session 8]
 │   ├─ late-set s->have_bbox from d->dbis->bboxes
 │   └─ cache OBB into s->s_obb_pts / s->s_have_obb
 │
 ├─ children loop (container objects without s_i_data)
 │
 └─ OBJ[dp->d_minor_type].ft_scene_obj(s, dp, dbip, ttol, tol, v)
```

All primitive-specific logic — LoD for BoTs, NURBS for BREPs, evaluated
wireframe for combs, point-cloud sampling, shading, CSG adaptive update —
lives in the per-primitive `ft_scene_obj` implementation.

### 3.2 Required changes not yet made (all DONE as of 2026-03-14)

All items below have been implemented.  The section is preserved as a
historical record of the design decisions made during the migration.

1. ~~**Add `struct resource *s_res` to `bsg_shape`**~~ — **DONE** (2026-03-09 session 8):
   `s_res` added to `bsg_shape`; set from `d->res` in draw_scene's setup phase;
   `rt_generic_scene_obj` uses `s->s_res` (or falls back to `&rt_uniresource`).

2. ~~**`rt_comb_scene_obj`**~~ — **DONE**: Implemented in
   `src/librt/comb/comb.c` (`rt_comb_scene_obj`) + `src/librt/comb/comb_scene_obj.c`
   (`rt_comb_eval_m3`, absorbing `draw_m3`).  Wired into `table.cpp` slot
   `ID_COMBINATION`.  The shim `draw_m3()` in `wireframe_eval.c` remains for
   out-of-tree callers but is no longer called from `draw_scene`.

3. ~~**`rt_bot_scene_obj`**~~ — **DONE**: Implemented in
   `src/librt/primitives/bot/bot_scene_obj.cpp`, absorbing `bot_adaptive_plot`.
   Uses `s->mesh_c` for LoD key lookup; uses `s->s_obb_pts` for OBB placeholder.
   Obol SoNode path guarded by `SoDB::isInitialized()`.

4. ~~**`rt_brep_scene_obj`**~~ — **DONE**: Implemented in
   `src/librt/primitives/brep/brep.cpp` (`rt_brep_scene_obj`), absorbing
   `brep_adaptive_plot`.  Mode-1 adaptive tessellation; Obol SoNode output.

5. ~~**`draw_scene` simplification**~~ — **DONE**: `draw_scene` in
   `src/libged/draw.cpp` now has zero per-primitive special cases.  The sole
   dispatch is `OBJ[dp->d_minor_type].ft_scene_obj()` → per-type implementation.
   The orphaned `extern "C" int draw_points` forward declaration was removed
   (2026-03-14); `draw_points` in `points_eval.c` is now superseded by mode-5
   handling in `rt_generic_scene_obj`.

6. ~~**Object-space vlists**~~ — **DONE**: All `ft_scene_obj` implementations
   call `rt_db_get_internal(…, NULL, …)` and let the renderer (or caller) apply
   `s->s_mat`.  The old `rt_db_get_internal(…, s->s_mat, …)` call path is gone.

### 3.3 Migration path (completed)

All six steps below have been executed:

1. ~~Add `s_res` to `bsg_shape`; set it from `d->res` in the setup phase.~~ ✅
2. ~~Implement `rt_comb_scene_obj`; test with mode-3 draw.~~ ✅
3. ~~Implement `rt_bot_scene_obj`; test LoD pipeline.~~ ✅
4. ~~Implement `rt_brep_scene_obj`; test shaded BREP.~~ ✅
5. ~~Simplify `draw_scene` to remove now-redundant dispatch blocks.~~ ✅
6. ~~Audit all remaining `d->dbis` accesses in `draw_scene`; the setup phase~~
   ~~should be the only remaining dbis access.~~ ✅

---

## 4. OpenInventor Scene Graph Opportunities

The OI model (as already partially implemented in `bsg/defines.h`) offers:

- **`BSG_NODE_LOD_GROUP`** — replace the per-view LoD sub-object mechanism with
  a LOD group node whose children are detail levels.  `bsg_view_traverse()`
  picks the appropriate child automatically.  The deprecated
  `bsg_shape_for_view` / `bsg_shape_get_view_obj` mechanism is replaced.

- **Instancing** — the same primitive node pointer appears as a child of
  multiple `BSG_NODE_SEPARATOR` nodes with different `s_mat` values.  This
  eliminates duplicated vlist data for instanced geometry.

- **`BSG_NODE_CAMERA`** — already wired in.  Camera is a scene-graph node;
  `bsg_traversal_state::active_camera` is updated during traversal.

- **Sensors** — `bsg_shape_add_sensor()` replaces polling of `s_dlist_stale`.
  Drawing frameworks register a sensor on geometry nodes; sensor fires when the
  node is marked stale by the pipeline, triggering selective redraw.

The per-view sub-object mechanism (`bsg_shape_for_view`, `bsg_shape_get_view_obj`)
is preserved for now but deprecated.  New code should use `BSG_NODE_LOD_GROUP`.

---

## 5. draw_m3 (Mode 3) Structural Issues

`wireframe_eval.c` is **1875 lines** of raytrace-based CSG Boolean evaluation.
It is the most complex drawing mode and has several structural problems:

1. **Re-evaluates from scratch every draw call** — `rt_gettrees` re-parses
   the subtree on every invocation.  Results should be cached as a
   `BSG_NODE_SEPARATOR` subtree attached to the comb's scene node.

2. **Synchronous, potentially slow** — the raytrace can take seconds for
   complex Boolean trees.  It should be pushed to the `DrawPipeline` as a
   5th stage (after LoD), with the same lazy-placeholder pattern used for
   AABB/OBB.

3. **Invoked at the comb level but draws leaf geometry** — conceptually this
   is a "evaluated" version of the comb tree, meaning results belong to the
   comb node in the scene graph, not to leaf primitives.

4. **Can move to `rt_comb_scene_obj`** — the signature already has everything
   needed (`s->s_path`, `dbip`, `tol`, `ttol`, `s->vlfree`).  The async
   caching issue can be addressed by treating the evaluated result as a
   pipeline stage: when the evaluated vlist is available, populate it; until
   then, return without drawing (the standard `!have_bbox` no-op pattern).

---

## 6. Summary of Changes Made (2026-03-09)

### Files changed

| File | What changed |
|---|---|
| `include/bsg/defines.h` | Added `mesh_c`, `s_obb_pts[24]`, `s_have_obb` to `bsg_shape` (session 7); added `s_res` (session 8) |
| `src/libged/draw.cpp` | Consolidated all dbis access into a single setup phase; simplified lazy guard; `bot_adaptive_plot` uses `s->mesh_c` and `s->s_obb_pts`; setup phase now also writes `d->res → s->s_res` |
| `src/librt/primitives/generic.c` | `rt_generic_scene_obj` now handles `!have_bbox` with OBB placeholder; uses `s->s_res` for `rt_db_get_internal` (fallback to `&rt_uniresource`); calls `rt_db_get_internal(…, NULL, …)` for object-space vlists |

### `s_res` rationale in detail

The addition of `struct resource *s_res` to `bsg_shape` closes the last gap
identified in §2.2 above.  Before this change, any `ft_scene_obj` callback
that needed to call `rt_db_get_internal` had to either:

- Use `&rt_uniresource` (global, serialised, not thread-safe for concurrent draws), or
- Cast `s->s_i_data` back to `draw_update_data_t` (creates a libged dependency in librt).

With `s_res` forwarded from `d->res` in the setup phase, all `ft_scene_obj`
callbacks in librt can call `rt_db_get_internal(dp, dbip, NULL, s->s_res)` with
a thread-appropriate resource block.

### `ft_mat` / `rt_##name##_mat` usage with `s_res`

The combination of `s_res` and `rt_db_get_internal(…, NULL, …)` is also the
key to using `ft_mat` correctly for object-space vlist caching:

```
/* Object-space approach (new): */
rt_db_get_internal(&intern, dp, dbip, NULL, s->s_res);   /* object-space */
/* store intern or its vlists in cache keyed on dp->d_namep */
/* at render time, renderer applies s->s_mat to the cached vlist */

/* Alternative with ft_mat (if baking is needed): */
rt_db_get_internal(&intern, dp, dbip, NULL, s->s_res);   /* object-space */
OBJ[ip->idb_type].ft_mat(&intern, s->s_mat, NULL);       /* apply transform in-place */
/* generate world-space vlists from transformed intern */
```

The `rt_db_get_internal(…, s_mat, …)` path (current `draw_scene`) bakes the
matrix during cracking via export+import round-trip, which is equivalent to
using `ft_mat` but forces the round-trip even when not needed.  Using `NULL`
and then `ft_mat` when required is cheaper because it avoids the export step
when the vlist is already cached.

### Invariants maintained

- `d->dbis` is accessed **only** in `draw_scene`'s setup phase.
- `bot_adaptive_plot`, `brep_adaptive_plot`, and `rt_generic_scene_obj` access
  **no** libged-private state — only `bsg_shape` fields and `ft_scene_obj` parameters.
- Build is clean (GCC 13, `-Werror`).
