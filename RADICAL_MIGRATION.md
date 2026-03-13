# BRL-CAD → Obol Scene Graph: Radical Migration Plan

## Overview

This document is the master plan for replacing BRL-CAD's traditional flat-list,
immediate-mode drawing architecture with the **Obol** scene-graph library.  The
goal is a well-architected, idiomatic use of the Obol API throughout BRL-CAD's
3D visualization stack while preserving all user-facing features.

Obol is our fork of Coin3D/Open Inventor — a C++17, toolkit-agnostic,
dependency-minimal scene-graph library.  All of its scene-graph, action,
dragger, manipulator, and selection infrastructure is already complete and
tested; what remains is integrating it as BRL-CAD's authoritative renderer.

Intermediate non-working states are acceptable and preferable to maintaining
fragile hybrid states.

---

## Current State (pre-migration)

### Libraries involved

| Library   | Role                              | Migration target              |
|-----------|-----------------------------------|-------------------------------|
| `libdm`   | Immediate-mode display plugin     | **Remove** (replace with Obol)|
| `libfb`   | Framebuffer raster output         | Keep for rtwizard/offline rt  |
| `libbsg`  | Scene-graph shim (bv_ aliases)    | **Remove** (replace with Obol)|
| `librt`   | Geometry + raytracing             | Keep; `ft_scene_obj` → Obol nodes|
| `libged`  | Command layer + draw pipeline     | Keep drawing pipeline; remove vlist drawing|

### Drawing pipeline (current)

```
libged draw command
  └─ draw_gather_paths()       walk comb tree, build bsg_shape list
       └─ draw_scene()         dispatch per-shape drawing
            ├─ bot_adaptive_plot()     → LoD vlist
            ├─ brep_adaptive_plot()    → NURBS vlist
            ├─ draw_m3()              → CSG raytrace eplot vlist
            └─ ft_scene_obj()         → generic vlist via ft_plot/ft_adaptive_plot
                                         or ft_tessellate
```

Vlists are then consumed by `libdm` (dm_draw_vlist) which converts them to
immediate-mode OpenGL calls inside the dm-gl or dm-swrast plugin.

### qged rendering (current)

```
QgEdApp ─── QgEdMainWindow ─── QgQuadView
                                    └─ QgView (QOpenGLWidget)
                                         └─ libdm qtgl / swrast plugin
                                              └─ dm_draw_vlist()
```

### Key data structures

- `bsg_shape` — per-object container: transform (`s_mat`), vlist (`s_vlist`),
  LoD context (`mesh_c`), OBB data (`s_obb_pts`), async state (`have_bbox`)
- `bsg_view` — camera + view state (bview alias)
- `bsg_scene` / `bview_set` — set of views sharing a geometry set
- `draw_update_data_t` — per-draw context (dbip, path, tolerances, DbiState ptr)

---

## Target State (post-migration)

### Libraries in target

| Library   | Role after migration                          |
|-----------|-----------------------------------------------|
| `libdm`   | **Removed** (or reduced to rtwizard/fb raster)|
| `libbsg`  | **Removed** (Obol replaces all functions)     |
| `librt`   | Keep; `ft_scene_obj` → SoNode* on bsg_shape   |
| `libged`  | Keep command layer; draw pipeline feeds Obol  |
| `libObol` | Authoritative scene graph + renderer          |

### Drawing pipeline (target)

```
libged draw command
  └─ draw_gather_paths()       walk comb tree, build bsg_shape list
       └─ draw_scene()         dispatch per-shape; ft_scene_obj → SoNode*
            └─ ft_scene_obj()  populate s->s_obol_node (SoSeparator subtree)
                                  ├─ rt_bot_scene_obj   → SoIndexedFaceSet (LoD)
                                  ├─ rt_brep_scene_obj  → SoIndexedFaceSet
                                  ├─ rt_comb_scene_obj  → SoSeparator group
                                  └─ rt_generic_scene_obj → SoIndexedLineSet
       └─ obol_scene_assemble()
            walks bsg_shape tree → builds root SoSeparator hierarchy
```

### qged rendering (target)

```
QgEdApp ─── QgEdMainWindow ─── QgObolView (QOpenGLWidget)
                                    └─ SoViewport + SoRenderManager
                                         └─ SoGLRenderAction (GL traversal)
```

---

## Stage 0: Foundation — Obol in the Build System

**Status:** Complete

**Goal:** Get Obol linked into BRL-CAD and establish the bridging points.

### Tasks

1. **Add Obol to CMake:**
   - `find_package(Obol)` using the pre-built `bext_output/install/` tree.
   - Export `Obol::Obol` imported target; expose `Obol_INCLUDE_DIR`.
   - Gate on `BRLCAD_ENABLE_OBOL` option (initially defaults to ON when Obol found).

2. **Add `s_obol_node` to `bsg_shape`:**
   - `void *s_obol_node` — opaque pointer to a `SoNode *` held by the shape.
   - C++ code casts to `SoNode *` and calls `ref()`/`unref()`.
   - Free callback (`s_free_callback`) decrements reference on cleanup.
   - `s_obol_node` is initially NULL; `ft_scene_obj` populates it.

3. **Create `brlcad/include/bsg/obol_node.h`:**
   - C++-safe helper for attaching/detaching Obol nodes to/from `bsg_shape`.
   - `bsg_shape_set_obol_node(bsg_shape *s, void *node)` — replaces existing,
     calls `unref()` on old, `ref()` on new.
   - `bsg_shape_get_obol_node(const bsg_shape *s)` — accessor.

4. **Create `QgObolView.h` for qged:**
   - Self-contained Qt widget based on `qt_obol_widget.h` from the Obol examples.
   - Adapts `SoDB::ContextManager` for Qt (`QtObolContextManager`).
   - Exposes `setSceneGraph()`, `redraw()`, `viewAll()`.
   - Replaces `QgView` (libdm qtgl/swrast) as qged's primary 3D display widget.

5. **Initialize Obol in `QgEdApp`:**
   - Call `SoDB::init(&ctxMgr)`, `SoNodeKit::init()`, `SoInteraction::init()`.
   - Create per-view Obol scene roots (one `SoSeparator *` per `bsg_view`).
   - Call `SoDB::finish()` in destructor.

6. **Create `obol_scene.h` / `obol_scene.cpp` (in `libged`):**
   - `obol_scene_init()` — create root `SoSeparator`, directional light, camera.
   - `obol_scene_assemble()` — walk `bsg_shape` tree; for each shape with a
     populated `s_obol_node`, add its subtree under the appropriate
     `SoSeparator` with the correct `SoTransform` from `s->s_mat`.
   - `obol_scene_clear()` — remove all geometry children, keep camera/light.
   - Called from `do_view_changed()` in `QgEdApp` whenever the view is dirty.

---

## Stage 1: Primitive Nodes — `ft_scene_obj` → SoNode

**Status:** Complete

**Goal:** Migrate each primitive's `ft_scene_obj` to build and cache an Obol
`SoNode` subtree instead of (or alongside) a vlist.

### Generic shape (all primitives)

Modify `rt_generic_scene_obj` in `brlcad/src/librt/primitives/generic.c`:

```cpp
// Instead of: generating vlists via ft_plot/ft_adaptive_plot/ft_tessellate
// Do:
SoSeparator *root = new SoSeparator;

// 1. Material from s->s_os->s_color
SoBaseColor *col = new SoBaseColor;
col->rgb.setValue(s->s_os->s_color[0]/255.0f,
                  s->s_os->s_color[1]/255.0f,
                  s->s_os->s_color[2]/255.0f);
root->addChild(col);

// 2. Geometry — tessellate or plot via librt, convert to SoIndexedFaceSet
// or SoIndexedLineSet
...

bsg_shape_set_obol_node(s, root);
```

The transform is NOT embedded in the node — `s->s_mat` is applied at assembly
time in `obol_scene_assemble()` via `SoTransform`.  This allows instancing:
multiple `bsg_shape` objects referencing the same geometry at different
transforms share the same Obol node.

### BoT (Bag of Triangles)

`rt_bot_scene_obj` in `brlcad/src/librt/primitives/bot/bot.cpp`:

- **Wireframe:** `SoIndexedLineSet` from edge list.
- **Shaded (LoD):** `SoIndexedFaceSet` with normals; use `SoLevelOfDetail` to
  switch between detail levels as the mesh cache delivers them.
- **Progressive refinement:** AABB placeholder → OBB placeholder → full mesh
  maps to: initial `SoLineSet(obb_pts)` → finer `SoIndexedFaceSet`.
- Store LOD node in `s_obol_node`; update children in-place as LoD arrives.

### BREP (boundary representation)

`rt_brep_scene_obj` in `brlcad/src/librt/primitives/brep/brep.cpp`:

- Tessellate via `rt_brep_adaptive_plot` or `ON_Mesh` → `SoIndexedFaceSet`.
- Wireframe: `SoIndexedLineSet` from control polygon.

### CSG wireframe (mode 3 — evaluated wireframe)

`rt_comb_scene_obj`:

- Re-use the existing `draw_m3` eplot logic but store the result in
  `SoIndexedLineSet` instead of a vlist.
- If eplot is expensive, use a `SoProceduralShape` callback that generates
  the vlist lazily on first GL traversal.

### Default / unimplemented primitives

`rt_generic_scene_obj` handles everything via `ft_plot` or `ft_tessellate`,
falling back to an AABB wireframe if neither is available.

---

## Stage 2: Scene Hierarchy

**Status:** Complete

**Goal:** Express BRL-CAD's comb-tree hierarchy directly as an Obol scene.

### Mapping

| BRL-CAD concept    | Obol equivalent                        |
|--------------------|----------------------------------------|
| comb (with children) | `SoSeparator` (state boundary)      |
| leaf solid         | `SoSeparator` + geometry node          |
| instance transform | `SoTransform` child of leaf separator  |
| material override  | `SoMaterial` / `SoBaseColor` as child  |
| color inheritance  | Open Inventor state propagation        |
| visibility flag    | `SoSwitch` (whichChild 0 or -1)        |

### Implementation

Modify `draw_gather_paths()` and the scene assembler to emit a proper
hierarchical scene rather than a flat list:

```
root SoSeparator
  SoDirectionalLight
  SoPerspectiveCamera (or SoOrthographicCamera)
  SoSeparator  ← per top-level comb (e.g. all.g)
    SoSeparator  ← per region (e.g. region_1.r)
      SoTransform  ← cumulative s_mat
      SoMaterial   ← s_os->s_color
      SoIndexedFaceSet  ← leaf geometry (from ft_scene_obj)
    SoSeparator  ← region_2.r
      ...
```

Instancing — multiple paths referencing the same primitive:
- Use `SoSeparator *geomNode = ...` shared across multiple `SoSeparator` parents.
- `SoNode::ref()` counts prevent premature deallocation.

---

## Stage 3: Drawing Modes

**Goal:** Map BRL-CAD's drawing modes to `SoRenderManager` modes.

| BRL-CAD s_dmode | Description           | Obol mapping                              |
|-----------------|-----------------------|-------------------------------------------|
| 0               | Wireframe             | `SoRenderManager::WIREFRAME`              |
| 1               | Hidden-line           | `SoRenderManager::HIDDEN_LINE`            |
| 2               | Shaded (Phong)        | `SoRenderManager::AS_IS` + normals        |
| 3               | Evaluated wireframe   | `SoProceduralShape` (CSG eplot callback)  |
| 4               | Shaded + hidden-line  | `SoRenderManager::SHADED_HIDDEN_LINES`    |
| 5               | Point cloud           | `SoRenderManager::POINTS`                 |

### Per-object draw mode

Objects can have different draw modes.  Use per-subtree `SoDrawStyle` nodes:

```
SoSeparator (object root)
  SoDrawStyle  ← lineWidth, pointSize, style (FILLED/LINES/POINTS)
  SoMaterial
  SoIndexedFaceSet or SoIndexedLineSet
```

For wireframe-only objects inside a shaded scene: wrap the subtree in an
`SoGroup` with `SoDrawStyle::style = LINES` so the render manager's global
render mode doesn't override it.

---

## Stage 4: Camera and View

**Goal:** Replace `bsg_view`'s manual matrix math with Obol's camera system.

### Current camera state (in bsg_view / bview)

```c
fastf_t gv_size;         // view volume half-size
fastf_t gv_scale;        // pixels-per-unit
point_t gv_center;       // look-at point
mat_t   gv_rotation;     // eye rotation matrix
mat_t   gv_model2view;
mat_t   gv_view2model;
```

### Target: SoPerspectiveCamera / SoOrthographicCamera

```cpp
SoPerspectiveCamera *cam = new SoPerspectiveCamera;
cam->position.setValue(eye[0], eye[1], eye[2]);
cam->orientation.setValue(SbRotation(rot4x4));
cam->focalDistance = gv_size;
cam->heightAngle = fov_radians;
```

Camera navigation commands (`ae`, `center`, `zoom`, `rot`) update the Obol
camera directly via `SbRotation`, `SbVec3f`.  The view matrix is then derived
by `SoGLRenderAction` automatically during the render traversal.

`bsg_view` will keep its numeric fields for backward-compatible command-line
tools; `QgObolView` syncs from `bsg_view` → Obol camera on each redraw.

### viewAll

```cpp
void QgObolView::viewAll() {
    viewport_.viewAll();
    SbBox3f bbox;
    SoGetBoundingBoxAction ba(SbViewportRegion(width(), height()));
    ba.apply(viewport_.getSceneGraph());
    bbox = ba.getBoundingBox();
    viewport_.getCamera()->viewAll(bbox, SbViewportRegion(width(), height()));
}
```

---

## Stage 5: Selection and Picking

**Goal:** Replace librt-based screen-ray picking with `SoRayPickAction`.

### Current picking

```c
// In libged pick:
rt_shootray(...);
// returns hit list from librt
```

### Target: SoRayPickAction

```cpp
SoRayPickAction rpa(SbViewportRegion(w, h));
rpa.setPoint(SbVec2s(x, y));
rpa.apply(viewport_.getSceneGraph());
const SoPickedPointList &picks = rpa.getPickedPointList();
// For each pick, SoPath leads back to the SoSeparator for the BRL-CAD object
```

### Highlight on hover

```cpp
// Use SoSelection node in the scene
SoSelection *sel = new SoSelection;
sel->policy = SoSelection::SINGLE;
sel->addSelectionCallback(onSelectionChanged, this);
root->addChild(sel);
// SoLocateHighlight or SoBoxHighlightRenderAction for visual feedback
```

---

## Stage 6: mged, archer, rtwizard

**Goal:** Update legacy frontends to use Obol.

### mged (Tcl/C-based)

- mged uses a custom Tk OpenGL widget (`dm-tk`).
- Replace `dm-tk` with an Obol-based renderer using `SoDB::ContextManager`
  that wraps the raw GLX context from Tk's `togl` widget.
- Alternatively, expose a minimal C API from `libObol` for Tcl embedding.

### archer

- archer is also Qt-based.  Apply the same `QgObolView` approach as qged.

### rtwizard

- rtwizard uses offscreen rendering (`rt`, `rtwizard`) for high-quality images.
- Use `SoOffscreenRenderer` with an `OSMesaContextManager` for headless rendering.
- rtwizard geometry pipeline: `rt` → pixel buffer → composited with Obol overlays.

---

## Stage 7: Remove libdm / libbsg

**Goal:** Once Obol rendering is the sole path for qged (and optionally mged),
remove the old infrastructure.

### libdm removal

- Delete: `dm-gl.c`, `dm-gl_lod.cpp`, `dm-generic.c`, `dm_plugins.cpp`
- Delete: all dm plugin directories (`qtgl/`, `swrast/`, `glx/`, `null/`,
  `plot/`, `postscript/`)
- Keep `libfb` for rtwizard pixel buffers (not scene-graph based)
- Delete: `include/dm.h`, `include/dm/` tree
- Update all callers (mged, archer, libged/view.cpp) to use Obol equivalents

### libbsg removal

- Delete: `src/libbsg/` entirely
- Delete: `include/bsg/` subtree (except migration aliases if needed)
- `bsg_shape` either becomes a lightweight struct (just holding `SoNode *` and
  path info) or is eliminated in favor of storing paths in the Obol scene
  directly (using SoPath for selection, SoSearchAction for lookup)
- `bsg_view` either becomes a thin wrapper around `SoViewport` or is removed
  entirely; `QgObolView` owns the `SoViewport`
- Update all callers: libged draw/redraw, ged/view, qged

---

## Dependency Graph

```
Stage 0: Foundation (current)
    ↓
Stage 1: ft_scene_obj → SoNode
    ↓
Stage 2: Scene Hierarchy (comb tree → Obol tree)
    ↓
Stage 3: Drawing Modes (SoRenderManager modes)
    ↓
Stage 4: Camera / View (bsg_view → SoCamera)
    ↓
Stage 5: Selection (SoRayPickAction, SoSelection)
    ↓
Stage 6: mged / archer / rtwizard
    ↓
Stage 7: Remove libdm / libbsg
```

Stages 1-3 can partially overlap.  Stage 7 is safe to start whenever a
stage's work is complete for any given frontend (e.g. remove qtgl plugin as
soon as `QgObolView` is rendering qged correctly, before mged is done).

---

## Key Design Principles

1. **Obol owns the rendered scene.** BRL-CAD's drawing code is responsible for
   building and updating Obol nodes; Obol is responsible for all GL rendering.

2. **Progressive refinement via Obol nodes.** AABB/OBB/LoD upgrades replace
   the existing node children in-place (the `SoSeparator` for that primitive
   gets its geometry child swapped out), triggering an automatic re-render via
   the field-change notification system.

3. **No vlist in the hot path.** Vlists are an interim representation.  The
   target is `ft_scene_obj` callbacks that produce Obol nodes directly.  A
   compatibility shim can convert vlists to `SoIndexedLineSet` / `SoFaceSet`
   during the transition but should be removed once all primitives have native
   Obol nodes.

4. **Thread safety.** `DbiState` AABB/OBB/LoD results are delivered on worker
   threads.  Obol node creation must happen on the GL thread (or use
   `SoDB::writelock()` / `SoDB::readlock()` for thread-safe modifications).
   The existing `drain_background_geom()` → `do_view_changed()` mechanism is
   the correct integration point: drain delivers pipeline results to the main
   thread which then updates Obol nodes and schedules a repaint.

5. **No libdm in scene rendering.** The rendering loop is:
   `SoGLRenderAction.apply(root)` — Obol traverses and renders the entire scene.
   No manual dm_draw_vlist() loops remain.

6. **Camera is an Obol camera.** All view state (azimuth, elevation, zoom,
   center) is stored in and manipulated via the `SoCamera` node.  The `bsg_view`
   numeric fields are updated after each Obol camera change for backward
   compatibility with command-line tools.

7. **Preserve `ft_scene_obj` signature.** The librt function table stays as-is.
   The only change is what the callbacks put into `bsg_shape`: an Obol `SoNode *`
   in `s_obol_node` instead of (or in addition to) vlists in `s_vlist`.

---

## Current File Inventory

### Files to significantly modify

| File | Change |
|------|--------|
| `include/bsg/defines.h` | Add `s_obol_node` to `bsg_shape` |
| `src/libged/draw.cpp` | Call `obol_scene_assemble()` after draw_scene; remove dm_draw_vlist |
| `src/librt/primitives/generic.c` | `rt_generic_scene_obj` → build SoNode instead of vlist |
| `src/qged/QgEdApp.cpp` | Initialize Obol; use QgObolView for primary view |
| `src/qged/CMakeLists.txt` | Link against Obol::Obol |
| `CMakeLists.txt` | `find_package(Obol)` / `BRLCAD_FIND_PACKAGE(Obol)` |

### Files to create

| File | Purpose |
|------|---------|
| `include/bsg/obol_node.h` | C++ helpers: set/get SoNode on bsg_shape |
| `src/libged/obol_scene.h` | Scene assembler interface |
| `src/libged/obol_scene.cpp` | Scene assembler: bsg_shape tree → Obol SoSeparator |
| `src/qged/QgObolView.h` | Qt/Obol widget (replaces QgView) |

### Files to eventually delete (Stage 7)

- `src/libdm/` (entire tree, except fb_* for rtwizard)
- `src/libbsg/` (entire tree)
- `include/bsg/` (subtree, except thin migration aliases)
- `include/dm.h` and `include/dm/` subtree

---

## Acceptance Criteria

The migration is complete when:

1. `qged` renders BRL-CAD geometry using Obol (SoGLRenderAction) with no
   libdm plugin involved.
2. All drawing modes (0–5) work via SoRenderManager's built-in modes plus
   custom SoProceduralShape for mode 3.
3. `mged` renders via an Obol context-manager wrapping the Tk GL context.
4. `rtwizard` uses SoOffscreenRenderer + OSMesa for headless rendering.
5. Camera navigation (ae, center, zoom, rot) and interactive mouse navigation
   are implemented via Obol camera manipulation.
6. Object selection and highlighting work via SoRayPickAction + SoSelection.
7. All libged draw commands (`draw`, `erase`, `who`, `e`, `B`) work correctly.
8. All existing regression tests pass.
9. `libdm` (excluding fb_* raster helpers) and `libbsg` are deleted.
