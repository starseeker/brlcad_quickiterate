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
- **Step 4/5 (Obol pane lifecycle — `release` and `refresh`)**: Fixed two
  correctness gaps in the Obol pane lifecycle:
  1. **`release()` now handles Obol panes** (`attach.c`): When the name
     passed to `release` is not found in `active_dm_set`, it also checks
     `active_pane_set` via `mged_pane_find_by_name()`.  If found, it:
     removes the `mged_pane` from `active_pane_set`; removes the `bsg_view`
     from `ged_views` (`bsg_scene_rm_view`); frees the `tclcad_view_data`
     user-data; removes the view from `ged_free_views`; and calls
     `bsg_view_free()` + `bu_free()` to fully teardown the Obol pane.
     This fixes a leak where `releasemv` in `mview.tcl` silently failed to
     clean up Obol panes (the error was swallowed by `catch`).
  2. **`mview.tcl` adds `<Destroy>` binding** for Obol pane widgets: Each
     `obol_view` pane now gets `bind $w.$pane <Destroy> "catch {release …}"`
     so cleanup happens even if the widget is destroyed without going through
     the `releasemv` Tcl proc.
  3. **mged shutdown cleans up `active_pane_set`** (`mged.c`): The orderly
     shutdown path (formerly cleaning only `active_dm_set`) now also iterates
     `active_pane_set`, frees the `tclcad_view_data` on each Obol pane's
     `bsg_view`, frees the `mged_pane` struct, and frees `active_pane_set`
     itself.  This prevents `tclcad_view_data` from leaking at process exit.
  4. **`refresh()` gates `obol_notify_views`**: The call is now conditional
     on `BU_PTBL_LEN(&active_pane_set) > 0` (Obol panes exist) AND
     `obol_needs_refresh || do_time` (something actually changed).  The
     `obol_needs_refresh` variable is set at the top of `refresh()` from
     `s->update_views` and from any `vs_flag` that was set on an
     `active_dm_set` view state.  This avoids unnecessary Obol re-renders
     on idle timer ticks when the scene is quiescent.

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

4. **✅ Migrate `refresh()`** — `refresh()` now gates `obol_notify_views` on
   `obol_needs_refresh || do_time` (something actually changed) AND
   `active_pane_set` being non-empty, preventing idle re-renders.  The legacy
   dm draw block (now guarded by `if (!DMP) continue`) remains for backward
   compatibility; it will be removed in Step 6 once all panes use `mged_pane`.

5. **✅ Migrate per-pane overlay rendering** — adc, predictor, trails, menu
   overlays are guarded at function entry (`if (!DMP) return`) so Obol panes
   silently skip libdm overlay drawing.  Full Obol overlay support (SoOverlay
   nodes) is deferred until Step 6 when libdm is fully removed.  Additionally,
   the Obol pane `release()` path and `<Destroy>` binding (see Step 4/5
   in Completed Stage 7 work) ensure per-pane state is correctly freed when
   a pane is closed.

5.5 **✅ Add `mged_curr_pane` to `mged_state`** (Step 6 prep) —
   `struct mged_pane *mged_curr_pane` added to `mged_state` in `mged.h`.
   `set_curr_pane()` now sets both `s->gedp->ged_gvp` AND `s->mged_curr_pane`,
   giving all mged code a direct pointer to the active Obol pane.  Initialized
   to `MGED_PANE_NULL` at startup.  `refresh()` now checks `s->mged_curr_pane`
   as a fast-path guard in the `obol_notify_views` condition, eliminating the
   `BU_PTBL_LEN` call when no Obol pane has ever been activated.  The orphaned
   `extern "C" int draw_points` forward declaration was removed from `draw.cpp`
   (the implementation in `points_eval.c` is superseded by `rt_generic_scene_obj`
   mode-5 handling via `rt_sample_pnts`).

5.6 **✅ Unify view-dirty tracking: `vs_flag` → `s->update_views`** (Step 6.a) —
   All view-command code paths that previously set only `view_state->vs_flag = 1`
   now also set `s->update_views = 1`.  This ensures that `obol_notify_views` fires
   correctly in the Obol path (which reads `s->update_views` via `obol_needs_refresh`)
   even when the `active_dm_set` vs_flag scan is eventually removed in step 6.
   Files updated: `chgview.c` (17 sites), `edsol.c` (5 sites), `edarb.c` (2 sites),
   `dodraw.c`, `tedit.c`, `rtif.c`, `setup.c`, `rect.c`, `menu.c`, `cmd.c` (3 sites),
   `usepen.c` (3 sites), `mged.c` (2 sites in `new_edit_mats` and `mged_view_callback`).
   The comment in `refresh()` updated to reflect this invariant.  The `view_changed_hook`
   in `dm-generic.c` was also fixed by adding a `struct mged_state *hs_s` back-pointer
   to `mged_view_hook_state` and setting `hs->hs_s->update_views = 1` there.
   **All** MGED view-change paths now set both `vs_flag` and `s->update_views`.

5.7 **✅ Propagate `update_views` through `*_set_dirty_flag` hooks** —
   The `bu_structparse` variable-change hooks for axes settings (`ax_set_dirty_flag`
   in `axes.c`), color-scheme settings (`cs_set_dirty_flag` in `color_scheme.c`),
   grid settings (`grid_set_dirty_flag` in `grid.c`), display-manager variables
   (`set_dirty_flag` in `set.c`), and ADC cursor state (`adc_set_dirty_flag` in
   `adc.c`) now all set `s->update_views = 1` alongside the legacy `dm_dirty` flag.
   This ensures the Obol path repaints whenever any of these settings change.
   Also: `overlay.c` dmp assignment hardened (`DMP ? (void *)DMP : NULL` instead of
   unconditional dereference); `clone.c` draw-skip check extended to also allow a
   redraw when `s->mged_curr_pane` is set (an Obol pane is active).

5.8 **✅ Add `DbiState::wait_for_pipeline()` + `wait_pipeline` GED command** —
   New `DbiState::wait_for_pipeline(int max_ms)` polls `drain_geom_results()` in a
   1 ms sleep loop until the background draw pipeline `settled()` or the timeout
   expires.  Exposed as a GED command `wait_pipeline [max_ms]` registered in
   `src/libged/draw/draw.c`; the `ged_exec_wait_pipeline` C API is auto-generated
   in `ged_cmds.h`.  `test_dbi_cpp.cpp` updated to use `wait_for_pipeline()` instead
   of hand-rolled poll loops.

5.9 **✅ Add per-pane state to `mged_pane` (Step 6 prep)** —
   `struct mged_pane` now includes the same "shareable resource" pointer fields
   that `mged_dm` carries (`mp_view_state`, `mp_color_scheme`, `mp_axes_state`, …).
   Two new functions in `attach.c`:
   - `mged_pane_init_resources(s, mp)` — allocates and copies initial state from
     `mged_dm_init_state` (mirrors what `dm_var_init()` does for legacy dm panes).
   - `mged_pane_free_resources(mp)` — frees the per-pane state on teardown.
   `f_new_obol_view_ptr()` now calls `mged_pane_init_resources()` immediately
   after allocating the pane.  `mged_pane_release()` calls `mged_pane_free_resources()`
   before freeing the pane struct.  The macros were then updated in step 5.10 below.

5.10 **✅ Change state macros to ternary pane-first form (Step 6)** —
   All per-pane state macros (`view_state`, `adc_state`, `menu_state`, `rubber_band`,
   `mged_variables`, `color_scheme`, `grid_state`, `axes_state`, `dlist_state`) now
   use a ternary expression that prefers `s->mged_curr_pane->mp_*` when
   `s->mged_curr_pane` is non-null, falling back to `s->mged_curr_dm->dm_*` for
   legacy dm panes.  Pre-requisites met first:
   - `dm_var_init()` in `attach.c` changed to use explicit `s->mged_curr_dm->dm_*`
     for all `BU_ALLOC` calls (those need lvalue assignment, not ternary result).
   - Startup init in `mged.c` likewise changed to explicit `s->mged_curr_dm->dm_*`.
   - `f_postscript()` in `cmd.c`: `view_state = vsp` changed to
     `s->mged_curr_dm->dm_view_state = vsp`; `menu_state = dml->dm_menu_state`
     changed to `s->mged_curr_dm->dm_menu_state = dml->dm_menu_state`.
   All MGED .c files compile cleanly after the macro change.

5.11 **✅ Wire `mp_view_state` + `active_pane_set` view loops (Session 13)** —
   Six improvements to make Obol panes participate in all view operations:
   1. `mged_pane_init_resources()` now allocates a `_view_state` shell
      (`mp->mp_view_state`) with `vs_gvp = mp->mp_gvp` and calls `view_ring_init()`.
      `mged_pane_free_resources()` frees view_ring items + the shell (not `vs_gvp`).
      This makes the ternary `view_state` macro safe for Obol panes.
   2. `chgview.c` `edit_com()` and `cmd_autoview()` iterate `active_pane_set` after
      `active_dm_set` so Obol panes also receive `autoview` when a new object appears.
   3. `mged.h` `mged_edit_state`: added `edit_rate_*_pane` fields.
      `chgview.c` sets them; `mged.c` knob loop prefers `set_curr_pane` for Obol.
      Added `active_pane_set` loop for view rate knobs (rot_m, rot_v, sca).
   4. `buttons.c`: `edit_accept`/`edit_reject` also reset `mv_transform` for Obol
      panes; `chg_state` calls `new_mats()` for Obol panes too.
   5. `grid.c` `update_grids()`: scales `mp_grid_state->res_h/v` for Obol panes.
   6. `mged.c` `refresh()`: clears `mp_view_state->vs_flag` for Obol panes alongside
      the existing `active_dm_set` loop.

5.12 **✅ `set_curr_pane` now redirects `mged_curr_dm` to nu-init-state (Session 13)** —
   `set_curr_pane()` now additionally sets `s->mged_curr_dm = mged_dm_init_state`
   (the "nu" headless dm created at startup with `dm_dmp == NULL`).  This ensures
   that whenever an Obol pane is active:
   - `DMP == NULL` so all legacy libdm drawing guards (`if (!DMP) return;`) fire
     immediately — no special-casing needed.
   - `s->mged_curr_dm` points to a valid struct (not dangling), preventing any
     accidental dereference of a legacy dm pointer.
   - The ternary macros (`view_state`, `color_scheme`, etc.) continue to prefer
     `mp->mp_*` because `mged_curr_pane` is non-NULL.
   All `active_pane_set` loops that call `set_curr_pane` now also restore
   `mged_curr_dm` via `set_curr_dm(s, save_m_dmp)` when there was no Obol pane
   active before the loop (save_pane == NULL), preventing mged_curr_dm from
   being left pointing at mged_dm_init_state after the loop.
   Comments in `attach.c` and `mged_dm.h` updated to reflect the new behaviour.

5.13 **✅ `mged_pane` Tcl HUD display variable names (Session 13)** —
   `mged_pane` now has `mp_fps_name`, `mp_aet_name`, `mp_ang_name`, `mp_center_name`,
   `mp_size_name`, `mp_adc_name` fields (mirrors `dm_fps_name` etc. in `mged_dm`).
   `mged_pane_init_resources()` initialises these via `bu_vls_init()`.
   `mged_pane_free_resources()` frees them via `bu_vls_free()`.
   New `mged_pane_link_vars()` function (attach.c) populates them from `gv_name`.
   Called automatically by `f_new_obol_view_ptr` after `mged_pane_init_resources`.
   This prepares for a future `obol_dotitles()` that updates the Obol pane HUD
   display Tcl variables (`$::mged_display($path,fps)` etc.).

5.14 **✅ Remove `dm_open("nu")` from mged startup + attach path** —
   The initial "nu" `mged_dm` entry (`mged_dm_init_state`) now has `dm_dmp == NULL`
   without opening a libdm plugin at startup.  Previously, `dm_open(NULL, s->interp,
   "nu", 0, NULL)` was called unconditionally in `mged.c` to create the sentinel dm.
   Now, `s->mged_curr_dm->dm_dmp` stays NULL from the `BU_ALLOC` zero-init.  All
   DMP uses in mged have already been guarded with `if (!DMP)` (step 1), so NULL is
   safe throughout.

   A second `dm_open("nu")` call in `mged_attach()` was used to bootstrap option
   parsing (`-d display_string`) before calling `gui_setup()`.  This is replaced
   with a direct argv scan for the `-d` option, removing that dm_open dependency.

   Additional null-guard cleanups in this step:
   - `mged_link_vars()` (attach.c): returns early when `p->dm_dmp` is NULL.
   - `f_get_dm_list()` (attach.c): skips entries with NULL `dm_dmp`.
   - `f_dm()` (attach.c): returns "nu" for `dm type` and error for other subcommands
     when DMP is NULL.
   - `release()` (attach.c): the "nu pathname" check is replaced with a `!DMP` check.
   - `mged_finish()` (mged.c): frees `mged_dm` struct even when `dm_dmp` is NULL.
   - `dozoom.c` `createDListSolid`/`freeDListsAll`: skip entries with NULL `dm_dmp`.
   - `share.c` `share_dlist`/`f_share`: null guards for dm_dmp in all loops.
   - `set.c` dlist create loop: null guard for dm_dmp.
   - `edsol.c` ARB vertex dialog: null guard for `dm_get_dname`.
   - `mged.c` Mac OS X bg hack: guarded with `if (DMP)`.

   **Result**: mged no longer depends on the libdm "nu" plugin at startup.
   The remaining `dm_open` call is only in `mged_dm_init()` which is called from
   `mged_attach()` when the user explicitly attaches a graphical display manager
   (ogl, swrast, etc.) — the prerequisite for step 6.

5.15 **✅ Add predictor state to `mged_pane`; `pv_head`/`pane_trails` macros** —
   `struct mged_pane` gains `mp_p_vlist` (predictor vlist head), `mp_trails[NUM_TRAILS]`
   (velocity-predictor trail history), and `mp_ndrawn` (drawn-object count used by
   `usepen.c` for selection zone calculation).  Two new ternary macros in `mged_dm.h`:
   - `pv_head` — pointer to the active pane's `bu_list` vlist head (prefers
     `mged_curr_pane->mp_p_vlist` when set, falls back to `mged_curr_dm->dm_p_vlist`).
   - `pane_trails` — pointer to the active pane's `struct trail[NUM_TRAILS]` array.
   `predictor.c` is fully migrated to use `pv_head` and `pane_trails[i]` instead of
   `s->mged_curr_dm->dm_p_vlist` and `s->mged_curr_dm->dm_trails[i]`.  This removes
   all 45 `mged_curr_dm` references from `predictor.c`.  `predictor_init_pane(mp)`
   is added for Obol pane initialization (called by `mged_pane_init_resources()`).
   `usepen.c` uses `mp_ndrawn` when `mged_curr_pane` is active.
   `dozoom.c` uses `pv_head` for predictor vlist drawing.
   `mged_pane_release()` frees `mp_p_vlist` via `BSG_FREE_VLIST`.

5.16 **✅ Simplify `refresh()` dirty-flag scan** —
   The `active_dm_set` dirty-flag scan loop in `refresh()` previously checked
   `vs_flag` in addition to `s->update_views`.  Step 5.6 already made `update_views`
   the canonical dirty signal (every code path that sets `vs_flag = 1` also sets
   `s->update_views = 1`, verified by inspection).  The scan loop is now:
   ```c
   if (s->update_views) {
       for each legacy dm entry: p->dm_dirty = 1;
   }
   ```
   `obol_needs_refresh` is captured from `s->update_views` alone (no longer ORed with
   `vs_flag`).  The vs_flag *clearing* loops are retained as housekeeping.

5.17 **✅ Move null-dm guard before `set_curr_dm` in `refresh()` draw loop** —
   The `if (!DMP) continue` guard in the `active_dm_set` draw loop in `refresh()` was
   previously placed AFTER `set_curr_dm(s, p)`.  It is moved to BEFORE the call so
   that `set_curr_dm` (and the mged_curr_dm redirect) is entirely skipped for null-dm
   entries (the startup sentinel and Obol-redirect entries).  In Obol-only mode (where
   every entry in `active_dm_set` has `dm_dmp == NULL`) the loop body is now a
   complete no-op without any side-effects on `mged_curr_dm`.

   The same `if (!m_dmp->dm_dmp) continue` guard has been applied to **all**
   `active_dm_set` loops across the mged source tree in this step:
   - `mged.c`: `new_edit_mats()`, view-rate knob loop
   - `chgview.c`: `edit_com`, `cmd_autoview`, `f_svbase` shared-view dirty loop
   - `grid.c`, `adc.c`, `axes.c`, `color_scheme.c`, `set.c`: all `*_set_dirty_flag` hooks
   - `buttons.c`: `be_repl`/`be_reject` mv_transform loops, `chg_state` new_mats loop
   - `menu.c`: `mmenu_set`, `mmenu_set_all`
   - `fbserv.c`: client-fd lookup, netfd lookup; `fbserv_set_port` gets `if (!DMP) return`
   - `cmd.c`: `f_opendb` resize loop
   - `set.c`: `set_knob_dirty_flag()`, display-list create/free loops
   - `set.c`: **Bug fix** — inner dlist-sharing loop used outer-loop index `di` instead
     of `dj` in `BU_PTBL_GET` (pre-existing typo now corrected)
   - All redundant `if (m_dmp->dm_dmp) dm_set_dirty(...)` replaced with direct
     `dm_set_dirty(...)` calls (null check now at loop top)

6.a **✅ Register legacy dm panes in `active_pane_set` (Step 6 prep)** —
   `active_pane_set` is promoted from an Obol-only registry to the **complete pane
   registry** covering both Obol panes and legacy GL panes.

   **Data model change** — `struct mged_pane` gains a new `mp_dm` field:
   - `mp_dm == NULL`: Obol pane (created by `f_new_obol_view_ptr`).  `mp_*` resource
     pointers are owned by the `mged_pane` itself; freed by `mged_pane_free_resources`.
   - `mp_dm != NULL`: *thin wrapper* around a legacy `mged_dm` (created in
     `mged_attach()`).  `mp_*` resource pointers are SHARED with the `mged_dm` and
     must NOT be freed by `mged_pane_free_resources`.

   **`mged_attach()` changes**:
   - After `mged_dm_init()` succeeds, sets `gv_name` on the dm's `vs_gvp` to the dm
     pathname (so `mged_pane_find_by_name` can find it).
   - Allocates a thin `mged_pane` wrapper with `mp_dm = s->mged_curr_dm`, `mp_gvp =
     vs_gvp`, shared resource pointers, `mp_cmd_tie = dm->dm_tie`.
   - Calls `mged_pane_init_resources` (shares dm ptrs) and `mged_pane_link_vars`.
   - Inserts wrapper into `active_pane_set`.

   **`release()` changes**: finds the thin wrapper via `mp_dm == s->mged_curr_dm` and
   removes/frees it from `active_pane_set` BEFORE `usurp_all_resources()` nulls the
   dm pointers.  `mged_pane_free_resources` skips freeing shared pointers when
   `mp_dm != NULL`.

   **`set_curr_pane()` change**: when `mp->mp_dm != NULL`, redirects
   `s->mged_curr_dm = mp->mp_dm` (restores the real dm for legacy GL drawing) instead
   of redirecting to the null sentinel.  The ternary macros (`view_state` etc.) still
   return `mp_*` values (which equal `dm->dm_*` since they're shared).

   **`pv_head` / `pane_trails` macros** updated: use `dm_p_vlist` / `dm_trails` when
   `mged_curr_pane->mp_dm != NULL` (predictor state lives in the `mged_dm` for
   wrappers; the wrapper's inline fields are unused and kept zero-init'd).

   **`refresh()` guards**: All `active_pane_set` loops that handle Obol-only concerns
   gain `if (mp->mp_dm) continue` guards:
   - View-rate knob loop
   - `vs_flag` clear loop
   - `obol_notify_views` condition: now checks for Obol-only panes (`mp_dm == NULL`)
     rather than `BU_PTBL_LEN(&active_pane_set) > 0` (which would always be true).

   **`mged_finish()` shutdown**: the `active_dm_set` shutdown loop now also removes
   and frees the thin wrapper from `active_pane_set` before freeing the `mged_dm`.
   The `active_pane_set` shutdown loop skips entries with `mp_dm != NULL` (already
   freed by `active_dm_set` loop).

   **`f_winset` / `mged_pane_find_by_name`**: no code changes needed — the existing
   `mged_pane_find_by_name` + `set_curr_pane` path already handles both pane types
   correctly since they're both in `active_pane_set` with a valid `gv_name`.  The
   `active_dm_set` fallback in `f_winset` is retained as a safety net for dm panes
   attached before this code was in place.

   **Result**: `active_pane_set` is now the authoritative source of truth for all
   active panes.  `active_dm_set` is now redundant (all its entries that have real
   dm panes also have wrappers in `active_pane_set`), making Step 6 (full removal of
   `active_dm_set` and `struct mged_dm`) achievable in a subsequent session.

   **Remaining work for Step 6 proper**: migrated (Step 6.b below) — all rendering
   loops now use `active_pane_set`.  Remaining: delete `struct mged_dm`,
   `active_dm_set`, `DMP`/`fbp` macros, and `dm-generic.c`.

6.b **✅ Migrate all `active_dm_set` iteration loops to `active_pane_set`** —
   All 15 files that had `active_dm_set` rendering/dirty/update loops have been
   migrated to use `active_pane_set` with `if (!mp->mp_dm) continue` guards:
   `adc.c`, `axes.c`, `grid.c`, `color_scheme.c`, `menu.c`, `rect.c`, `edsol.c`,
   `buttons.c`, `chgview.c`, `cmd.c`, `set.c`, `mged.c`, `share.c`, `dozoom.c`,
   `fbserv.c`.  The `GET_MGED_DM` macro in `mged_dm.h` also migrated.  Paired
   `active_dm_set` + Obol `active_pane_set` loops throughout (added in earlier
   sessions) are collapsed to a single unified `active_pane_set` loop.

   `active_dm_set` now only appears in `attach.c` (insert/remove at dm attach/detach)
   and `mged.c` (init/free at startup/shutdown).  These are Step 6.c.

6.c **✅ Remove `active_dm_set` insert/remove/init/free** —
   `bu_ptbl_ins`/`bu_ptbl_rm` in `attach.c` and `bu_ptbl_init`/`bu_ptbl_ins`/
   `bu_ptbl_free` in `mged.c` all removed.  `mged_finish` shutdown loop migrated
   to iterate `active_pane_set` for legacy dm wrapper panes (mp_dm != NULL) and
   free them directly.  `active_dm_set` definition removed from `attach.c`;
   `extern active_dm_set` declaration removed from `mged_dm.h`.

6. **Remove `mged_dm` and `active_dm_set`** ✅ (Steps 6.a–6.c done) —
   `active_dm_set` is fully gone.  `active_pane_set` is the sole pane registry.

7. **Remove `attach` command's dm backend** (Step 7.1–7.3 done in Session 18):

   **Step 7.1a** ✅ — `mged_attach()` calls `set_curr_pane(s, wrapper_pane)` after
   creating the legacy dm wrapper pane.  `mged_curr_pane` is now non-NULL after
   any `attach` command.

   **Step 7.1b** ✅ — `release()` uses `set_curr_pane()` (not `set_curr_dm()`) to
   update both `mged_curr_pane` and `mged_curr_dm` when releasing a dm.  The named
   dm switch uses `set_curr_pane(s, mp)` for save/restore.

   **Step 7.2** ✅ — Startup creates a sentinel "init pane" (`s->mged_init_pane`)
   that wraps `mged_dm_init_state`.  `mged_curr_pane` is set to `init_pane` right
   after resources are allocated (before any `attach`), so `mged_curr_pane` is
   **always non-NULL** after startup.  `mged_finish()` frees `mged_init_pane`.

   **Step 7.3** ✅ — Ternary macros (`view_state`, `adc_state`, `menu_state`,
   `rubber_band`, `mged_variables`, `color_scheme`, `grid_state`, `axes_state`,
   `dlist_state`, `pv_head`, `pane_trails`) simplified to direct `mp_*` access
   (no `? mged_curr_pane : mged_curr_dm` fallback).  Dead-code `if (!save_pane)
   set_curr_dm(...)` lines removed from all 10 affected files.

   **Step 7.4** ✅ (Session 19) — Migrate `cmd_list::cl_tie` from `mged_dm *` to
   `mged_pane *`; remove `edit_rate_*_dm` fields from `mged_edit_state`.

   *7.4a* `cmd_list::cl_tie` is now `struct mged_pane *` (was `struct mged_dm *`).
   `f_tie` in `cmd.c` now finds and stores a `mged_pane*` directly.  Tie/untie
   logic uses `mp_cmd_tie` as the back-link (clears `mp_cmd_tie = CMD_LIST_NULL`)
   and keeps `mp_dm->dm_tie` in sync for legacy-dm wrappers.  `release()` in
   `attach.c` clears both `cl_tie` and `dm_tie` on teardown.  The two
   `set_curr_dm(s, curr_cmd_list->cl_tie)` calls in `mged.c` (`stdin_input` and
   `stdin_input_Tcl`) are replaced with `set_curr_pane(s, curr_cmd_list->cl_tie)`.
   The `cmd_win set` handler in `cmd.c` likewise uses `set_curr_pane`.

   *7.4b* `mged_edit_state::edit_rate_{mr,or,vr,mt,vt}_dm` fields removed.  The
   five matching assignments in `chgview.c` are gone.  `event_check` in `mged.c`
   no longer has `else set_curr_dm(…, edit_rate_*_dm)` fallbacks — it calls
   `set_curr_pane(s, s->s_edit->edit_rate_*_pane)` directly (safe because
   `mged_curr_pane` is always non-NULL and the pane field is set whenever the
   rate flag is set in chgview.c).  All 26 mged .c files compile cleanly.

   **Step 7.5** ✅ (Session 19) — Migrate all remaining `set_curr_dm()` callers
   to `set_curr_pane()`.  `set_curr_dm()` is now only called by `set_curr_pane()`
   itself.

   - `doevent.c`: `doEvent()` now uses `GET_MGED_PANE` (new macro in `mged_dm.h`)
     to find the pane for the X11 event window, then calls `set_curr_pane`.
     `struct mged_dm *save_dm_list` replaced by `struct mged_pane *save_pane`.
   - `dozoom.c`: the three vectorThreshold restore guards changed from
     `if (s->mged_curr_dm != save_dm_list) set_curr_dm(...)` to
     `if (s->mged_curr_pane != save_pane) set_curr_pane(...)`.
   - `fbserv.c` `fbserv_new_client_handler`: uses `save_pane` + `set_curr_pane`
     for restore.  The `else set_curr_dm(dlp)` fallback in
     `fbserv_existing_client_handler` removed.
   - `share.c`: `createDListAll` call path now uses pane save/restore loop.
   - `mged.c` `mged_finish`: `set_curr_dm(s, MGED_DM_NULL)` replaced with
     direct `s->mged_curr_dm = MGED_DM_NULL` (inside pane-already-freed loop).
   - `attach.c`: `mged_attach()` saves/restores `o_pane` on `gui_setup` error.
     `release()` fallback uses direct assignment rather than `set_curr_dm`.
   - `cmd.c` `f_postscript`: uses `dml_pane` + `set_curr_pane` for restore.
   - `cmd_win set`: already migrated to `set_curr_pane` in Step 7.4.

   **`set_curr_dm()` is now called only from `set_curr_pane()` itself** and from
   the `set_curr_dm` function definition.  All external callers eliminated.
   26 mged .c files compile cleanly with -Werror.

   **Step 7.6** ✅ (Session 20) — Migrate remaining `mged_curr_dm->dm_*` direct
   accesses to use `mged_curr_pane` fields:
   - `titles.c` `dotitles()`: all six `dm_*_name` Tcl variable name references
     changed to `s->mged_curr_pane->mp_*_name` (center, size, aet, ang, adc ×2, fps).
     `mged_pane_link_vars()` is already called for both legacy-dm wrapper panes and
     Obol panes, so `mp_*_name` is always populated.
   - `dozoom.c`: `dm_ndrawn` accumulations changed to `mp_ndrawn`, making
     `mp_ndrawn` the authoritative drawn-object counter for all pane types.
   - `usepen.c`: `mp_dm ? mp_dm->dm_ndrawn : mp_ndrawn` ternary simplified to
     always use `mp_ndrawn` (safe now that dozoom writes mp_ndrawn).
   - `clone.c`: `mged_curr_dm != mged_dm_init_state || mged_curr_pane` replaced
     with `mged_curr_pane != mged_init_pane` (always-true condition fixed).
   - `edsol.c` `get_rotation_vertex()`: display name obtained from
     `mp->mp_dm->dm_dmp` (legacy) or `mp->mp_gvp->gv_name` (Obol) instead of
     `mged_curr_dm->dm_dmp`.
   - `share.c` `usurp_all_resources()`: `MGED_STATE->mged_curr_dm->dm_dlist_state`
     replaced with `dlp2->dm_dlist_state` (correct free of the dying dm's state).
   - `attach.c` `set_curr_pane()` comment updated to note that `set_curr_dm()`
     is now internal-only.

      **Step 7.7** ✅ (Session 21) — Migrate `save_m_dmp`/`save_dlp` patterns and
   fbserv.c to use pane pointers:
   - `set.c` `set_scroll_private()`: `save_m_dmp = s->mged_curr_dm` replaced with
     `save_mv = save_pane->mp_mged_variables`; comparison `mp->mp_mged_variables ==
     save_m_dmp->dm_mged_variables` simplified to `mp->mp_mged_variables == save_mv`.
   - `set.c` `set_dlist()`: `save_dlp = s->mged_curr_dm` replaced with
     `save_mv = save_pane->mp_mged_variables`; both `dm_mged_variables != save_dlp->…`
     comparisons replaced with `!= save_mv`.
   - `fbserv.c` `fbserv_set_port()`: `cdm = s->mged_curr_pane->mp_dm` introduced as a
     local after the `!DMP` guard; all `s->mged_curr_dm->dm_netchan/dm_netfd` replaced
     with `cdm->dm_netchan/dm_netfd` (the `!DMP` guard already ensures `mp_dm != NULL`).
   - `cmd.c` `f_postscript()`: `dml = s->mged_curr_pane->mp_dm` replaces
     `dml = s->mged_curr_dm`; post-`mged_attach` dm field accesses changed from
     `s->mged_curr_dm->dm_*` to `s->mged_curr_pane->mp_dm->dm_*`.

      **Step 7.8** ✅ (Session 22) — Migrate remaining `mged_curr_dm`-based macros
   to go through `mged_curr_pane->mp_dm`:
   - 22 macros changed: `DMP_dirty`, `fbp`, `clients`, `mapped`, `owner`, `am_mode`,
     `perspective_angle`, `zclip_ptr`, `cmd_hook`, `viewpoint_hook`, `eventHandler`,
     `adc_auto`, `grid_auto_size`, `dm_mouse_dx/dy`, `dm_omx/omy`, `dm_knobs`,
     `dm_work_pt`, `scroll_top/active/y/array`.  All are only accessed in code paths
     guarded by `if (!DMP) return;` or `if (!mp->mp_dm) continue;`, so `mp_dm != NULL`
     is guaranteed at every call site.  `DMP` itself keeps the `mged_curr_dm->dm_dmp`
     path for now (used as an lvalue in `mged_dm_init()`).
   - `pv_head` and `pane_trails` simplified from ternary to always use `mp_p_vlist` /
     `mp_trails` from the pane.  `mged_pane_init_resources()` for wrapper panes already
     called `predictor_init_pane(pane)`, so both Obol and legacy-dm wrapper panes have
     properly initialised predictor state in the pane fields.
   - `dm_var_init()` in `attach.c`: (a) **bug fix** — replaced `view_state->vs_gvp`
     (which expanded to the OLD pane's view_state) with a local `new_vs_gvp` pointer
     that initialises `s->mged_curr_dm->dm_view_state->vs_gvp` directly; (b) scalar
     field initialisations at the bottom (`DMP_dirty`, `mapped`, `owner`, `am_mode`,
     `adc_auto`, `grid_auto_size`) changed to explicit `s->mged_curr_dm->dm_*`.
   - `mged_dm_init()` in `attach.c`: `cmd_hook = dm_commands` → explicit
     `s->mged_curr_dm->dm_cmd_hook = dm_commands`.
   - `mged_attach()` in `attach.c`: `predictor_init(s)` removed (it incorrectly
     targeted the OLD pane's trails); replaced by `predictor_init_pane(pane)` inside
     `mged_pane_init_resources()`.  `BU_LIST_INIT(&dm_p_vlist)` kept for safe no-op
     teardown in `release()` / `mged_finish()`.
   - `mged_pane_free_resources()` for wrapper panes: `BSG_FREE_VLIST(&rt_vlfree,
     &mp->mp_p_vlist)` added — wrapper pane now owns the predictor vlist.

   **Step 7.9** ✅ (Session 23) — Migrate `DMP` macro from `mged_curr_dm->dm_dmp`
   to a conditional pane-based expression; remove dead `set_curr_dm()`:
   - `mged_dm.h`: `#define DMP` changed to:
     `(s->mged_curr_pane->mp_dm ? s->mged_curr_pane->mp_dm->dm_dmp : (struct dm *)NULL)`
     For Obol panes (mp_dm == NULL), DMP evaluates to NULL so all `if (!DMP) return;`
     guards fire without a NULL dereference.  `extern set_curr_dm` declaration removed.
   - `attach.c` `set_curr_dm()`: Deleted.  The function had no callers after Step 7.5.
     Its ged_gvp / gv_grid update logic was partially done by `set_curr_pane()`; the
     gv_grid update was moved inline into `set_curr_pane()` for wrapper panes.
   - `attach.c` `set_curr_pane()`: Removed `else { mged_curr_dm = mged_dm_init_state }`
     path for Obol panes — no longer needed since DMP is ternary.  Kept
     `mged_curr_dm = mp->mp_dm` for legacy wrapper panes (lifecycle code still reads it).
   - `attach.c` `mged_dm_init()`: All DMP and `view_state->vs_gvp` uses replaced with
     explicit `s->mged_curr_dm->dm_dmp` / `s->mged_curr_dm->dm_view_state->vs_gvp`.
     Also fixes the Step 7.2 carry-over bug: `view_state->vs_gvp` expanded to the OLD
     pane's view; replaced with the NEW dm's view (`dm_var_init` bug first fixed in 7.8
     for dm_var_init itself, now fixed here too).
   - `attach.c` `mged_attach()`: Added local `ndmp = s->mged_curr_dm->dm_dmp` after
     `mged_dm_init()` succeeds; replaced 6 DMP uses before `set_curr_pane()` with `ndmp`.
     Moved `mged_fb_open()` to AFTER `set_curr_pane()` so DMP / fbp correctly reference
     the new pane's dm.
   - `attach.c` `release()`: `else if (!DMP)` guard changed to
     `else if (!s->mged_curr_dm->dm_dmp)`; `if (fbp)` block changed to
     `if (s->mged_curr_dm->dm_fbp)` with explicit `dm_fbp` access throughout;
     `dm_close(DMP)` changed to `dm_close(s->mged_curr_dm->dm_dmp)`.
     This fixes a bug introduced by Step 7.8: in the `Bad:` path (name=NULL),
     `mged_curr_pane` is still the OLD pane, so fbp and DMP macros would have
     referenced the old dm's framebuffer / dmp instead of the new (bad) dm's.
   - `attach.c` `Bad:` label: `if (DMP != ...)` changed to
     `if (s->mged_curr_dm->dm_dmp != ...)` for the same reason.

   **After Step 7.9**:  `mged_curr_dm` is a purely-lifecycle field.  No macro
   expansion goes through it.  It is only read/written directly in: `set_curr_pane()`
   (update for wrapper panes), `release()`, `mged_attach()`, `dm_var_init()`,
   `mged_dm_init()`, `mged.c` startup.

   **Step 7.10** ✅ (Session 24) — Remove `mged_curr_dm` field from `mged_state`:
   - `mged.h`: `mged_curr_dm` field deleted; comment added explaining replacement paths.
   - `mged_dm.h`: `dm_var_init` extern updated to include explicit `ndm` parameter.
   - `attach.c` `dm_var_init(s, target_dm)` → `dm_var_init(s, target_dm, ndm)`: All
     `s->mged_curr_dm->` replaced with `ndm->`.  The `ndm` parameter is the new dm
     being initialised; `target_dm` is the source dm whose resources are copied.
   - `attach.c` `mged_dm_init(s, o_dm, ...)` → `mged_dm_init(s, o_dm, ndm, ...)`: Passes
     `ndm` to `dm_var_init()` and uses it for all field access (cmd_hook, dm_dmp, views,
     perspective).
   - `attach.c` `mged_attach()`: `BU_ALLOC(s->mged_curr_dm, ...)` replaced with
     `BU_ALLOC(ndm, ...)`.  `o_dm` obtained from `s->mged_curr_pane->mp_dm` (or sentinel).
     All subsequent uses of `s->mged_curr_dm` replaced with `ndm` or `ndm->*`.
     `share_dlist`, `ged_gvp`, pane creation all use `ndm` directly.
   - `attach.c` `set_curr_pane()`: `s->mged_curr_dm = mp->mp_dm` update removed (field gone).
     Function comment updated: DMP is ternary through pane, no dm redirect needed.
   - `attach.c` `release()`:
     - Signature: `release(s, name, need_close, bad_dm)` — `bad_dm` is only used
       when `name == NULL` (Bad: path from mged_attach).
     - `save_dm_list` replaced with `save_pane` (saves previous `mged_curr_pane`).
     - `if (mp->mp_dm != s->mged_curr_dm)` → `if (mp != s->mged_curr_pane)`.
     - Local `cdm` pointer: for name-given path `cdm = s->mged_curr_pane->mp_dm`
       (set_curr_pane already called); for name=NULL path `cdm = bad_dm`.
     - End-of-function restore: `set_curr_pane(s, save_pane)` (no dm pointer search).
     - `s->mged_curr_dm = MGED_DM_NULL` at end removed.
     - `f_release` updated: name-given calls `release(s, name, 1, NULL)`;
       name-NULL calls `release(s, NULL, 1, s->mged_curr_pane->mp_dm)`.
   - `mged.c` startup: `BU_ALLOC(mged_dm_init_state, ...)` direct (no `s->mged_curr_dm`).
     All ~20 `s->mged_curr_dm->` accesses replaced with `mged_dm_init_state->`.
     `mged_link_vars(mged_dm_init_state)` direct.
   - `mged.c` `mged_finish()`: `s->mged_curr_dm = MGED_DM_NULL` removed.

   **After Step 7.10**: `mged_curr_dm` is gone from `mged_state`.  `mged_dm_init_state`
   is the sole global `mged_dm *` (sentinel/headless dm).  `set_curr_pane()` no longer
   touches any dm field.  The `DMP` macro goes through `mged_curr_pane->mp_dm` only.

   **Step 7.11** ✅ (Session 24) — Remove `dm_tie` from `struct mged_dm`:
   - `mged_dm.h`: `dm_tie` field deleted from `struct mged_dm`; `mp_cmd_tie` comment
     updated to "canonical" (no longer "mirrors dm_tie").
   - `cmd.c` `f_tie()`: removed `tlp->mp_dm->dm_tie = clp` update (redundant with
     `tlp->mp_cmd_tie = clp`).  `mp_cmd_tie` is now the sole tie tracking field.
   - `cmd.c` `cmd_close()` and `f_winset()` untie paths: removed
     `clp->cl_tie->mp_dm->dm_tie = CMD_LIST_NULL` updates.
   - `attach.c` `release()`: moved cmd_tie clearing into the wrapper-pane cleanup
     block (before `BU_PUT`), clearing `wrapper->mp_cmd_tie` while the pane is
     still alive.  Removed the separate `cdm->dm_tie` check block.
   - `attach.c` `mged_attach()`: `pane->mp_cmd_tie = ndm->dm_tie` → `= NULL`
     (ndm->dm_tie was always NULL for newly-allocated dm anyway).

   **After Step 7.11**: `struct mged_dm` no longer has `dm_tie`.  The command-window
   tie is tracked exclusively in `mged_pane::mp_cmd_tie`.

   **Step 7.12** ✅ (Session 24) — Remove `dm_p_vlist` from `struct mged_dm`:
   - `mged_dm.h`: `dm_p_vlist` field deleted; comment updated.  `mp_p_vlist` in
     `mged_pane` is now the only predictor vlist location.  Comment on `mp_p_vlist`
     in `mged_pane` struct updated.  Other stale `mged_curr_dm` comment references
     in `mged_dm.h` cleaned up.  DMP macro comment block updated.
   - `attach.c` `mged_attach()`: `BU_LIST_INIT(&ndm->dm_p_vlist)` replaced with
     comment (field removed).
   - `attach.c` `release()`: `BSG_FREE_VLIST(&cdm->dm_p_vlist)` replaced with
     comment (list was always empty — wrapper pane's predictor data lives in
     `mp_p_vlist`).
   - `mged.c` startup: `BU_LIST_INIT(&mged_dm_init_state->dm_p_vlist)` replaced
     with comment.
   - `mged.c` `mged_finish()`: `BSG_FREE_VLIST(&p->dm_p_vlist)` replaced with
     comment.

   **After Step 7.12**: `struct mged_dm` no longer has `dm_p_vlist`.  Predictor
   vlists live exclusively in `mged_pane::mp_p_vlist`.

   **Step 7.13** ✅ (Session 24) — Remove dead `mged_dm` fields and dead functions:
   - `mged_dm.h`: Removed `dm_ndrawn`, `dm_trails[NUM_TRAILS]` (completely unused in
     code), and all Tcl display variable name VLS fields (`dm_fps_name`, `dm_aet_name`,
     `dm_ang_name`, `dm_center_name`, `dm_size_name`, `dm_adc_name`) — these were only
     ever written (by `mged_link_vars`), never read.  All HUD variable names now live
     exclusively in `mged_pane::mp_fps_name` etc.
   - `attach.c`: Removed functions `mged_slider_init_vls()`, `mged_slider_free_vls()`,
     and `mged_link_vars()` (all dead).  Removed call sites:
     `mged_link_vars(ndm)` in `mged_attach()`, `mged_slider_free_vls(cdm)` in
     `release()`.
   - `mged.c`: Removed `mged_slider_free_vls(p)` in `mged_finish()` and
     `mged_link_vars(mged_dm_init_state)` in startup.
   - `mged.h`: Removed `mged_link_vars` and `mged_slider_free_vls` declarations.

   **After Step 7.13**: `struct mged_dm` no longer has any trail, ndrawn, or VLS name
   fields.  HUD variable names are populated by `mged_pane_link_vars()` exclusively.

   **Step 7.14** ✅ (Session 24) — Remove hook function pointers from `struct mged_dm`:
   - `mged_dm.h`: Removed `dm_cmd_hook`, `dm_viewpoint_hook`, `dm_eventHandler` from
     `struct mged_dm`.  Removed corresponding macros (`cmd_hook`, `viewpoint_hook`,
     `eventHandler`).
   - `attach.c` `mged_dm_init()`: Removed `ndm->dm_cmd_hook = dm_commands` (field gone).
   - `attach.c` `dm_cmd()`: Replaced `if (!cmd_hook) { ... } return cmd_hook(...)` with
     direct `return dm_commands(...)` call (dm_cmd_hook was always dm_commands).
   - `mged.c` startup: Removed `mged_dm_init_state->dm_cmd_hook = dm_commands`.
   - `mged.c` `refresh()`: Removed `if (viewpoint_hook) (*viewpoint_hook)()` (VR hook;
     `viewpoint_hook` was never assigned a non-NULL value — always NULL/dead).

   **After Step 7.14**: `struct mged_dm` no longer has any function pointer fields.
   The `dm` command calls `dm_commands()` directly.  The VR viewpoint hook is gone.

### Step 7.15 — Move scalar/array pane state fields from `mged_dm` to `mged_pane`

   Removed from `mged_dm`: `dm_owner`, `dm_am_mode`, `dm_perspective_angle`,
   `dm_zclip_ptr`, `dm_adc_auto`, `dm_grid_auto_size`, `dm_mouse_dx/dy`, `dm_omx/omy`,
   `dm_knobs[8]`, `dm_work_pt`, `dm_scroll_top/active/y`, `dm_scroll_array[6]`.
   All moved to `mged_pane` as `mp_*` equivalents.
   `dm_var_init()` no longer sets these; `mged_pane_init_resources()` does.
   Updated macros: `am_mode`, `perspective_angle`, `owner`, `adc_auto`, `grid_auto_size`,
   `mouse_dx/dy`, `omx/omy`, `knobs`, `work_pt`, `scroll_*` all go through `mged_curr_pane`.

### Step 7.16 — Transfer ownership of 8 non-view shareable resources from `mged_dm` to `mged_pane`

   Removed from `mged_dm`: `dm_adc_state`, `dm_menu_state`, `dm_rubber_band`,
   `dm_mged_variables`, `dm_color_scheme`, `dm_grid_state`, `dm_axes_state`,
   `dm_dlist_state`.
   Added to `mged_dm`: `dm_pane` back-pointer to the owning `mged_pane`.
   The 8 resources are now allocated in `mged_pane_init_resources()` (wrapper path)
   and freed in `mged_pane_free_resources()` with ref counting.
   Pane creation in `mged_attach()` was moved BEFORE `mged_dm_init()` so that
   `ndm->dm_pane->mp_mged_variables` is available for `dm_set_perspective()`.
   `dm_var_init()` no longer allocates any resources (view_state was still there).
   `share.c`: `SHARE_RESOURCE` macro updated to use `dlp->dm_pane->mp_*`.
   `free_all_resources`/`usurp_all_resources` rewritten to operate via pane pointers.
   `set.c`: `mp->mp_dm->dm_dlist_state` → `mp->mp_dlist_state`.

### Step 7.17 — Move `dm_view_state` from `mged_dm` to `mged_pane`; `mged_dm` has NO resource fields

   Removed from `mged_dm`: `dm_view_state`.
   `struct mged_dm` now contains ONLY: `dm_dmp`, `dm_fbp`, `dm_netfd`, `dm_netchan`,
   `dm_clients[]`, `dm_dirty`, `dm_mapped`, and `dm_pane` back-pointer.
   All 9 shareable resources live in `mged_pane::mp_*`.
   `view_ring_destroy()` signature changed to take `struct _view_state *vsp` directly
   (removes the last `struct mged_dm *` dependency from chgview.c helpers).
   `dm_var_init()` now allocates `npane->mp_view_state` (not `ndm->dm_view_state`).
   `mged.c` startup: all resource allocations removed from `mged_dm_init_state`;
   `mged_pane_init_resources()` handles everything for the sentinel init_pane.
   `share.c`: `SHARE_RESOURCE_DM` macro deleted; view_state sharing uses the same
   `SHARE_RESOURCE` path via `dm_pane->mp_view_state`.

### Step 7.18 — Flatten `mged_dm` into `mged_pane`; delete `struct mged_dm` ✅ (Session 25)

   All remaining `mged_dm` fields (`dm_dmp`, `dm_fbp`, `dm_netfd`, `dm_netchan`,
   `dm_clients`, `dm_dirty`, `dm_mapped`) moved to `mged_pane` as `mp_dmp`, `mp_fbp`,
   `mp_netfd`, `mp_netchan`, `mp_clients`, `mp_dirty`, `mp_mapped`.

   - `struct mged_dm` deleted from `mged_dm.h`.
   - `mp_dm` field removed from `mged_pane`; Obol-vs-dm check is now `mp_dmp != NULL`.
   - `mged_dm_init_state` global eliminated; startup sentinel is `s->mged_init_pane`.
   - `DMP` macro: `s->mged_curr_pane->mp_dmp` (no more `mp_dm ? mp_dm->dm_dmp : NULL`).
   - `DMP_dirty`, `fbp`, `clients`, `mapped` macros go directly through `mged_curr_pane->mp_*`.
   - `dm_var_init()` / `mged_dm_init()` take `mged_pane *` instead of `mged_dm *`.
   - `mged_attach()`: no `ndm` allocation; pane is created directly and passed to `mged_dm_init`.
   - `release()` signature: `struct mged_pane *bad_pane` replaces `struct mged_dm *bad_dm`.
   - `usurp_all_resources()` / `free_all_resources()` / `share_dlist()`: take `mged_pane *`.
   - `SHARE_RESOURCE` macro: `dlp->mp_*` directly (no more `dlp->dm_pane->mp_*`).
   - `fbserv.c`: `cdm` is now `struct mged_pane *`; `mp_netfd`/`mp_fbp`/`mp_clients` direct.
   - `mged_pane_init_resources()` / `mged_pane_free_resources()`: unified single path.
   - All 27 mged .c files compile cleanly with -Werror.

   **After Step 7.18**: `struct mged_dm` no longer exists.  All pane state is in
   `mged_pane`.  The `mp_dmp != NULL` test distinguishes dm panes from Obol panes.

### Step 7.19 — Remove libdm attach path; delete dm-generic.c; eliminate dm render loop ✅ (Session 26)

   - `f_attach()` / `mged_attach()` / `mged_dm_init()`: removed entirely; Obol-only.
   - `dm-generic.c` deleted (all libdm glue removed from CMakeLists.txt).
   - `refresh()` dm render loop removed; only `obol_notify_views()` remains.
   - `drain_background_geom()` calls `do_view_changed(QG_VIEW_DRAWN)` on AABB/OBB/LoD progress.
   - `mged_attach()` is now empty (no libdm path); only `f_new_obol_view_ptr` is active.

### Step 7.20 — Delete all libdm fields from `mged_pane`; 27 mged files clean ✅ (Session 26)

   - `mp_dmp`, `mp_fbp`, `mp_netfd`, `mp_netchan`, `mp_clients`, `mp_dirty`, `mp_mapped`,
     `mp_owner` fields deleted from `mged_pane`.
   - `DMP`, `DMP_dirty`, `fbp`, `clients`, `mapped`, `owner` macros deleted from `mged_dm.h`.
   - `GET_MGED_DM` / `GET_MGED_PANE` macros simplified to always return `MGED_PANE_NULL`.
   - All drawing functions that called `dm_set_fg`, `dm_draw_*`, `dm_set_line_attr`, etc.
     are now stubs (`/* no-op */`) or have their DMP blocks removed:
     - `dotitles()`, `screen_vls()` → no-op stubs
     - `draw_rect()`, `paint_rect_area()`, `rt_rect_area()` → no-op stubs
     - `scroll_display()`, `mmenu_display()`, `mged_highlight_menu_item()` → no-op stubs
     - `predictor_frame()` → no-op stub
     - `draw_grid()`, `draw_adc_lines()`, `draw_axes()` → already guarded (were already no-ops)
   - `fbserv.c`: completely rewritten as a single `fbserv_set_port` no-op stub.
   - `share.c`: `'d'` case (dm_share_dlist) removed; `share_dlist()` is a no-op stub.
   - 27 mged .c files compile cleanly with -Werror and 0 warnings.

   **After Step 7.20**: `mged_pane` is fully libdm-free.  MGED is Obol-only.
   `DMP` is gone; all drawing goes through the Obol path.

   **Remaining work (Stage 8)**:
   - Remove `libdm` from CMakeLists link dependencies for `mged`
   - Remove libdm header includes from mged source files
   - Delete `src/libdm/` (except fb_* raster helpers for rtwizard)

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
