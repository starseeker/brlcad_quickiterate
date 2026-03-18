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

### dm plugin gating — Complete (Stage 22)

All GL rendering plugins are now **not built** when `BRLCAD_ENABLE_OBOL`.

- `dm-qtgl` plugin: **not built** when `BRLCAD_ENABLE_OBOL`.
- `dm-ogl` (glx) plugin: **not built** when `BRLCAD_ENABLE_OBOL`.
- `dm-X` (X11) plugin: **not built** when `BRLCAD_ENABLE_OBOL`.
- `dm-wgl` (WGL/Windows) plugin: **not built** when `BRLCAD_ENABLE_OBOL`.
- `dm-swrast` plugin: **not built** when `BRLCAD_ENABLE_OBOL` (Stage 22).
  Obol uses `QgObolSwrastView` (Coin3D/OSMesa) for headless/swrast rendering.
- `QgSW.cpp` / `QgGL.cpp`: excluded from `libqtcad` build when Obol.

### libdm draw test gating — Complete (Stage 22)

All tests in `src/libged/tests/draw/` use `dm_open("swrast")` (libdm) and are
guarded with `if(NOT BRLCAD_ENABLE_OBOL)`.  The Obol draw-pipeline equivalents
are `qged_test` and `qged_pipeline_test` (use `QgObolSwrastView`).

---

## What Remains

### 1. rtwizard — Obol-backed fbserv pipeline — Complete (Stage 25)

**rtwizard now routes all rendering through the TCP fbserv path in both Obol and
non-Obol builds.**  The Obol fbserv (Stage 24) uses the libdm-free `pixbuf_server`
backend; the traditional non-Obol fbserv uses the `/dev/mem` libdm driver.  The PKG
wire protocol is identical in both cases so `rt`, `rtedge`, `fblabel`, `fbclear`,
`fb-png`, and `fb-pix` work unchanged against either backend.

Changes made (Stage 25):

- **`src/tclscripts/rtwizard/rtwizard`**: The Stage-23 Obol-specific `rtimage_file`
  branch is removed.  The headless `else` block now uses a single fbserv path for
  both Obol and non-Obol builds.  The only difference is the device string: Obol
  builds use `"obol_pixbuf"` (accepted by the Stage-24 pixbuf fbserv); non-Obol
  builds use `"/dev/mem"`.  The kill-fbserv guard now covers both device strings.

- **`src/fb/fb-png.c`**: Obol path speaks the fbserv PKG wire protocol directly
  via libpkg (MSG_FBOPEN → read rows via MSG_FBREADRECT → MSG_FBCLOSE).  No libdm
  symbols used.  Non-Obol path (inside `#else`) is unchanged.

- **`src/fb/CMakeLists.txt`**: `fb-png` Obol branch links
  `libbu + libpkg + ${PNG_LIBRARIES}` only (no libdm, no dm_plugins).
  Added `brlcad_find_package(PNG REQUIRED)` at the top so `${PNG_LIBRARIES}` is
  defined in this file.

- **`src/util/pix-png.c`** / **`src/util/CMakeLists.txt`**: Obol branch guards
  both `fb_common_file_size()` calls with `#ifndef BRLCAD_ENABLE_OBOL`; the autosize
  fallback uses a plain `stat()` call instead.  CMakeLists Obol branch links
  `libbu + ${PNG_LIBRARIES}` only (no libdm).

### 1d. pix-fb and bw-fb — libdm-free in Obol builds — Complete (Stage 34)

**`pix-fb` (write a raw .pix file to the framebuffer) and `bw-fb` (write a BW
file to the framebuffer) now have Obol paths that speak the fbserv PKG wire
protocol directly, with no libdm dependency.**

In addition, `BRLCAD_ENABLE_OBOL` detection is now performed at the **top level**
(`brlcad/CMakeLists.txt`) right after `include(BRLCAD_Find_Package)` so that the
flag is available uniformly to every subdirectory without relying on per-directory
`brlcad_find_package(Obol)` calls.

Files changed:

- **`brlcad/CMakeLists.txt`**: Added `brlcad_find_package(Obol)` block with
  `BRLCAD_ENABLE_OBOL` / `BRLCAD_OBOL_DUAL_GL` cache variables set before the
  `verbose_add_subdirectory("" src)` call.  Child CMakeLists keep their own
  `if(NOT DEFINED BRLCAD_ENABLE_OBOL)` guards as incremental-reconfigure fallbacks.

- **`src/fb/pix-fb.c`**: `#ifdef BRLCAD_ENABLE_OBOL` path opens a PKG connection
  to fbserv (`pkg_open`), sends `MSG_FBOPEN`, then sends the pixel data row-by-row
  (or multi-row where applicable) via `MSG_FBWRITERECT`, and closes with
  `MSG_FBCLOSE`.  Supports `-c` (clear), `-i` (inverse), `-m` (multi-line), offsets,
  and autosize.  `pkg_waitfor(MSG_RETURN, …)` used for synchronous acknowledgement.
  Non-Obol `#else` path is the unchanged original `fb_open` / `fb_write` /
  `fb_writerect` / `fb_close` code.

- **`src/fb/bw-fb.c`**: `#ifdef BRLCAD_ENABLE_OBOL` path uses `MSG_FBBWWRITERECT`
  (1 byte/pixel; the server expands each byte to R=G=B).  Selective color-plane
  loading (`-R`/`-G`/`-B`) is unsupported in the Obol path (exits with a clear
  error if a partial subset is requested).  Non-Obol `#else` path unchanged.

- **`src/fb/CMakeLists.txt`**: `pix-fb` and `bw-fb` each have an Obol branch
  linking `libbu + libpkg` only (no libdm, no dm_plugins).  `fb-bw` and `fb-fb`
  moved into the `NOT BRLCAD_ENABLE_OBOL` block since they have no Obol path yet.

### 1b. fb-pix — libdm-free in Obol builds — Complete (Stage 26)

**`fb-pix` (write framebuffer contents to a raw .pix file) now has an Obol path
that speaks the fbserv PKG wire protocol directly, with no libdm dependency.**

Two files changed:

- **`src/fb/fb-pix.c`**: `#ifdef BRLCAD_ENABLE_OBOL` path connects to the fbserv
  TCP port using raw libpkg — `MSG_FBOPEN` handshake, `MSG_FBREADRECT` one row at a
  time, writes raw RGB bytes to the output file, then `MSG_FBCLOSE`.  No libdm
  symbols used.  Non-Obol `#else` path is the unchanged original `fb_open` /
  `fb_read` / `fb_close` code (including the colormap-crunch option).

- **`src/fb/CMakeLists.txt`**: `fb-pix` Obol branch links `libbu + libpkg` only
  (no libdm, no dm_plugins, no cmap-crunch.c).  Non-Obol branch unchanged.

The full rtwizard pipeline for `.pix` output is now also libdm-free in Obol builds:
`rt -F port` → pixbuf fbserv → `fb-pix -F port outfile.pix` with no libdm anywhere.

### 1a. fbserv — Obol-backed pixel-buffer server — Complete (Stage 24)

**Question: Could `fbserv` be backed by Obol and remain functional (TCP protocol
still works, just no libdm/fb)?**  **Yes — done.**

Three files changed:

- **`src/fbserv/pixbuf_server.c`** (new): Implements all PKG handler functions
  (`fb_server_fb_open`, `fb_server_fb_write`, `fb_server_fb_readrect`, etc.) using
  a `malloc()`'d RGB pixel buffer (`g_pixbuf`) instead of a `struct fb *` from
  libdm.  The same wire protocol is preserved bit-for-bit.  Supported operations:
  FBOPEN/FBCLOSE/FBFREE, FBCLEAR, FBREAD/FBWRITE, FBREADRECT/FBWRITERECT,
  FBBWREADRECT/FBBWWRITERECT, FBFLUSH.  Cursor/colormap/view/zoom operations
  return success stubs so old client code doesn't break.  No libdm symbols used.

- **`src/fbserv/fbserv.c`**: All `fb_*` calls in `main_loop()` and `main()`
  wrapped with `#ifndef BRLCAD_ENABLE_OBOL`.  Also guards `#include "dm.h"`,
  `_fb_disk_enable`, and the `fb_log()` override function.

- **`src/fbserv/CMakeLists.txt`**: Conditional compile — Obol branch uses
  `pixbuf_server.c` and links only `libbu + libpkg` (no libdm, no dm_plugins).
  Non-Obol branch unchanged.

The standalone `fbserv` binary participates in the rtwizard pipeline:
`rt -F port` → pixbuf fbserv → `fb-png -F port outfile.png` works in Obol builds
end-to-end with no libdm anywhere in the chain.

### 2. Delete libdm rendering plugins (CMake gating complete — Stage 27 progress)

All GL rendering plugins are now **skipped in CMake** when `BRLCAD_ENABLE_OBOL`
(Stage 22 completed dm-swrast; Stages 19–21 did qtgl/glx/X/wgl).
The source directories remain on disk; a future stage will delete them.

CMake gating status:

| Directory            | CMake guard when Obol ON    | Replacement                        |
|----------------------|-----------------------------|-------------------------------------|
| `src/libdm/qtgl/`    | ✅ `NOT BRLCAD_ENABLE_OBOL` | `QgObolView`                        |
| `src/libdm/glx/`     | ✅ `NOT BRLCAD_ENABLE_OBOL` | `obol_view` HW GLX path             |
| `src/libdm/wgl/`     | ✅ `NOT BRLCAD_ENABLE_OBOL` | `obol_view` HW WGL path             |
| `src/libdm/X/`       | ✅ `NOT BRLCAD_ENABLE_OBOL` | `obol_view` (Tk/X handled)          |
| `src/libdm/swrast/`  | ✅ `NOT BRLCAD_ENABLE_OBOL` | `QgObolSwrastView` (Coin3D/OSMesa)  |
| `src/libdm/null/`    | n/a — no GL, always built   | No replacement needed               |
| `src/libdm/plot/`    | n/a — no GL, always built   | No replacement needed               |
| `src/libdm/postscript/` | n/a — no GL, always built | No replacement needed              |
| `src/libdm/txt/`     | n/a — no GL, always built   | No replacement needed               |

### 2a. Slim libdm core in Obol builds — Complete (Stage 27)

**Stage 27 gated `libdmgl` and the 2D overlay rendering helpers from Obol
builds, further trimming libdm's footprint when Obol is the renderer.**

Changes made (Stage 27):

- **`src/libdm/CMakeLists.txt`**: `libdmgl` (`dm-gl.c`, `dm-gl_lod.cpp`) is
  now built only when `NOT BRLCAD_ENABLE_OBOL AND BRLCAD_ENABLE_OPENGL`.  All
  five GL rendering plugins already skip `libdmgl` in Obol builds via their
  own `NOT BRLCAD_ENABLE_OBOL` guards, so this change makes the exclusion
  explicit in the library itself.

  The following pure 2D overlay rendering source files are also excluded from
  the Obol `libdm` build (they have no callers in Obol code paths):
  `adc.c`, `axes.c`, `clip.c`, `grid.c`, `labels.c`, `options.c`, `rect.c`,
  `scale.c`.  In non-Obol builds all eight files are compiled as before.

- **`src/libdm/view.c`**: Added `#ifndef BRLCAD_ENABLE_OBOL` guards around
  all internal calls into the gated files:
  - `dm_draw_faceplate()`: model-axes, view-axes, view-scale, ADC-cursor,
    grid, and rect blocks all guarded.
  - `dm_draw_visitor()`: `dm_draw_scene_axes()` call guarded.
  - `dm_draw_viewobjs()`: both `dm_draw_data_axes()` calls guarded;
    `dm_draw_prim_labels()` block guarded.

  In Obol builds all 2D overlay rendering is handled by Coin3D
  `SoAnnotation` subtrees, so these no-ops are correct.

Remaining steps (source-directory deletion requires committing to Obol-only):
1. Remove the now-dead source directories from disk (git rm) and update
   `cmakefiles()` lists.

### 2d. Gate dm rendering core from Obol builds — Complete (Stage 30)

**Stage 30 moves `dm-generic.c`, `view.c`, and `null/dm-Null.c` into the
`NOT BRLCAD_ENABLE_OBOL` conditional block in `src/libdm/CMakeLists.txt`,
eliminating the last dm-rendering symbols from `libdm.so` in Obol builds.**

Background:
- `dm-generic.c` supplies the ~70 accessor / mutator helpers for `struct dm`
  (`dm_get_width`, `dm_graphical`, `dm_close`, `dm_set_bg`, …).  In Obol
  builds these are only called from `view.c` (now excluded) and from
  `dm_plugins.cpp` (already excluded since Stage 29).
- `view.c` contains the dm-rendering visitor and draw helpers —
  `dm_draw_faceplate`, `dm_draw_viewobjs`, `dm_draw_objs`, `dm_draw_arrow`,
  `dm_draw_arrows`, `dm_draw_polys`, `dm_draw_lines`, `dm_draw_labels`, etc.
  All external callers (`QgGL.cpp`, `QgSW.cpp`, `gsh.cpp` USE_DM block) were
  already gated NOT BRLCAD_ENABLE_OBOL in earlier stages, so this entire file
  is dead code in Obol builds.
- `null/dm-Null.c` provides `dm_null`, the always-present no-op display
  manager.  The only reference to `dm_null` is in `dm_plugins.cpp`
  (`dm_null.i->dm_open(...)` in the "nu" branch) which was excluded in
  Stage 29, so `dm_null` is unreferenced in Obol builds.

Changes made (Stage 30):
- **`src/libdm/CMakeLists.txt`**: `null/dm-Null.c`, `dm-generic.c`, and
  `view.c` moved from the always-compiled `LIBDM_SRCS` list into the
  `if(NOT BRLCAD_ENABLE_OBOL)` conditional block (alongside `dm_plugins.cpp`
  and the 2D overlay helpers).  `cmakefiles()` registration updated.

Result: in Obol builds `libdm.so` now exports **only** the fb API
(`fb_open`, `fb_write`, `fb_close`, …), the fb utility helpers
(`fb_common_file_size`, …), the fbserv infrastructure, and the dm-open
stub functions (`dm_open` → `DM_NULL`, `dm_have_graphics` → 0, …).  No
`dm_draw_*`, no `dm_get_width`, no `dm_null`, no plugin loading.

### 2c. Split dm_plugins.cpp and gate dm_open in Obol builds — Complete (Stage 29)

**Stage 29 splits `dm_plugins.cpp` into a fb-only `fb_plugins.cpp` (always
compiled) and a dm-only `dm_plugins.cpp` (non-Obol only), gates the dm plugin
loading loop in `dm_init.cpp`, and provides lightweight Obol stubs for the
`dm_open` family of functions.**

Changes made (Stage 29):

- **`src/libdm/fb_plugins.cpp`** *(new)*: Contains the framebuffer plugin API
  implementations — `fb_open`, `fb_set_interface`, `fb_get_platform_specific`,
  `fb_put_platform_specific`, `fb_genhelp`.  These read only `fb_backends` and
  have no dependency on dm rendering plugins.  Compiled in ALL builds.

- **`src/libdm/dm_plugins.cpp`** *(trimmed)*: Now contains only the dm
  rendering plugin API — `dm_open`, `dm_have_graphics`, `dm_graphics_system`,
  `dm_list_types`, `dm_validXType`, `dm_valid_type`, `dm_bestXType`,
  `dm_default_type`.  Compiled only when `NOT BRLCAD_ENABLE_OBOL`.

- **`src/libdm/dm_obol_stubs.cpp`** *(new)*: Provides stub implementations of
  all 8 dm rendering API functions for Obol builds.  Stubs return `DM_NULL`,
  `0`, `NULL`, or `"nu"` immediately without consulting any backend map.
  Compiled only when `BRLCAD_ENABLE_OBOL`.

- **`src/libdm/dm_init.cpp`**: The dm plugin loading loop (file scan, dlopen,
  `dm_plugin_info` / `fb_plugin_info` symbol lookup) is now wrapped with
  `#ifndef BRLCAD_ENABLE_OBOL`.  In Obol builds, `libdm_init` still populates
  `fb_backends` with the four built-in interfaces (null, debug, mem, stack) but
  skips the entire dynamic plugin scan.  The `get_dm_map()` singleton, the
  `dm_handles` set, and their associated headers (`<set>`, `<algorithm>`,
  `<cctype>`, `bu/dylib.h`, `bu/file.h`) are all excluded via `#ifndef`.

- **`src/libdm/CMakeLists.txt`**: `fb_plugins.cpp` added to `LIBDM_SRCS`
  unconditionally.  `dm_plugins.cpp` appended only when
  `NOT BRLCAD_ENABLE_OBOL`; `dm_obol_stubs.cpp` appended when
  `BRLCAD_ENABLE_OBOL`.  `cmakefiles()` registration updated so both new
  files appear in distribution tarballs regardless of which build variant is
  active.

After Stage 29, in Obol builds, libdm:
- Never scans or dlopen's any dm rendering plugin
- Never allocates or populates `dm_backends` (the pointer stays `NULL`)
- Exposes the dm_open/dm_have_graphics/… API via zero-overhead stubs
- Still fully populates `fb_backends` for the built-in fb interfaces

### 2b. Gate / re-express final dm_open callers in Obol builds — Complete (Stage 28)

**Stage 28 both gates `dm_open` call sites from Obol build paths and replaces
libdm-backed command implementations with equivalent Obol-aware paths that
operate directly on `fbs_pixbuf` and `bsg_view`.**

Changes made (Stage 28a — gating):

- **`src/gtools/gsh/gsh.cpp`**: `#define USE_DM 1` is now gated with
  `#ifndef BRLCAD_ENABLE_OBOL`.  The `DisplayHash` class and both its
  methods, `GshState::view_checkpoint()` and `GshState::view_update()`, are
  wrapped with `#ifdef USE_DM`.  Call sites in `GshState::GshState()` and
  `GshState::eval()` are similarly guarded.

- **`src/gtools/gsh/CMakeLists.txt`**: Obol branch links `libged + libbu`
  only; non-Obol branch links `libged + libdm + libbu`.

- **`src/adrt/CMakeLists.txt`**: `isst` (which calls `dm_open`) is now
  built only when `NOT BRLCAD_ENABLE_OBOL AND BRLCAD_ENABLE_OPENGL AND
  BRLCAD_ENABLE_TK`.

- **`src/util/CMakeLists.txt`**: `plot3-dm` (which calls `dm_open` via
  X11/ogl) is now built only when `NOT BRLCAD_ENABLE_OBOL AND BRLCAD_ENABLE_TK`.

Changes made (Stage 28b — Obol-aware `dm` GED command):

- **`src/libged/dm/CMakeLists.txt`**: `GEDPL_DEFINITIONS = BRLCAD_ENABLE_OBOL`
  in Obol builds; plugin links `libged + libbu` only (no libdm).

- **`src/libged/dm/dm.c`**: Every subcommand has `#ifdef BRLCAD_ENABLE_OBOL`
  early-return paths that use `bsg_view` fields directly (no `struct dm *`):

  | subcommand | Obol behaviour |
  |---|---|
  | `type` / `types` | returns `"obol"` |
  | `list` | enumerates `bsg_view` instances by `gv_name` |
  | `width` / `height` | reads `gv_width` / `gv_height` from the current view |
  | `attach` | resolves/creates view without calling `dm_open` |
  | `debug` | returns `0` (no libdm debug level) |
  | `get` | reports `gv_name`, `gv_width`, `gv_height`, type `obol` |
  | `set` | redirects user to application-layer commands |
  | `bg` | returns default gradient; redirects user to `to_bg`/`obol_view` |
  | `initmsg` | reports "Obol rendering backend active" |

- **`src/libged/dm/screengrab.c`**: Obol path reads from `fbs_pixbuf`
  (packed RGB888, populated by `ert`) into `icv_image`; `dm.h` not included.

Changes made (Stage 28c — Obol-aware fb GED commands):

- **`src/libged/fbclear/fbclear.c`**: Obol path clears `fbs_pixbuf` with
  the requested colour, sets `gv_fb_mode = 2`, and calls
  `ged_refresh_handler` to trigger a repaint.

- **`src/libged/pix2fb/pix2fb.c`**: Obol path reads raw RGB888 data from
  the `.pix` file descriptor directly into `fbs_pixbuf` row by row, then
  calls `ged_refresh_handler`.

- **`src/libged/fb2pix/fb2pix.c`**: Obol path writes `fbs_pixbuf` rows to
  the output file as raw RGB888 (same `.pix` wire format), respecting the
  `-i` inversion flag.

- **`src/libged/png2fb/png2fb.c`**: Obol path uses `icv_read` to decode the
  PNG, converts `double [0,1]` pixel data to RGB888, fills `fbs_pixbuf`, and
  calls `ged_refresh_handler`.

- All four fb plugin **CMakeLists.txt** files: `GEDPL_DEFINITIONS =
  BRLCAD_ENABLE_OBOL`; link `libged + libbu` (png2fb also adds `libicv`);
  no `libdm` in Obol builds.

After Stage 28, `dm_open` has **no remaining callers in Obol builds** and
`libdm` has **no remaining link dependencies in Obol builds** for the ged
plugins above.

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
