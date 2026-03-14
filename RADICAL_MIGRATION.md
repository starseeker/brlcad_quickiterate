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

**Status:** Complete

**Goal:** Map BRL-CAD's drawing modes to `SoRenderManager` modes.

The approach uses a two-level system:
1. **Per-object `SoDrawStyle`** (in `obol_scene.cpp`): each leaf shape separator
   carries a `SoDrawStyle` node whose `style` field is set from `s->s_os->s_dmode`.
   This ensures correct rendering when the global `SoRenderManager` mode is `AS_IS`.
2. **Global render mode** (`QgObolView::setRenderMode` / `syncRenderModeFromDmode`):
   a global override that forces all objects to render the same way (wireframe, hidden
   line, points, etc.).  The context menu in `QgObolView` exposes all available modes.

| BRL-CAD s_dmode | Description           | SoDrawStyle::style | SoRenderManager override    |
|-----------------|-----------------------|--------------------|-----------------------------|
| 0               | Wireframe             | `LINES`            | `AS_IS`                     |
| 1               | Hidden-line           | `LINES`            | `HIDDEN_LINE`               |
| 2               | Shaded (Phong)        | `FILLED`           | `AS_IS`                     |
| 3               | Evaluated wireframe   | `LINES`            | `AS_IS` (vlist path)        |
| 4               | Shaded + hidden-line  | `FILLED`           | `SHADED_HIDDEN_LINES`       |
| 5               | Point cloud           | `POINTS`           | `POINTS`                    |

### Per-object draw mode

Objects can have different draw modes.  Each leaf shape separator produced by
`obol_scene_assemble()` carries a `SoDrawStyle` node as its first child:

```
SoSeparator (object root)
  SoDrawStyle  ← lineWidth/pointSize/style (FILLED/LINES/POINTS) from s_dmode
  SoTransform  ← from s->s_mat
  SoMaterial   ← Phong diffuse from s->s_os->s_color
  SoIndexedFaceSet or SoIndexedLineSet  ← from ft_scene_obj
```

The `SoSeparator` acts as a state barrier so per-object draw styles do not
bleed into sibling shapes.  When the global `SoRenderManager` mode is `AS_IS`
(the default), each object renders according to its own `SoDrawStyle`.

### Global render mode (`QgObolView`)

`QgObolView` provides:
- `setRenderMode(SoRenderManager::RenderMode)` — directly set the global mode.
- `syncRenderModeFromDmode(int dmode)` — maps a BRL-CAD draw mode integer to
  the appropriate `SoRenderManager` mode and applies it.  Useful when a
  command changes the global view draw mode.
- Right-click context menu listing all seven `SoRenderManager` modes.

---

## Stage 4: Camera and View

**Status:** Complete

**Goal:** Replace `bsg_view`'s manual matrix math with Obol's camera system.

### Implementation

`QgObolView` provides a bidirectional camera synchronisation bridge:

**`syncCameraFromBsgView()`** (already in Stage 0) — reads `bsg_view` fields and
writes them to the Obol `SoPerspectiveCamera`:
- Eye position: `gv_view2model * {0,0,0}` → `cam->position`
- Orientation: rows of `gv_rotation` → `cam->orientation` (via `SbRotation`)
- Focal distance: `gv_size` → `cam->focalDistance`
- Perspective: `gv_perspective` → `cam->heightAngle`

**`syncBsgViewFromCamera()`** (Stage 4) — reads the Obol camera and writes back
to `bsg_view` so all command-line tools stay consistent after interactive navigation:
- Extracts right/up/look world vectors from `cam->orientation`
- Rebuilds `gv_rotation` (4×4 with rows = right, up, −look)
- Computes scene center = `cam->position + look * cam->focalDistance`
- Rebuilds `gv_center` as `translate(−scene_center)`
- Sets `gv_size = focalDistance`, `gv_scale = focalDistance / 2`
- Calls `bsg_view_update()` to recompute derived matrices (model2view, view2model, aet)
- Clears `gv_progressive_autoview` so drain_background_geom doesn't override the user's navigation

**Mouse navigation** calls `syncBsgViewFromCamera()` after:
- `mouseMoveEvent` (orbit / pan) — when any camera-changing button is held
- `wheelEvent` (zoom) — always

**`viewAll()`** (Stage 4 upgrade):
- Uses `SoGetBoundingBoxAction` to compute the actual scene bbox
- Calls `cam->viewAll(scene, vpregion)` for tight fitting
- Then calls `syncBsgViewFromCamera()` to propagate the fitted view back to `bsg_view`

`bsg_view` retains all its legacy fields for backward-compatible command-line
tools (ae, center, zoom, rot commands continue to work via `syncCameraFromBsgView()`).

---

## Stage 5: Selection and Picking

**Status:** Complete

**Goal:** Replace librt-based screen-ray picking with `SoRayPickAction`.

### Implementation

**`obol_scene.cpp`** — Selection API

- **Reverse map** (`sep_shape_map`): `SoSeparator* → bsg_shape*`, maintained
  alongside the forward `shape_sep_map`.  Entries are added in
  `obol_scene_update_shape()` and removed on rebuild or `obol_scene_clear()`.

- **`obol_find_shape_for_path(const SoPath*)`** — walks the pick path from the
  deepest node toward the root, returning the first `bsg_shape` whose
  `SoSeparator` appears in the reverse map.

- **`obol_shape_set_selected(bsg_shape*, bool)`** — marks a shape as
  selected/deselected, sets `s_changed = 1` to force a material rebuild.

- **`obol_shape_is_selected(bsg_shape*)`** — returns the current selection state.

- **Selection highlight** — in `obol_scene_update_shape()`, if a shape is
  selected, its `SoMaterial` gets an emissive orange glow
  (`emissiveColor = {0.8, 0.4, 0.0}`).

**`QgObolView.h`** — Picking

- **Ctrl+left-click** triggers `pickAt(x, y)` instead of orbit.

- **`pickAt(int x, int y)`** — fires `SoRayPickAction` against the scene,
  resolves the closest hit to a `bsg_shape`, handles toggle (re-click
  deselects, new click replaces selection), calls `obol_scene_assemble()` to
  apply highlight, emits `picked(bsg_shape*)`.

- **`picked(bsg_shape*)` signal** — consumers connect to this to act on
  selection changes (e.g. update the tree view, show properties panel).

- **`selectedShape_` member** — tracks the currently selected shape so that
  `pickAt()` can clear the previous selection without iterating the entire
  scene.

---

## Stage 6: qged, mged, archer, rtwizard

**Status (qged):** Complete
**Status (mged):** Complete — per-pane independent cameras now supported via `new_obol_view_ptr`.
**Status (archer):** Complete — `cadwidgets::Ged` uses `obol_view` widgets with independent per-pane cameras.

**Goal:** Update frontends to use Obol rendering.

### qged (Qt-based) — **Done**

`QgObolView` is now the primary 3D rendering widget in qged when Obol is
available (detected via `BRLCAD_ENABLE_OBOL`):

- `QgEdApp` calls `SoDB::init()`, `SoNodeKit::init()`, `SoInteraction::init()`
  using `QgObolContextManager` before any GL widget is created.
- `QgEdMainWindow::CreateWidgets()` instantiates `QgObolView` (instead of
  `QgQuadView`) when `BRLCAD_ENABLE_OBOL` is defined.
- `view_update` signal is connected to `QgObolView::need_update()`, which calls
  `obol_scene_assemble()` (when `QG_VIEW_DRAWN` is set) then repaints.
- `QgObolView::init_done()` signal is connected to `do_obol_init()` for
  post-GL-init setup.
- All `c4` (QgQuadView) methods are guarded — in the Obol path they delegate
  to `obol_view_` or return no-ops.
- `qged_test` and `qged_pipeline_test` are also linked with Obol and
  `Qt6::OpenGLWidgets` so they compile when Obol is enabled.

### mged (Tcl/C-based) — **Done**

mged uses a platform-neutral Tk Obol widget (`obol_view`, implemented in
`libtclcad/obol_view.cpp`) to render geometry via Obol when
`BRLCAD_ENABLE_OBOL` is defined:

- `gui_setup()` in `attach.c` calls `obol_init` (via Tcl) to initialize SoDB
  before any `obol_view` widget is created.
- `mview.tcl`'s `openmv` proc detects `obol_view` and `gvp_ptr` commands and
  creates `obol_view` widgets (instead of calling `attach` for dm plugins).
  Each widget auto-detects HW GL (GLX on X11, WGL on Windows, NSOpenGL on macOS)
  and falls back to SW (SoOffscreenRenderer + OSMesa) automatically.
- After `obol_init`, the `obol_view` widget path supports:
  - HW GL via `OBOL_HW_GLX` (X11/GLX), `OBOL_HW_WGL` (Windows), `OBOL_HW_NSGL` (macOS)
  - SW via `SoOffscreenRenderer` + `CoinOSMesaContextManager` (headless/portable)
- `mged.c`'s `refresh()` now calls `obol_notify_views` (a new Tcl command) at
  the end of every refresh cycle to wake up all live `obol_view` widgets so
  that geometry changes (draw, erase, view commands) are immediately visible.
- `obol_notify_views` calls `obol_scene_assemble()` + `SoGLRenderAction` on
  every registered `obol_view` instance.
- `f_gvp_ptr` command exposes the current `bsg_view` pointer to Tcl so that
  `obol_view` widgets can be attached to it.
- **`f_new_obol_view_ptr` command** (in `mged/attach.c`) creates a new
  independent `bsg_view` (null dmp, registered in `ged_views`) and returns its
  pointer as a hex string.  `mview.tcl` calls this for each of the four panes.
- **Per-pane independent cameras**: `mview.tcl`'s Obol path now calls
  `new_obol_view_ptr $w.$pane` for each pane and stores the mapping in the
  `::obol_pane_gvp` Tcl array.  `f_winset` in `cmd.c` was updated to check
  this array when the legacy `active_dm_set` lookup fails, so view commands
  (`ae`, `press`, `zoom`, …) operate on the focused pane's camera.

The 4-pane layout (ul, ur, ll, lr) now creates independent `bsg_view` objects
(one per pane) with separate `obol_view` render widgets — multi-pane independent
cameras are therefore supported in mged.

### archer — **Done**

archer is a Tcl/Tk application using libtclcad.  Obol integration is done via
the `cadwidgets::Ged` widget, which uses `to_new_view` in libtclcad:

- `archer.c` already calls `obol_init` at startup (before any Tk widgets are
  created), so SoDB is initialized by the time `cadwidgets::Ged` is constructed.
- **`to_new_view` in `commands.c`** now recognizes `"obol"` as a virtual display
  type:
  - Skips `dm_open` entirely (leaves `new_gdvp->dmp = NULL`).
  - Registers the `bsg_view` in `ged_views` as usual.
  - Creates an `obol_view` Tk widget at the view name path via Tcl.
  - Calls `$path attach $gvp_ptr` to bind the `bsg_view` to the widget.
- **`to_open_fbs` in `fb.c`** now has a null-DMP guard so it returns `TCL_OK`
  silently for Obol views (no framebuffer yet — planned as follow-up).
- **`Ged.tcl` constructor** detects `obol_view` availability and sets
  `dmType obol` before the four `new_view` calls.  When Obol is not available,
  the original `dm_list`-based selection is used unchanged.
- **`ArcherCore.tcl`** transparency-menu guard updated: `mDisplayType == "obol"`
  is treated equivalently to `"ogl"`/`"wgl"` (allows transparency menu).

The 4-pane layout (ul, ur, ll, lr) now creates independent `bsg_view` objects
(one per pane) with separate `obol_view` render widgets — multi-pane independent
cameras are therefore supported in archer.

### rtwizard

- `rtwizard` calls `obol_init` when running in GUI mode (done).
- rtwizard uses offscreen rendering (`rt`, `rtwizard`) for high-quality images.
- Use `SoOffscreenRenderer` with an `OSMesaContextManager` for headless rendering.
- rtwizard geometry pipeline: `rt` → pixel buffer → composited with Obol overlays.

---

## Stage 7: Remove libdm / libbsg

**Status:** In Progress — null DMP guards added; `ert` command now works in Obol
path via memory fb + overlay rendering; more work needed to fully remove dm plugin
dependencies.

**Goal:** Once Obol rendering is the sole path for qged (and optionally mged),
remove the old infrastructure.

### Completed Stage 7 work

- **`go_refresh()` null DMP guard**: When a view's `dmp` is NULL (view owned by
  an `obol_view` widget or `QgObolView`), `go_refresh()` returns early without
  calling any `dm_draw_*` functions.  This eliminates libdm calls for Obol-rendered
  views in libtclcad-based apps (archer, etc.).
- **`go_draw_solid()` null DMP guard**: Same — skips libdm drawing when `dmp` is
  NULL.
- **`dm_draw_viewobjs` double-call bug fixed**: `go_refresh_draw()` was calling
  `dm_draw_viewobjs()` twice at the end of the non-framebuffer path; removed
  the spurious second call.
- **`obol_notify_views` global refresh command**: Registered in libtclcad's
  init.  Called by both mged's `refresh()` and libtclcad's
  `to_refresh_all_views()` so that all live `obol_view` Tk widgets are
  redrawn whenever the scene changes.
- **`ert` Obol path** (`libged/dm/ert.cpp`): When `dmp == NULL` (Obol path),
  `ert` now creates an in-memory framebuffer (`/dev/mem`) sized to the view
  dimensions instead of failing with "no current display manager set".  This
  unblocks embedded raytracing in qged's Obol mode.
- **`QgObolView` fb overlay** (`QgObolView.h`): Added `setFbServ()` and a private
  `_paintFbOverlay()` helper.  When `gv_fb_mode` is non-zero, the Obol single-view
  path reads the framebuffer pixels at the end of `paintGL()` and composites them
  on top of the 3D scene via QPainter (hardware-GL path) or inside `renderSingle()`
  (swrast path).
- **`qdm_open_obol_client_handler`** (`qged/fbserv.cpp`): New fbserv client handler
  for the Obol path.  Registered in `do_obol_init()` via `fbs_open_client_handler`;
  connects `QFBSocket::updated` → `QgObolView::need_update()` so that arriving rt
  pixels trigger a repaint.
- **`do_obol_init()` fb wiring** (`QgEdMainWindow.cpp`): After Obol GL is ready,
  sets `fbs_open_client_handler = &qdm_open_obol_client_handler`, stores the view
  widget pointer in `fbs_clientData`, and calls `setFbServ(gedp->ged_fbs)` on the
  Obol view so the overlay knows which fb to read.
- **Step 2 (`mged_pane` + `active_pane_set`)**: Added `struct mged_pane` to
  `mged_dm.h` alongside `struct mged_dm`.  Added `active_pane_set` (a `bu_ptbl`)
  in `attach.c` and `set_curr_pane()` to set `ged_gvp` for Obol panes without
  touching `mged_curr_dm`.  `f_new_obol_view_ptr` now also creates a `mged_pane`
  entry and adds it to `active_pane_set`.
- **Step 3 (`f_winset` migration)**: `f_winset` in `cmd.c` now checks
  `active_pane_set` first (Obol panes, matched by `gv_name`), then falls back to
  `active_dm_set` for legacy dm panes.  The old Tcl-variable bridge
  (`::obol_pane_gvp`) has been removed from both `f_winset` and `mview.tcl`.
- **Step 1 (DMP null guards)**: All MGED overlay drawing functions (`adcursor`,
  `draw_e_axes`, `draw_m_axes`, `draw_v_axes`, `draw_rect`, `draw_grid`,
  `dotitles`, `predictor_frame`, `dozoom`, `scroll_display`, `mmenu_display`,
  `mged_highlight_menu_item`) now return immediately when `DMP` is NULL.  All
  standalone `dm_set_dirty(DMP, 1)`, `dm_set_debug(DMP, ...)`, and
  `dm_set_perspective(DMP, ...)` calls are wrapped with `if (DMP)`.  All
  loop-based `dm_set_dirty(m_dmp->dm_dmp, 1)` patterns are guarded with
  `if (m_dmp->dm_dmp)`.  `doevent.c` returns early (skipping X11 event
  dispatch) when `DMP` is NULL.  Files modified: `adc.c`, `axes.c`,
  `buttons.c`, `chgmodel.c`, `chgview.c`, `cmd.c`, `color_scheme.c`,
  `dm-generic.c`, `doevent.c`, `dozoom.c`, `edarb.c`, `edsol.c`, `fbserv.c`,
  `grid.c`, `mater.c`, `menu.c`, `mged.c`, `overlay.c`, `predictor.c`,
  `rect.c`, `scroll.c`, `set.c`, `share.c`, `titles.c`, `usepen.c`.

### libdm removal (remaining work)

The following dm plugin files can be removed once their callers are updated:

- `qtgl/` — used by `libqtcad/QgGL.cpp` (HW GL framebuffer for qged's `rt` output)
  and as a fallback in `QgEdMainWindow` when Obol is absent or SW-mode is not
  available with dual-GL.  Requires updating `QgGL.cpp` to use an Obol framebuffer.
- `glx/` — used by mged's legacy `attach -t ogl` path.  Can be removed once
  mged is fully migrated to `obol_view`.
- `swrast/` — used by `qged_test` / `qged_pipeline_test` for headless rendering.
  Can be removed once those tests are migrated to Obol OSMesa path.
- `null/`, `plot/`, `postscript/` — non-GL plugins; can be removed once all
  callers use Obol or libfb for output.

Remaining libdm work:
- Remove `dm-gl.c`, `dm-gl_lod.cpp`, `dm-generic.c`, `dm_plugins.cpp` from the build
- Delete: `include/dm.h`, `include/dm/` tree
- Update all callers (mged's legacy dm path, archer, libged/view.cpp) to use Obol equivalents
- Replace `QgGL.cpp`'s `dm_open("qtgl")` framebuffer with an Obol-based framebuffer
  (prerequisite for removing dm-qtgl)

### MGED refactoring for libdm removal

MGED's internal display-manager infrastructure (`mged_dm` / `active_dm_set`) is the
largest single dependency on libdm remaining after Stage 6 is complete.  This section
describes the planned refactoring path so that future work can proceed incrementally.

**Current MGED DM architecture (to be replaced):**

`struct mged_dm` in `mged_dm.h` is a fat per-pane struct that owns:
- `struct dm *dm_dmp` — the libdm display manager (GL context + draw commands)
- `struct fb *dm_fbp` — the libdm framebuffer
- Network framebuffer socket state (`dm_netfd`, `dm_clients[]`)
- Per-pane overlay state (predictor vlist, trails, scroll bars)
- Shareable display resources (`_view_state`, `_adc_state`, `_menu_state`, …)
- A `cmd_list *dm_tie` link that associates a Tcl command history to a pane

`active_dm_set` is a `bu_ptbl` of `mged_dm*` pointers.  `set_curr_dm()` switches
the "active" pane; the `DMP` macro returns `s->mged_curr_dm->dm_dmp`.

Hundreds of mged source files use `DMP`, `fbp`, `dm_get_*`, `dm_set_*`, and
`dm_draw_*` directly, making a bulk replacement non-trivial.

**Target MGED view architecture (libdm-free):**

Each pane becomes a `bsg_view` registered in `s->gedp->ged_views`, with:
- `dmp = NULL` (no libdm; Obol `obol_view` Tk widget does the rendering)
- `u_data` pointing to a `tclcad_view_data` (already the case for Obol panes
  created by `f_new_obol_view_ptr`)

A new, smaller `mged_pane` struct replaces `mged_dm`, retaining only what is
needed after libdm is gone:
```c
struct mged_pane {
    bsg_view          *mp_gvp;       /* the view this pane displays */
    struct cmd_list   *mp_cmd_tie;   /* Tcl command-history link */
    /* per-pane overlay state that isn't moving to bsg_view: */
    struct bu_list     mp_p_vlist;   /* predictor vlist */
    struct trail       mp_trails[NUM_TRAILS]; /* NUM_TRAILS defined in mged_dm.h */
    /* … scroll/menu/adc state still needed after libdm removal … */
};
```

`active_pane_set` (a `bu_ptbl` of `mged_pane*`) replaces `active_dm_set`.
`set_curr_pane()` replaces `set_curr_dm()` and sets `s->gedp->ged_gvp` directly
from `mp_gvp` (no DMP indirection).

**Migration steps (incremental, preserving backward compatibility):**

1. **✅ Guard all `DMP` uses** — Added `if (!DMP) return;` guards to all MGED
   overlay drawing functions (`adcursor`, `draw_e_axes`, `draw_m_axes`,
   `draw_v_axes`, `draw_rect`, `draw_grid`, `dotitles`, `predictor_frame`,
   `dozoom`, `scroll_display`, `mmenu_display`, `mged_highlight_menu_item`).
   Added `if (DMP)` wrappers around all standalone `dm_set_dirty(DMP, 1)`,
   `dm_set_debug(DMP, ...)`, and `dm_set_perspective(DMP, ...)` calls in
   `buttons.c`, `chgmodel.c`, `chgview.c`, `cmd.c`, `dm-generic.c`, `mater.c`,
   `edarb.c`, `overlay.c`, `usepen.c`, `edsol.c`, `fbserv.c`, and `set.c`.
   Added `if (m_dmp->dm_dmp)` guards to all loop-based `dm_set_dirty` patterns
   in `adc.c`, `axes.c`, `color_scheme.c`, `grid.c`, `menu.c`, `mged.c`,
   `rect.c`, `set.c`, and `share.c`.  Added a `if (!DMP)` early-return in
   `doevent.c` so that X11 events are not dispatched through a null DMP.

2. **✅ Add `mged_pane` alongside `mged_dm`** — `struct mged_pane` added to
   `mged_dm.h`; `active_pane_set` (a `bu_ptbl`) added in `attach.c`;
   `set_curr_pane()` added.  `f_new_obol_view_ptr` now creates a `mged_pane` and
   registers it in `active_pane_set`.  The old `::obol_pane_gvp` Tcl array is gone.

3. **✅ Migrate `f_winset` fully** — `f_winset` in `cmd.c` now checks
   `active_pane_set` first (matched by `mp_gvp->gv_name`), then falls back to
   `active_dm_set` for legacy dm panes.  The `::obol_pane_gvp` Tcl-variable bridge
   has been removed from `f_winset` and `mview.tcl`.

4. **Migrate `refresh()`** — `mged.c`'s `refresh()` already skips dm drawing when
   `DMP` is NULL (replaced by `obol_notify_views`).  Once all panes are Obol, remove
   the dm draw block entirely.

5. **Migrate per-pane overlay rendering** — adc, predictor, trails, menu overlays
   currently draw via libdm vlist calls.  Each needs an Obol `SoOverlay` / vlist→Obol
   conversion, or can be deferred until after libdm is gone by simply skipping
   overlay drawing when `DMP == NULL`.

6. **Remove `mged_dm` and `active_dm_set`** — Once all panes use `mged_pane` and
   no remaining mged code references `DMP` unconditionally, delete `struct mged_dm`,
   `active_dm_set`, the `DMP`/`fbp`/`clients` macros, and everything in
   `src/mged/dm-generic.c`.

7. **Remove `attach` command's dm backend** — `gui_setup()` in `attach.c` currently
   contains the full `dm_open` path for the legacy GL path.  Once step 6 is done,
   `gui_setup()` becomes an Obol-only setup function.

**Key files to update (Stage 7 MGED work):**

| File | Change |
|------|--------|
| `mged/mged_dm.h` | Add `mged_pane`; deprecate `mged_dm`; add DMP-null guards to macros |
| `mged/attach.c` | Remove legacy dm_open path from `gui_setup()`; keep `f_new_obol_view_ptr` |
| `mged/cmd.c` | Migrate `f_winset` to `active_pane_set`; remove `active_dm_set` loop |
| `mged/mged.c` | `refresh()`: remove dm_draw block (already guarded); call only obol_notify |
| `mged/share.c` | Sharing of view state between panes needs pane→pane (not dm→dm) linkage |
| `mged/dm-generic.c` | Delete (all `mged_dm` display-manager glue) |
| `mged/fbserv.c` | Replace with Obol fb overlay path (see Stage 7 ert/fbserv work) |

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
