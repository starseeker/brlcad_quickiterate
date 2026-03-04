This effort involves trying to integrate the Obol scene manager into
BRL-CAD to replace (eventually) our very old school libdm solution to
the problem of 3D display.  There is a new prototype libbv API that
attempts to allow BRL-CAD commands to act with view concepts without
introducing dependencies on Obol's API directly into our core code.
The new API is designed to be easier to map to that API than our old
code.

Obol is present in the tree, but if you need to change it you should
build the new Obol version separately and target it's install for
bext_output/install so BRL-CAD's usual configure process can incorporate
it as a proper 3rd party dependency - the Obol code won't build cleanly
inside BRL-CAD's build.  Obol includes an osmesa, which should be installed
over and used instead of the bext osmesa - Obol's copy is newer.

The Qt based qged interface and its associated libqtcad library are the testing
ground for new drawing concepts, and various libged commands have an
alternative drawing code set up (see, for example, draw2.cpp) to work with a
new scene design instead of our old-school MGED style display.  It is this
drawing path and its dependent codes we are seeking (initially) to switch to
leveraging Obol properly.  The dm command will need an Obol-compatible
alternative to traditional libdm backed dm controls as well.

The libqtcad and qged codes are permitted as necessary or desirable
to talk directly to the Obol APIs, but libged and lower codes should
NOT do so - we want to limit our exposure to the Inventor API to
higher level code paths where toolkit integrations make it a necessity.

One of the most complex interactions between the scene and commands is the ert
command, which does async population of an in-scene overlay showing raytracing
results from another program.  We would like to do this in an Obol idiomatic
way, and the ert2 and ert3 commands represent initial attempts to set up such
an Obol-idiomatic alternative.  The difference between ert2 and ert3 is the
network communication approach - ert3 attempts to use a new libbu API that
let's us leverage pipes locally rather than continuing down the fbserv route.

To support the new ert3 approach, we also are setting up a librtrender and
associated program to enable rt-style async communication with another process
without having to go through libdm's APIs.  Eventually the idea is to have librtrender
offer as both a clean C API and a bu_ipc based async protocol all of rt's rendering
features, but for now we need to use basic abilities to plumb out the full
Obol+qged+ert3 interaction stack.

Where we want to get to is being able to have ert3 successfully render an incremental
raytrace in an Obol driven scene manager in qged.  We also need to verify that Obol
can successfully display wireframe and shaded mode data as well (independent of the
ert3 raytracing question.)

To make this work, you'll need to apt update and apt install the Qt6 development
headers.  Obol should be present in the bext_output/install directory, but you may
need to fine tune its integration into the BRL-CAD build a little - that's been through
a few iterations and upstream Obol doesn't yet do a proper Config.cmake solution (although
that should be coming.)

Once you get qged running with Obol for its scene manager, exercise the draw
and ert2/ert3 commands and capture their rendered outputs with qged screenshots
to demonstrate correct drawing behavior.  Make sure you don't accidentally get
a qged using the old display system - we want to make sure we're using Obol
correctly and having it actually display BRL-CAD geometry data in-scene.

All of this is new, unsettled API and code - you are free to change whatever needs
changing without regard to backwards compatibility.  If Obol needs changes, please
prepare an OBOL_CHANGES.md file we can feed upstream to the Obol project to have them
implement what you need.

This file is intended to make it easier for multiple sessions of copilot to quickly
get down to work on the core tasks rather than having to figure out setup issues -
please update and refine it for that purpose as you work.  Unless instructed otherwise,
copilot sessions working on this project should continue working until they get close
to their session time limit or all work is completed.

---

## Session 1 Progress (completed)

### What was done:
1. **Build environment setup**: `sudo apt-get install -y qt6-base-dev qt6-svg-dev libx11-dev libxi-dev libxext-dev libglu1-mesa-dev xvfb`
2. **CMake configure command** (use every session):
   ```
   REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
   cmake -S "$REPO_ROOT/brlcad" -B "$REPO_ROOT/brlcad_build" \
     -DBRLCAD_EXT_DIR="$REPO_ROOT/bext_output" \
     -DBRLCAD_EXTRADOCS=OFF -DBRLCAD_ENABLE_STEP=OFF -DBRLCAD_ENABLE_GDAL=OFF \
     -DBRLCAD_ENABLE_QT=ON -DBRLCAD_ENABLE_OBOL=ON \
     -DOBOL_INSTALL_DIR="$REPO_ROOT/bext_output/install"
   ```
3. **Build command**: `cmake --build brlcad_build --target qged -j$(nproc)`
4. **New features committed**:
   - `qged/main.cpp`: Added `--obol`/`-o` CLI option
   - `QgEdApp`: Added `obol_mode` parameter; selects `QgView_Obol` canvas

---

## Session 2 Progress (completed)

### What was done:
1. **`bv_node_vlist_get()` fix** (scene.cpp): wrapped `bv_scene_obj` nodes stored
   geometry ptr at obj root (`bu_list` linkage), not at `s_vlist` — now returns
   `&s->s_vlist` for wrapped nodes
2. **`bv_render_ctx_update_scene()`** added to render.h + render_ctx.cpp (clears
   node cache, swaps scene, re-syncs Inventor graph)
3. **`bv_render_ctx_sync_scene()`** added — sync without render (for Qt GL path)
4. **`bv_scene_sync_from_view()`** added to defines.h + scene.cpp: in-place sync
   of an existing `bv_scene` from a view's display lists
5. **`*2.cpp` commands updated** to embrace new API:
   - `draw2.cpp`: fixed `local_dobjs` bug; calls `bv_scene_sync_from_view(gedp->ged_scene, cv)` after draw
   - `erase2.cpp`: calls `bv_scene_sync_from_view` after erase
   - `zap2.cpp`: calls `bv_scene_sync_from_view` after clear
   - `rtcheck2.cpp`: calls `bv_scene_remove_obj` for erased overlap objs, `bv_scene_sync_from_view` after new ones are added
6. **`QgEdApp::do_view_changed`**: simplified — draw2 keeps `ged_scene` in sync,
   so `do_view_changed` just calls `obolw->update()` to trigger repaint
7. **`QgEdApp` init_done fix**: replaced `QTimer::singleShot(0)` with
   `QObject::connect(cv, &QgView::init_done, ...)` so `bv_render_ctx_create` only
   runs after `SoDB::init()` has been called by `initializeGL()`
8. **`QgObolWidget::paintGL()`**: now calls `bv_render_ctx_sync_scene(ctx_, view_)`
   then `renderMgr_.render()` — the widget's own render manager uses the Qt GL
   framebuffer (not OSMesa)
9. **`setRenderCtx()`** already connects `ctx->root` to `viewport_` + `renderMgr_`

### Remaining issue (Session 3 TODO) — Obol viewport still black:
The viewport renders black even after `draw all.g`. The draw command runs correctly
(console shows it completed, hierarchy shows `all.g`). Possible causes:

**Most likely**: `sync_camera_to_viewport` is called with `view_` which is the
widget's local `bview_new` (created in the constructor). This view was never
given the GED session's actual camera state. The camera position/target/up are
all zeros → `viewdir` is zero → camera points nowhere → scene is invisible.

**Fix needed**:
- In `QgEdApp`, after `init_done` wires up the render ctx, also call
  `obolw->set_view(mdl->gedp->ged_gvnv)` so the widget uses the GED view's camera
- OR: after draw, call `bv_render_ctx_sync_scene` with `gedp->ged_gvnv`
- OR: after sync, call `ctx->viewport.viewAll()` to auto-fit camera to scene

**Alternatively** — the issue may be that `viewport_.viewAll()` is called when
`ctx->root` only has a directional light (no geometry), so camera ends up very
far away or at origin. After draw adds geometry to `ctx->root` (via `sync_scene`),
the camera is never re-fitted. Try calling `viewport_.viewAll()` after the first
`sync_scene` that adds geometry.

**Also check**: After `setRenderCtx()` wires `viewport_.setSceneGraph(ctx->root)`,
and then `sync_scene` adds children to `ctx->root`, those children should be
visible to `viewport_`'s camera automatically since it shares the same `SoSeparator`.

**Debug approach** for next session:
```cpp
// In paintGL(), before render, add:
bu_log("paintGL: ctx=%p scene_children=%d\n", ctx_,
       ((SoSeparator*)bv_render_ctx_scene_root(ctx_))->getNumChildren());
bu_log("camera pos: %f %f %f\n", cam->position[0], cam->position[1], cam->position[2]);
```

### Running qged with Obol:
```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
export LD_LIBRARY_PATH="$REPO_ROOT/brlcad_build/lib:$REPO_ROOT/bext_output/install/lib:$LD_LIBRARY_PATH"
Xvfb :99 -screen 0 1280x800x24 &; export DISPLAY=:99
$REPO_ROOT/brlcad_build/bin/qged --obol $REPO_ROOT/brlcad/src/libged/tests/draw/moss.g
# In qged console: draw all.g
```
