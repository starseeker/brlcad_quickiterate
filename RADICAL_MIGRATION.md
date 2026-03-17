# BRL-CAD → Obol Migration Status

## Goal

Replace BRL-CAD's immediate-mode `libdm`/`libbsg` drawing stack with the
**Obol** scene-graph library (our Coin3D/Open Inventor fork).  All user-facing
functionality in MGED, Archer, rtwizard, and qged must be preserved.
Implementation improvements and bug-fixes are welcome; sub-optimal legacy
behavior does not need to be preserved.

The migration is largely complete for the Obol build path (`BRLCAD_ENABLE_OBOL`).
What remains is deleting the now-dead code and finishing rtwizard.

---

## Architecture Summary

### Libraries

| Library   | Status in Obol build                                  |
|-----------|-------------------------------------------------------|
| `libdm`   | Still built; core fb_* helpers kept; rendering plugins skipped or deleted |
| `libbsg`  | Still built; **not yet deleted** — pending cleanup    |
| `librt`   | Unchanged; `ft_scene_obj` produces Obol `SoNode*`     |
| `libged`  | libdm removed from core deps; draw pipeline feeds Obol|
| `libObol` | Authoritative scene graph + renderer                  |

### Rendering pipeline (Obol path)

```
libged draw command
  └─ draw_gather_paths()    walk comb tree, build bsg_shape list
       └─ draw_scene()      dispatch per-shape; ft_scene_obj → SoNode*
            └─ obol_scene_assemble()
                 walks bsg_shape tree → builds root SoSeparator hierarchy
```

The `SoGLRenderAction` traversal in `QgObolView` / `obol_view` widget handles
all actual GL rendering.  No `dm_draw_vlist()` calls remain in the hot path.

### qged rendering

```
QgEdApp ─── QgEdMainWindow ─── QgObolView (QOpenGLWidget)
                                    └─ SoViewport + SoRenderManager
                                         └─ SoGLRenderAction
```

### mged / archer rendering

```
Tk app ─── obol_view Tk widget (libtclcad/obol_view.cpp)
               └─ SoDB + SoGLRenderAction (via GLX/WGL/NSOpenGL or OSMesa)
```

Each pane is a `mged_pane` with `mp_dmp == NULL` (no dm handle).
`active_pane_set` is the sole pane registry; `active_dm_set` is gone.
The macros `view_state`, `adc_state`, etc. resolve through `mged_curr_pane->mp_*`.

---

## What Is Done

### Rendering (Stages 0–6) — Complete

- **Obol in CMake**: `find_package(Obol)`, `BRLCAD_ENABLE_OBOL` option,
  `Obol::Obol` imported target, `bsg_shape::s_obol_node`, `obol_node.h`.
- **Primitive nodes** (`ft_scene_obj`): all primitives produce Obol `SoNode*`
  via `rt_generic_scene_obj`, `rt_bot_scene_obj`, `rt_brep_scene_obj`.
- **Scene hierarchy**: comb tree → `SoSeparator` tree with `SoTransform`,
  `SoMaterial`, `SoDrawStyle` per leaf; instancing via shared `SoNode*`.
- **Drawing modes** (0–5): per-object `SoDrawStyle` + global `SoRenderManager`
  render mode; context-menu in `QgObolView` exposes all modes.
- **Camera**: bidirectional sync between `bsg_view` and `SoPerspectiveCamera`.
  `viewAll()` uses `SoGetBoundingBoxAction`; interactive navigation calls
  `syncBsgViewFromCamera()` so `ae`/`center`/`zoom` commands stay consistent.
- **Selection**: `SoRayPickAction` on Ctrl+left-click; selection highlight via
  emissive orange `SoMaterial`; `picked(bsg_shape*)` Qt signal.
- **qged**: `QgObolView` is the primary 3D widget; `QgEdApp` initialises SoDB;
  `QgEdMainWindow` creates `QgObolView` instead of `QgQuadView`.
- **mged**: `obol_view` Tk widget per pane; independent per-pane `bsg_view`;
  `obol_notify_views` Tcl command wakes all widgets on scene change.
- **archer**: `cadwidgets::Ged` uses `to_new_view` with `"obol"` type;
  `Ged.tcl` detects Obol and sets `dmType obol` before the four pane creates.
- **rtwizard**: `obol_init` called at GUI startup (**GUI mode done**); full
  headless/offscreen rendering via `SoOffscreenRenderer` + OSMesa is **not yet
  done** — see "What Remains" item 1.

### MGED internal refactoring — Complete

`struct mged_dm` and `active_dm_set` are fully removed.  Key milestones:

- `struct mged_pane` is the sole pane type; `mp_dmp == NULL` for Obol panes.
- `active_pane_set` (`bu_ptbl`) is the sole pane registry.
- `DMP` macro and all `dm_*` libdm macros deleted from `mged_dm.h`.
- All MGED drawing functions (`dotitles`, `scroll_display`, `draw_rect`, etc.)
  are no-op stubs; libdm removed from `mged/CMakeLists.txt`.
- `fbserv.c` is a single `fbserv_set_port` no-op stub.
- `mged_curr_pane` is always non-NULL after startup (sentinel `mged_init_pane`).
- `set_curr_dm()` deleted; all callers use `set_curr_pane()`.
- `dm-generic.c` deleted from disk.

### libdm removal from library deps — Complete for Obol path

| Library     | libdm dep in Obol builds        |
|-------------|----------------------------------|
| `mged`      | Removed                          |
| `libtclcad` | Removed from `libtclcad_deps`    |
| `libged`    | Removed from `libged_deps` core  |
| `libqtcad`  | Removed (`QgSW`/`QgGL` excluded) |
| `qged`      | Removed (Stage 20)               |
| `rtwizard`  | Removed (Stage 21)               |

### Key libtclcad changes — Complete

- **`dm.c`**: completely stubbed — `dm_open` creates a `dm_obj` with
  `dmo_dmp = NULL`; all 30+ subcommands return `TCL_OK`.  No `dm_*` calls.
- **`view/draw.c`** / **`view/refresh.c`**: Obol-only stubs; `go_refresh()`
  calls `obol_notify_views` for Obol views.
- **`commands.c`**: `to_bg`/`to_light`/`to_zbuffer`/`to_transparency`/`to_fontsize`
  read/write `tclcad_view_data::gdv_*` fields + trigger `obol_notify_views`;
  `to_pix`/`to_png` delegate to `obol_view_screengrab`; `to_is_viewable` returns 1
  for all views.
- **`obol_view.cpp`**: `obol_view_apply_render_settings()` reads `gdv_bg`/`gdv_light`
  each frame; `obol_view_screengrab_impl()` uses `SoOffscreenRenderer` for png/pix.
- **`tclcad/draw.h`**: `tclcad_view_data` gains `gdv_bg[3]`, `gdv_light`,
  `gdv_zbuffer`, `gdv_transparency`, `gdv_fontsize`.

### qged embedded raytracing (ert) — Complete

- `ert.cpp` Obol path allocates `fbs_pixbuf`/`fbs_pixbuf_w`/`fbs_pixbuf_h`
  on `fbserv_obj` instead of opening `/dev/mem`.
- `QgObolView::_paintFbOverlay()` reads `fbs_->fbs_pixbuf` directly; no
  `fb_readrect`/`fb_getwidth`/`fb_getheight` calls remain in qged.
- `fbserv.cpp` Obol path: `obol_fbs_pkg_switch()` table +
  `qdm_obol_new_client()` handle the fbserv protocol natively.

### dm plugin gating — Complete (Stage 21)

- `dm-qtgl` plugin: **not built** when `BRLCAD_ENABLE_OBOL`.
- `dm-ogl` (glx) plugin: **not built** when `BRLCAD_ENABLE_OBOL`.
- `dm-X` (X11) plugin: **not built** when `BRLCAD_ENABLE_OBOL`.
- `dm-wgl` (WGL/Windows) plugin: **not built** when `BRLCAD_ENABLE_OBOL`.
- `dm-swrast` plugin: still built (headless/test use); `swrastwin.cpp` and
  `libqtcad` link skipped when Obol.
- `QgSW.cpp` / `QgGL.cpp`: excluded from `libqtcad` build when Obol.

---

## What Remains

### 1. rtwizard — Obol offscreen rendering (headless pipeline)

`rtwizard` CMakeLists no longer links `libdm` in Obol builds (Stage 21).
The GUI mode already calls `obol_init` at startup.  What remains is the
headless pixel-compositing pipeline:

- Replace the `fbserv`-subprocess + `rt` pipeline used in `PictureType*.itcl`
  with an `SoOffscreenRenderer` + `CoinOSMesaContextManager` path for headless
  rendering.
- Implement a `rtwizard_obol_render` Tcl command (or extend `obol_view_screengrab`)
  that composites multiple rt passes using `SoTexture2` overlay nodes and writes
  the final image directly without needing an external `fbserv` process.
- The `rtwizard` Tcl script already guards `obol_init` with
  `[info commands obol_init] ne ""`; the rendering dispatch in each
  `PictureType*.itcl` needs an analogous Obol branch.

### 2. Delete libdm rendering plugins (CMake gating complete — file deletion pending)

All rendering plugins are now **skipped in CMake** when `BRLCAD_ENABLE_OBOL`
(Stage 21 completed dm-X and dm-wgl; earlier stages did qtgl/glx/swrast).
The source directories remain on disk; a future stage will delete them.

CMake gating status:

| Directory            | CMake guard when Obol ON    | Replacement                  |
|----------------------|-----------------------------|------------------------------|
| `src/libdm/qtgl/`    | ✅ `NOT BRLCAD_ENABLE_OBOL` | `QgObolView`                 |
| `src/libdm/glx/`     | ✅ `NOT BRLCAD_ENABLE_OBOL` | `obol_view` HW GLX path      |
| `src/libdm/wgl/`     | ✅ `NOT BRLCAD_ENABLE_OBOL` | `obol_view` HW WGL path      |
| `src/libdm/X/`       | ✅ `NOT BRLCAD_ENABLE_OBOL` | `obol_view` (Tk/X handled)   |
| `src/libdm/swrast/`  | ⚠️ Still built (no swrastwin) | OSMesa `obol_view` SW path  |
| `src/libdm/null/`    | n/a — no GL, always built   | No replacement needed        |
| `src/libdm/plot/`    | n/a — no GL, always built   | No replacement needed        |
| `src/libdm/postscript/` | n/a — no GL, always built | No replacement needed       |
| `src/libdm/txt/`     | n/a — no GL, always built   | No replacement needed        |

Remaining steps for a clean delete:
1. Decide whether to also skip `dm-swrast` in Obol builds (it is used for
   headless tests; skip only after OSMesa `obol_view` path is verified).
2. Remove the now-dead source directories from disk (git rm) and update
   `cmakefiles()` lists.
3. Delete `libdmgl` (`dm-gl.c`, `dm-gl_lod.cpp`) once no plugin uses it in
   Obol builds.
4. Slim core `libdm` to fb_* helpers only (`fb_generic.c`, `fb_log.c`,
   `fb_paged_io.c`, `fb_rect.c`, `fb_util.c`, `fbserv.c`, `if_disk.c`,
   `if_mem.c`, `if_remote.c`, `if_stack.c`), or fold those into a new
   `libfb` if fully separating from the `dm` namespace is desired.

### 3. Delete `src/libbsg/` and `include/bsg/`

`libbsg` contains: `scene_graph.cpp`, `lod.cpp`, `knobs.cpp`, `polygon*.cpp`,
`hash.c`, `util.cpp`, `view_sets.cpp`, `vlist.c`, etc.

Steps:
1. Verify no Obol-path callers remain (most bsg_* types are thin wrappers in
   `include/bsg/` that alias `bv_*`/`bview`/`bsg_view` etc.).
2. Move any genuinely needed helpers (e.g. vlist utilities) to `librt` or
   `libbn`.
3. Delete `src/libbsg/` tree; remove from `source_dirs.cmake`.
4. Delete `include/bsg/` (or leave thin migration aliases for external callers).

### 4. Delete / slim `include/dm.h` and `include/dm/`

`include/dm.h` is a large catch-all header.  The four minimal headers in
`include/dm/` are the target end state:

| Header            | Keep?  | Contents                              |
|-------------------|--------|---------------------------------------|
| `dm/defines.h`    | Yes    | `struct fb`, minimal fb_* declarations|
| `dm/fbserv.h`     | Yes    | `struct fbserv_obj`, fbs_pixbuf fields|
| `dm/util.h`       | Yes    | `fb_common_*`, `fb_sim_*`             |
| `dm/view.h`       | Yes    | `struct dm_view_data`                 |

Once fb_* helpers are no longer needed (i.e. the in-memory `fbs_pixbuf` path
covers all use), even these can be deleted and `struct fbserv_obj` can move to
`include/ged/defines.h` or `include/tclcad/draw.h`.

### 5. Remove non-Obol fallback path

`include/qtcad/QgView.h`, `QgSW.h`, `QgGL.h` still include `dm.h` in non-Obol
builds (guarded with `#ifndef BRLCAD_ENABLE_OBOL`).  Once the decision is made
to drop non-Obol builds entirely:

- Delete `src/libqtcad/QgSW.cpp`, `src/libqtcad/QgGL.cpp`.
- Delete `include/qtcad/QgSW.h`, `include/qtcad/QgGL.h`.
- Remove all `#ifndef BRLCAD_ENABLE_OBOL` guards that protect libdm code.
- Remove `QgQuadView` (the libdm-backed 4-pane widget) or reduce it to a
  thin wrapper around `QgObolView`.

### 6. Tidy libtclcad dm.c stub

`src/libtclcad/dm.c` is a complete stub (`dm_open` creates a `dm_obj` with
`dmo_dmp = NULL`; all subcommands no-op).  The `dm_open` Tcl command is still
registered for backward compatibility with legacy scripts.  Once archer and
mged scripts no longer call `dm_open` directly, this file and its Tcl command
registrations can be deleted.

### 7. Stubbed functionality — post-migration check list

The following features were **working** in the libdm path but are currently
**no-op stubs** in the Obol path.  Each item must be re-examined after
libdm/libbsg deletion and implemented using the Obol/Coin3D mechanism
described.  See `MGED_TODO.md` for day-to-day tracking; this list captures
the user-visible scope.

#### 7a. 2D overlay rendering (mged/archer)

All of the items below previously called `dm_draw_line_2d`, `dm_draw_string_2d`,
`dm_set_line_attr`, and related libdm 2D drawing APIs.  Their source files now
contain no-op stubs tagged **Step 7.20**.  The Obol replacement is a
screen-space `SoAnnotation` subtree rendered after the 3D scene; each overlay
can share a single `SoOrthographicCamera` and use `SoLineSet`/`SoText2` nodes.

| Feature | Source file | Former user-visible result | Obol approach |
|---------|-------------|---------------------------|---------------|
| **ADC (Angle/Distance Cursor)** | `mged/adc.c` → `adcursor()` | Two cursor lines with angle + distance readout drawn over the 3D view | `SoAnnotation` + `SoLineSet` (two lines) + `SoText2` label nodes; position/angles driven by `adc_state` |
| **Edit axes indicator** | `mged/axes.c` → `draw_e_axes()` | Colored XYZ arrows at the current edit origin | `SoAnnotation` + per-axis `SoLineSet`/`SoCone` in a screen-corner or at-edit-point camera-relative transform |
| **Model axes indicator** | `mged/axes.c` → `draw_m_axes()` | Colored XYZ arrows at the model origin | Same `SoAnnotation` mechanism; size/position driven by `axes_state` |
| **View axes indicator** | `mged/axes.c` → `draw_v_axes()` | Colored XYZ arrows in a fixed screen corner | Small fixed-size `SoAnnotation` corner inset; common in Coin3D viewer examples |
| **Grid overlay** | `mged/grid.c` → `draw_grid()` | Evenly-spaced grid lines on the model plane | `SoCoordinate3` + `SoLineSet` grid computed from `grid_state`; toggle with `SoSwitch` |
| **Rubber-band rectangle** | `mged/rect.c` → `draw_rect()` | Draggable selection rectangle drawn over the view | `SoAnnotation` + `SoLineSet` (4 edges) updated each mouse-move event |
| **Rubber-band FB paint** | `mged/rect.c` → `paint_rect_area()` | Pixel region of the framebuffer painted as a rectangle | Replace with `SoTexture2`-based overlay fed from `fbs_pixbuf`, or simply remove (superseded by full `ert` fbserv overlay) |
| **Rubber-band raytrace** | `mged/rect.c` → `rt_rect_area()` | `rt` subprocess launched on the rubber-band region; result composited into the view | Port the `fbs_pixbuf` approach from `qged/ert.cpp` to mged's Obol pane; launch `rt` with `--rect` args |
| **Predictor frame** | `mged/predictor.c` → `predictor_frame()` | Ghost wireframe view-frustum preview for velocity navigation | `SoLineSet` under an `SoMatrixTransform` updated from `predictor_state`; toggle with `SoSwitch` |
| **On-screen scroll sliders** | `mged/scroll.c` → `scroll_display()` | Interactive translation/rotation sliders drawn in the view area | Replace with Tk `Scale` widgets docked below each `obol_view` pane; slider callbacks already update `bsg_view` via `knob` dispatch |
| **In-view button menus** | `mged/menu.c` → `mmenu_display()`, `mged_highlight_menu_item()` | Pop-up function menus drawn directly inside the 3D view | Replace with Tk popup menus (`tk_popup`) or a dedicated Tk frame; the `mmenu_select()` / `mmenu_parms()` selection logic is intact |
| **HUD title text** | `mged/titles.c` → `screen_vls()`, `dotitles()` | Model name, editing state, view size, AET drawn as text in the view | `obol_update_title_vars()` already writes `$aet`, `$center`, `$size` Tcl vars; the Tk label display (`titles.tcl`) must be wired to these vars and to `obol_notify_views`; actual in-view text can use `SoText2` under `SoAnnotation` if desired |

#### 7b. MGED framebuffer server (mged fbserv)

`mged/fbserv.c` → `fbserv_set_port()` is a no-op.

- **Former behavior**: MGED opened an in-process libdm framebuffer (`/dev/wl`
  or `fbserv`), accepted `rt` output via pkg protocol, and composited the pixel
  data onto the display manager surface (`mp_fbp` overlay).
- **Current state**: `mged_pane` has no `mp_fbp` or `mp_netfd`; all removed in
  Step 7.20.  The `mv_listen` / `mv_port` mged variables exist but the hook
  (`fbserv_set_port`) does nothing.
- **Obol approach**: Port the `fbs_pixbuf` + `qdm_obol_new_client` mechanism
  from `qged/fbserv.cpp` to the mged Tcl/`obol_view` path.  The `obol_view`
  widget already has an `_paintFbOverlay` hook in `QgObolView`; a parallel
  `obol_view_paint_fb_overlay()` should be added to `libtclcad/obol_view.cpp`.
  The `fbs_pixbuf` buffer on `fbserv_obj` is then uploaded as a `SoTexture2`
  overlay node on each frame.

#### 7c. OpenGL display lists (mged/dozoom.c)

`dozoom()`, `createDListSolid()`, `createDListAll()`, `freeDListsAll()` are
all no-op stubs (Step 7.20).

- **Former behavior**: Every solid's vlist was compiled into a GL display
  list; `dozoom` iterated the list and called `glCallList` for each visible
  shape, implementing view-frustum culling with the libdm matrix stack.
- **Current state**: Obol's `SoGLRenderAction` handles all GL calls; the
  per-shape `SoNode*` stored in `bsg_shape::s_obol_node` is the replacement.
- **Action**: Verify that the Coin3D caching (`SoSeparator::renderCaching =
  ON`) provides equivalent per-shape cache performance.  If profiling shows a
  bottleneck, evaluate `SoVBO` or `SoVertexBuffer` caching for heavy BoT meshes.

#### 7d. X11 / libdm event dispatch (mged/doevent.c)

`doEvent()` (the X11 `XAnyEvent` handler) was removed in Stage 8; mged now
relies entirely on Obol/Qt event routing through the `obol_view` Tk widget.

- **Check**: Confirm that all keyboard shortcuts (`f`, `R`, `c`, etc.) and
  mouse button bindings that previously flowed through `doEvent` are correctly
  bound in the Obol Tk widget event table (`obol_view.cpp`
  `bind_obol_view_events()`).  Specifically verify: left/middle/right mouse
  drag for rotate/translate/zoom; `<ButtonRelease>` to terminate rubber-band
  rectangle; `<KeyPress>` for single-key shortcuts.

#### 7e. Display-list sharing between panes (mged/share.c)

`share_dlist()` is a no-op; the `'d'`/`'D'` branch of `f_share()` is silently
skipped.

- **Former behavior**: Two panes could share a single GL display-list context
  so that compiled geometry was not duplicated in GPU memory.
- **Current state**: Each `mged_pane` / `obol_view` widget has its own
  `SoSceneManager`; geometry nodes (`SoNode*`) are ref-counted and shared via
  the `bsg_shape::s_obol_node` pointer, providing an equivalent benefit at the
  scene-graph level.
- **Action**: Document (or assert) that `SoNode` ref-counting gives the same
  single-upload guarantee; remove the dead `share_dlist` stub once confirmed.

#### 7f. `to_fb_rect` Tcl command (libtclcad/commands.c)

`to_fb_rect()` returns `BRLCAD_OK` immediately in the Obol path.

- **Former behavior**: Painted a rectangular region of the framebuffer
  surface in the view.
- **Obol approach**: If this is still needed (e.g. by archer scripts), route
  it through the same `fbs_pixbuf` overlay described in §7b above.  If no
  callers remain after removing libdm, delete the command registration.

#### 7g. `go_draw()` in libtclcad (libtclcad/view/draw.c)

`go_draw()` is a one-line no-op returning immediately.

- **Former behavior**: Called `dm_draw_vlist` on every display-list entry to
  repaint the Tk window synchronously.
- **Obol approach**: Already replaced by `obol_notify_views`; all callers of
  `go_draw` should be audited to confirm they now call (or indirectly trigger)
  `obol_notify_views` instead.

---

## Key Files Reference

### Active Obol source files

| File | Role |
|------|------|
| `src/qged/QgObolView.h` | Qt/Obol 3D widget; fbserv pixel overlay |
| `src/qged/fbserv.cpp` | Obol-native fbserv packet handlers |
| `src/libtclcad/obol_view.cpp` | Tk/Obol widget; render settings; screengrab |
| `src/libtclcad/commands.c` | to_bg/light/zbuffer/… → gdv_* fields |
| `src/libged/draw/draw.cpp` | draw pipeline; obol_scene_assemble() |
| `src/libged/dm/ert.cpp` | embedded rt; fbs_pixbuf allocation |
| `src/mged/attach.c` | mged_pane lifecycle; obol pane create/release |
| `src/mged/mged_dm.h` | mged_pane struct; active_pane_set |

### Key data structures

| Struct | Location | Role |
|--------|----------|------|
| `bsg_shape` | `include/bsg/defines.h` | per-object: `s_obol_node`, `s_mat`, AABB, async state |
| `bsg_view` | `include/bsg/defines.h` | camera + view state; synced to/from `SoCamera` |
| `mged_pane` | `src/mged/mged_dm.h` | sole pane abstraction in mged; `mp_dmp == NULL` for Obol |
| `tclcad_view_data` | `include/tclcad/draw.h` | per-view Tcl settings: `gdv_bg`, `gdv_light`, … |
| `fbserv_obj` | `include/dm/fbserv.h` | `fbs_pixbuf`/`w`/`h` for Obol rt pixel buffer |

### Key Tcl commands (libtclcad)

| Command | Implementation |
|---------|----------------|
| `obol_init` | Initialise SoDB; must be called before any `obol_view` widget |
| `obol_view <path>` | Create Obol Tk widget; `attach <gvp_ptr>` binds it to a `bsg_view` |
| `obol_notify_views` | Re-assemble + repaint all registered `obol_view` instances |
| `obol_view_screengrab <view> png|pix <file>` | Offscreen screenshot via `SoOffscreenRenderer` |
| `$ged bg $v R G B` | → `gdv_bg[3]` + `obol_notify_views` |
| `$ged light $v 0\|1` | → `gdv_light` + `obol_notify_views` |
| `$ged pix $v file.pix` | → `obol_view_screengrab $v pix file.pix` |
| `$ged png $v file.png` | → `obol_view_screengrab $v png file.png` |

---

## Acceptance Criteria

The migration is complete when all of the following are true:

1. `qged` renders via `SoGLRenderAction`; zero libdm symbols linked.
2. All drawing modes (0–5) work via `SoRenderManager` + per-object `SoDrawStyle`.
3. `mged` renders all 4 panes via `obol_view` Tk widgets.
4. `rtwizard` uses `SoOffscreenRenderer` + OSMesa for headless rendering.
5. Camera navigation (`ae`, `center`, `zoom`, `rot`, mouse) works correctly.
6. Object selection and highlighting work via `SoRayPickAction`.
7. All libged draw commands (`draw`, `erase`, `e`, `B`, `who`) work correctly.
8. All existing regression tests pass.
9. `src/libdm/` (rendering plugins + core GL) and `src/libbsg/` are deleted.
10. `include/dm.h`, `include/dm/` (except minimal fb helpers if still needed),
    and `include/bsg/` are deleted or reduced to thin migration aliases.
