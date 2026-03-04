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
   mkdir -p "$REPO_ROOT/brlcad_build"
   cmake -S "$REPO_ROOT/brlcad" -B "$REPO_ROOT/brlcad_build" \
     -DBRLCAD_EXT_DIR="$REPO_ROOT/bext_output" \
     -DBRLCAD_EXTRADOCS=OFF -DBRLCAD_ENABLE_STEP=OFF -DBRLCAD_ENABLE_GDAL=OFF \
     -DBRLCAD_ENABLE_QT=ON -DBRLCAD_ENABLE_OBOL=ON \
     -DOBOL_INSTALL_DIR="$REPO_ROOT/bext_output/install"
   ```
3. **Build command**: `cmake --build brlcad_build --target qged -j$(nproc)`
4. **Bug fixes committed**:
   - `fbserv.cpp`: Fixed argument order in `render_run()` call (user-data was in `render_progress_cb` slot)
   - `QgObolWidget.h`: Constructor crash when `view_` was NULL — now allocates a `struct bview` + companion `bview_new` in the constructor (like `QgGL`)
   - `qged/CMakeLists.txt`: Added `librtrender` link dependency for Obol build
5. **New features committed**:
   - `qged/main.cpp`: Added `--obol`/`-o` CLI option to select Obol viewport
   - `QgEdApp`: Added `obol_mode` parameter; selects `QgView_Obol` canvas; deferred timer wires `bv_render_ctx` to `QgObolWidget` after `show()`
6. **Verified**: qged launches without crashing with `--obol` flag; Xvfb screenshot confirmed

### Remaining issues (Session 2 TODO):
1. **Black Obol viewport after `draw` command** — root cause identified:
   - The `draw` command creates `bv_scene_obj` objects (old display list path) in `v->gv_db_grp`
   - The `bv_render_ctx` expects a `bv_scene` with `bv_node` objects
   - Bridge: `bv_scene_from_view(v)` converts view's `bv_scene_obj` objects to `bv_node` wrappers
   - Fix needed: (a) add `bv_render_ctx_update_scene()` API to render.h/render_ctx.cpp that clears the node cache and re-syncs from a new `bv_scene`, (b) call it from `do_view_changed()` in `QgEdApp` when `QG_VIEW_DRAWN` and Obol is active
2. **ert2/ert3 raytrace overlay** — code is wired in fbserv.cpp but needs end-to-end testing
3. **Screenshots** with actual geometry visible needed

### Key architecture notes:
- `bv_render_ctx_create(ged_scene, nullptr, w, h)` — `ged_scene` is a `bv_scene*` that starts empty
- `draw` command → `bv_scene_obj` in `v->gv_db_grp` (NOT in `ged_scene`!)
- Bridge: `bv_scene_from_view(gedp->ged_gvp)` → new `bv_scene*` with all drawn objects as `bv_node` wrappers
- The right fix is to call this bridge in `do_view_changed(QG_VIEW_DRAWN)` and update the render context
- `bv_render_frame()` rebuilds Inventor nodes for all `bv_node`s with `dlist_stale=1`
- `bv_scene_obj_to_node()` always sets `dlist_stale=1` for new nodes, so first sync always triggers rebuild
- The Obol/Inventor render chain: `bv_render_frame()` → `sync_scene()` → `build_so_node()` → `append_geometry()` which uses `bv_node_vlist_get()` to get vlists from the wrapped `bv_scene_obj`
- `append_geometry()` reads `bu_list *vl` from the node — this is the vlist of polyline/polygon data

### Running qged with Obol:
```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
export LD_LIBRARY_PATH="$REPO_ROOT/brlcad_build/lib:$REPO_ROOT/bext_output/install/lib:$LD_LIBRARY_PATH"
Xvfb :99 -screen 0 1280x800x24 &
DISPLAY=:99 $REPO_ROOT/brlcad_build/bin/qged --obol some_file.g
```

### Sample .g file for testing:
`$REPO_ROOT/brlcad/src/libged/tests/draw/moss.g` — contains `all.g` assembly
