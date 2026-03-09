# BRL-CAD Quick Iterate – Copilot Agent Instructions

## Repository Layout

```
brlcad_quickiterate/
├── brlcad/          # BRL-CAD source tree
├── bext_output/     # Pre-built BRL-CAD external dependencies (bext)
│   ├── install/     # Runtime-installed dependency artifacts
│   └── noinstall/   # Build-time-only dependency artifacts
└── .github/
    └── copilot-instructions.md   # This file
```

## Prerequisites – Qt6 Development Packages

The qged tests (`qged_test`, `qged_pipeline_test`) require Qt6.  **Always run these commands before configuring with `-DBRLCAD_ENABLE_QT=ON`**:

```bash
sudo apt-get update
sudo apt-get install -y \
  qt6-base-dev \
  qt6-base-dev-tools \
  qt6-svg-dev \
  libqt6opengl6-dev \
  libqt6openglwidgets6 \
  libqt6widgets6 \
  libgl1-mesa-dev \
  xvfb   # required to run Qt GUI tests headlessly
```

When Qt6 is available, configure with `-DBRLCAD_ENABLE_QT=ON` instead of `-DBRLCAD_ENABLE_QT=OFF`.

## Configuring BRL-CAD

Use the pre-built dependencies in `bext_output/` together with the flags below to minimize configure and build time.  Run these commands from the **repository root**:

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
mkdir -p brlcad_build
cmake -S "$REPO_ROOT/brlcad" -B "$REPO_ROOT/brlcad_build" \
  -DBRLCAD_EXT_DIR="$REPO_ROOT/bext_output" \
  -DBRLCAD_EXTRADOCS=OFF \
  -DBRLCAD_ENABLE_STEP=OFF \
  -DBRLCAD_ENABLE_GDAL=OFF \
  -DBRLCAD_ENABLE_QT=OFF
```

Expected configure time: ~55 seconds on a fresh build directory (a few seconds on a re-configure).

## Building BRL-CAD

After a successful configure, build with:

```bash
cmake --build /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build -j$(nproc)
```

To build only a specific target (e.g. `libbu`):

```bash
cmake --build /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build --target libbu -j$(nproc)
```

## Important Notes

- **bext_output is pre-built** – do not delete or rebuild it unless strictly necessary; rebuilding bext from source takes a very long time.
- **Build directory is outside the source tree** – always configure with a separate build directory (e.g. `brlcad_build/` at the repo root) so that source-tree integrity checks inside CMake pass.
- **distcheck.yml** – BRL-CAD's cmake system validates that `brlcad/.github/workflows/distcheck.yml` is present and up to date.  The copy committed in this repo was generated from the current source tree; if the source tree is updated you may need to re-generate it by running cmake once, letting it fail, then copying the generated file from `<build_dir>/CMakeTmp/distcheck.yml` into `brlcad/.github/workflows/distcheck.yml`.
- **Ninja is available** but the default Unix Makefiles generator was measured to be faster in this environment for fresh configures.  Either generator works for builds.
- BRL-CAD enforces strict compiler warnings (including `-Werror`) by default, so compiler version matters.  The environment provides GCC 13.

## Architectural Direction: Async-First Draw Pipeline

ALL scene objects (BoTs **and** CSG/wireframe primitives) are treated equally: their AABB is computed lazily by the async `DrawPipeline` rather than synchronously during `gather_paths`.  This is activated by `BRLCAD_CACHE_AABB_DELAY_MS > 0` (for testing) and will become the permanent path once the full re-engineering is complete.

1. **`gather_paths` / `scene_obj`** — only walks the comb tree structure.  Every leaf solid (BoT or CSG) gets `sp->have_bbox = 0` and no view object initially when `BRLCAD_CACHE_AABB_DELAY_MS > 0`.
2. **All leaf data collection** flows through the async `DrawPipeline` (5-stage: attr → AABB → OBB → LoD → write):
   - AABB: `aabb_worker` calls `ft_bbox` on every primitive (BoT and CSG alike).
   - OBB: `obb_worker` calls `ft_oriented_bbox` (BoTs and BREPs mainly).
   - LoD: `lod_worker` caches mesh data for BoTs only.
3. **`bot_adaptive_plot` / `draw_scene`** — both return a no-op when `have_bbox == 0` and nothing is available yet. On the next `do_view_changed(QG_VIEW_DRAWN)` redraw (triggered when pipeline data arrives), the missing-view-obj scan in `BViewState::redraw` retries the shape.  Drawing priority is: LoD > OBB placeholder > AABB placeholder > no-op.
4. **`drain_background_geom`** — tracks AABB, OBB, and LoD counts; calls `do_view_changed(QG_VIEW_DRAWN)` whenever any counter advances.
5. **Progressive autoview** — `bsg_view_autoview()` sets `gv_s->gv_progressive_autoview = 1`.  `drain_background_geom` re-calls `bsg_view_autoview` every time new AABBs arrive so the camera tracks the growing scene.  The flag is cleared when `BViewState::shapes_without_bbox()` returns 0 (all shapes have bboxes) or when any explicit user view command is run.

The env vars `BRLCAD_CACHE_AABB_DELAY_MS`, `BRLCAD_CACHE_OBB_DELAY_MS`, `BRLCAD_CACHE_LOD_DELAY_MS` inject per-item delays to simulate expensive geometry in tests (see `qged_pipeline_test`).
