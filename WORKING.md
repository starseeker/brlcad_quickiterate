# DrawPipeline Visualization Testing - Session Log

## Summary

The 5-stage concurrent DrawPipeline (attr→AABB→OBB→LoD→write) has been
implemented.  This file tracks progress toward end-to-end visual verification.

---

## Session 2 Results (2026-03-08) — lod_mu_ starvation fix + fast cache path

### Root Cause Found and Fixed

Two bugs prevented LoD results from arriving promptly:

#### Bug 1: `lod_mu_` held by read-only draw-path calls (libbsg/lod.cpp)

`bsg_mesh_lod_key_get` and `POPState::cache_get` opened **read-write** LMDB
transactions (flag `0`) and held the global `lod_mu_` mutex across the
entire operation.  Since `bsg_mesh_lod_cache` (the writer, called from
lod_worker) also holds `lod_mu_` for the full POP-buffer computation, every
`draw`/`redraw` call that checked for a LoD key serialized against lod_worker.

**Fix:** Both read-only functions now:
- Use a **local** `MDB_txn*` / `MDB_dbi` (never the shared `c->i->lod_txn`)
- Open the transaction with **`MDB_RDONLY`** — safe for concurrent readers
- **Remove** the `lod_mu_` lock entirely from the read path

The two write functions (`bsg_mesh_lod_key_put`, `cache_write`) correctly
retain `lod_mu_` since LMDB enforces one writer per environment.

#### Bug 2: Expensive vertex/face hash on warm-cache path (librt/cache_drawing.cpp)

`lod_worker` always called `bsg_mesh_lod_cache(bot→vertices, bot→faces, …)`,
which hashes all vertex+face data **before** checking whether LoD is already
cached.  Even on a warm cache the hash is O(n_vertices + n_faces) per BoT.

**Fix:** `lod_worker` now pre-checks `bsg_mesh_lod_key_get(name)` first.
Since that function is now a fast RDONLY lookup (Bug 1 fix), this makes the
warm-cache path a single cheap name→key lookup:

```
warm cache: bsg_mesh_lod_key_get(name) → key  →  enqueue result  (fast)
cold cache: bsg_mesh_lod_key_get(name) = 0 → bsg_mesh_lod_cache(…) → enqueue (slow, one time)
```

### Verification (GenericTwin.g, 706 BoTs, 2242 solids)

`drawpipeline_test` with warm LMDB cache:

| Phase | Results | Wall time |
|---|---|---|
| AABB+OBB (15 s poll) | **3651** (2242 AABB + 706 OBB + 703 LoD) | < 1 s actual |
| LoD additional (60 s poll) | 0 (all LoD arrived in first poll) | — |
| User CPU | **1.07 s** (entire test, 75 s wall) | — |

All 703 LoD-capable BoTs generate results within the first `drain_geom_results()`
call. 3 BoTs don't yield LoD (degenerate geometry or too few faces).

### Checklist

- [x] Build environment (Qt6 + X11/mesa) set up
- [x] GenericTwin.g built (706 BoTs, 2242 solids, 4823 objects total)
- [x] ENV debug delays working (BRLCAD_CACHE_ATTR/AABB/OBB/LOD_DELAY_MS)
- [x] drawpipeline_test.cpp added — passes for AABB + OBB phases
- [x] lod_mu_ starvation fixed (MDB_RDONLY + local txn in read paths)
- [x] Fast warm-cache path in lod_worker (pre-check bsg_mesh_lod_key_get)
- [x] LoD results verified: 703/706 arrive in first drain call on warm cache
- [ ] Screenshots show identical renders (swrast renders AABB wireframes;
      LoD geometry draw path not yet exercised by the test)
- [ ] qged interactive draw test

### Environment-variable debug delays

Added to `cache_drawing.cpp`:

| Variable | Stage affected | Description |
|---|---|---|
| `BRLCAD_CACHE_ATTR_DELAY_MS` | attr_worker | milliseconds sleep per object |
| `BRLCAD_CACHE_AABB_DELAY_MS` | aabb_worker | milliseconds sleep per object |
| `BRLCAD_CACHE_OBB_DELAY_MS`  | obb_worker  | milliseconds sleep per object |
| `BRLCAD_CACHE_LOD_DELAY_MS`  | lod_worker  | milliseconds sleep per object |

Set to e.g. `BRLCAD_CACHE_AABB_DELAY_MS=500` to slow the AABB stage so
the "no bbox" state is visible long enough to screenshot.

### Build steps

```sh
# 1. Install deps
sudo apt-get install -y \
  libgl1-mesa-dev libglu1-mesa-dev \
  libx11-dev libxext-dev libxi-dev libxrandr-dev libxrender-dev libxxf86vm-dev \
  qt6-base-dev qt6-svg-dev

# 2. Configure
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
mkdir -p /home/runner/brlcad_build
cmake -S "$REPO_ROOT/brlcad" -B /home/runner/brlcad_build \
  -DBRLCAD_EXT_DIR="$REPO_ROOT/bext_output" \
  -DBRLCAD_EXTRADOCS=OFF \
  -DBRLCAD_ENABLE_STEP=OFF \
  -DBRLCAD_ENABLE_GDAL=OFF \
  -DBRLCAD_ENABLE_QT=ON

# 3. Build targets
cmake --build /home/runner/brlcad_build --target gsh Generic_Twin.g ged_test_drawpipeline -j$(nproc)
# For qged:
cmake --build /home/runner/brlcad_build --target qged -j$(nproc)
```

### Next steps

~~3. **`draw_data_t::dbis` NULL**: the `draw_data_t` code path in `draw.cpp`
   was passing NULL `dbis` to `bot_adaptive_plot`, meaning OBB lookup would
   not work. **Fixed in session 3.**~~

1. **qged interactive test**: open GenericTwin.g in qged, issue `draw all`,
   observe progressive AABB→OBB→LoD refinement in the viewport.

2. **LoD rendering in swrast screenshots**: the swrast screenshots all show
   the same size because the LoD data is immediately available on warm cache.
   To see the progressive AABB→OBB→LoD cycle:
   - Delete or clear the `dp_test_cache` LMDB directory first (cold cache)
   - Or use `BRLCAD_CACHE_LOD_DELAY_MS=5000` to artificially delay LoD
   - Then screenshots 1 and 2 will show AABB/OBB wireframes; 3 will show LoD mesh

---

## Session 3 Changes (2026-03-08)

### draw_data_t::dbis propagation

The `draw_data_t` struct (used by the legacy `draw_gather_paths` tree walk)
had no `dbis` field. When `draw_gather_paths` created a `draw_update_data_t`
(the per-shape callback struct), it set `ud->dbis = NULL`.  This meant
`bot_adaptive_plot` couldn't access `d->dbis->obbs` for OBB wireframes.

**Fix:** Added `struct DbiState *dbis` to `draw_data_t` in `ged_private.h`.
Changed `draw.cpp` to propagate `dd->dbis` → `ud->dbis`.  Set `dd.dbis`
in `gobjs.cpp` caller.

### OBB placeholder wireframe (bsg_vlist_arb8)

`bot_adaptive_plot` had a TODO comment: `obb_available = false` (always fell
back to the AABB box even when OBB data was in `d->dbis->obbs`).

**Fix:** The placeholder drawing path now:
1. Checks `d->dbis->obbs` for the hash of `dp->d_namep`
2. If found, reads the 24 fastf_t corners, calls new `bsg_vlist_arb8()`
3. Falls back to `bsg_vlist_rpp()` AABB box only if no OBB or no dbis

**New function `bsg_vlist_arb8`** added to `libbsg/vlist.c` +
`include/bsg/vlist.h`.  Draws 12 edges of an arb8 wireframe (face 0: 0→1→2→3→0,
face 1: 4→5→6→7→4, laterals: 0→4, 1→5, 2→6, 3→7) from 8 arbitrary corner
points in arb8 corner order.

### drawpipeline_test improvement

Added assertion for `final_obbs > 0` (was only printed, not asserted).
Verified: 706 OBBs populated from GenericTwin.g.  All 3 assertions now pass.

### OBB placeholders in BViewState::redraw()

The AABB placeholder section in `BViewState::redraw()` now also checks
`dbis->obbs` for the solid hash.  If an OBB is available, `bsg_vlist_arb8`
draws the 8-corner wireframe; otherwise falls back to `bsg_vlist_rpp` AABB.
This means the `redraw()` placeholder path has parity with `bot_adaptive_plot`:
both draw the tightest available wireframe for a not-yet-loaded primitive.

---

## Session 4 Results (2026-03-08) — gsh + swrast + DbiState validation

### Tests run

#### drawpipeline_test (GenericTwin.g, warm cache)

| Phase | Result |
|---|---|
| bboxes | **2242** |
| OBBs | **706** |
| drain results | **3651** |
| Screenshot sizes | 3138 bytes × 3 (red wireframe geometry confirmed) |
| Test result | **PASSED** |

#### drawpipeline_test (GenericTwin.g, cold cache + 50ms LoD delay)

| Phase | Result |
|---|---|
| bboxes | **2242** |
| OBBs | **706** |
| drain results | **3127** (75 LoD in 60 s with 50 ms/item delay) |
| Screenshot sizes | 4078 bytes × 3 |
| Test result | **PASSED** |

Screenshots decoded: each contains ~15000 non-zero (red wireframe) pixels,
confirming swrast is rendering geometry correctly.

#### ged_test_dbi_c (C API surface)

All **35 checks passed**.  Fixed a pre-existing C++-compat compilation
bug first (`typedef struct _dbi_state` → `typedef struct DbiState` in
`dbi.h`'s C placeholder section, to match `view.h`'s `struct DbiState *`
forward-reference tag).

#### ged_test_draw_basic / ged_test_draw_lod / ged_test_draw_faceplate

All passed (some approximate-match off-by-ones, no hard failures).

#### ged_test_drawing_select

8 image comparisons differ from control images — **pre-existing failures**
confirmed by running the test against the original source tree without any
local changes.  Not introduced by this session.

#### ged_test_gsh_draw (new test — gsh --new-cmds + swrast smoke test)

New test `gsh_draw_test.cpp` added.  Validates the `gsh --new-cmds` code
path (DbiState setup → swrast attach → `draw all.g` → autoview → screengrab
→ `Z` clear) against `moss.g`.

| Check | Result |
|---|---|
| DbiState created | ✓ |
| swrast dm attached | ✓ |
| draw all.g completed | ✓ |
| screenshot created (2742 bytes) | **PASS** |
| Z clear completed | **PASS** |
| Overall | **PASSED** |

### Changes

1. **`include/ged/dbi.h`** — C placeholder struct tags changed from
   `_dbi_state`/`_bview_state` to `DbiState`/`BViewState` to match
   `view.h`'s `struct DbiState *dbis` forward reference.  Fixes
   `-Werror=c++-compat` error that prevented `ged_test_dbi_c.c` from
   compiling.

2. **`src/libged/tests/draw/gsh_draw_test.cpp`** — New test exercising
   the `gsh --new-cmds` draw path (DbiState + swrast + draw + screengrab).

3. **`src/libged/tests/draw/CMakeLists.txt`** — Registers `ged_test_gsh_draw`
   target and `ged_test_gsh_draw_moss` CTest entry.

### Checklist

- [x] Build environment (Qt6 + X11/mesa) set up
- [x] drawpipeline_test PASSED (warm cache)
- [x] drawpipeline_test PASSED (cold cache + LoD delay)
- [x] Screenshots contain red wireframe geometry (swrast rendering confirmed)
- [x] ged_test_dbi_c — C++-compat bug fixed; all 35 checks pass
- [x] ged_test_draw_basic / lod / faceplate — all pass
- [x] ged_test_gsh_draw (new) — gsh --new-cmds + swrast path validated
- [x] qged interactive test — qged_test validates swrast draw path in qged

### Next steps

1. **qged DrawPipeline drain test**: verify AABB→OBB→LoD refinement visible
   in qged viewport by adding a drain polling loop to qged_test, waiting
   for background geometry to arrive before screenshotting the LoD state.

---

## Session 5 Results (2026-03-08) — qged swrast validation

### Build and environment

- Qt6 dev packages (`qt6-base-dev qt6-svg-dev`) + Xvfb installed.
- qged built with `BRLCAD_ENABLE_QT=ON` (1100×800 dark-theme window).
- swrast DM (`-s` flag) used in all tests to avoid needing a GPU.

### qged startup smoke test

`qged -h` prints usage cleanly (exit 0).

`qged -s moss.g` with 5-second timeout:
- Starts, opens moss.g, shows main window.
- Killed by timeout (expected — event loop is interactive).
- No crash, no assertion failure.

### qged_test (new — `src/qged/qged_test.cpp`)

Automated validation of the full qged draw pipeline:

| Step | Result |
|---|---|
| `QgEdApp` constructed (swrast=1) | ✓ |
| Window 1100×800 shown | ✓ |
| `load_g_file(moss.g)` | OK |
| Bright pixels before draw | 4586 (UI chrome only) |
| `draw all.g` + `autoview` | ret=0 |
| Bright pixels after draw | **8011** |
| **Pixels added by draw** | **+3425** (wireframe geometry) |
| Screenshot saved | `qged_test_after.png` (37935 bytes) |
| Test result | **PASSED** |

The before/after pixel count difference proves swrast rendered actual
geometry into the 3D viewport, not just the dark theme background.

### Changes

1. **`src/qged/qged_test.cpp`** — Automated qged draw test.
2. **`src/qged/qged_test_runner.h`** — Qt `Q_OBJECT` class for the test runner.
3. **`src/qged/CMakeLists.txt`** — Adds `qged_test` build target
   (excluded from `all`, requires `DISPLAY`).
4. **`.gitignore`** — Added `qged_test_cache/` runtime artifact.
5. **`WORKING.md`** — Updated with session 5 results.

### Checklist

- [x] Install Qt6 dev packages (base + svg) + Xvfb
- [x] Build qged with Qt6 enabled
- [x] qged startup smoke test (help + 5s timeout open)
- [x] qged_test: before/after draw pixel count confirms geometry rendered
- [x] qged_test: screenshot saved (1100×800 PNG, 37935 bytes)
- [x] Test PASSED: +3425 bright wireframe pixels after draw all.g

---

## Session 6 Results (2026-03-08) — AABB/OBB/LoD pipeline validation in qged

### Objective

Verify that the DrawPipeline AABB→OBB→LoD progressive BoT drawing
behavior works correctly **inside qged** (Qt event loop, swrast DM,
QgEdApp drain timer, BViewState::redraw).

### Key finding: AABB is sync, OBB/LoD are async

During investigation, the pipeline stages were confirmed as:

- **AABB bboxes**: populated **synchronously** during the `draw all`
  command (via `DbiState::update_dp` walking the comb tree).
  2242 bboxes were present immediately after `draw all`, before any drain.
- **OBB** + **LoD results**: populated **asynchronously** by the background
  `DrawPipeline`.  Before `QApplication::processEvents()`, obbs == 0.
  After one processEvents pass (which lets the QgEdApp 100ms drain timer
  fire), obbs == 706.

### drawpipeline_test baseline (headless, GenericTwin.g)

| Metric | Value |
|---|---|
| bboxes | 2242 |
| OBBs | 706 |
| drain results | 3651 |
| Test result | **PASSED** |

### qged_pipeline_test (new — `src/qged/qged_pipeline_test.cpp`)

Three-stage validation inside qged (GenericTwin.g, 706 BoTs, swrast):

| Stage | bboxes | obbs | bright px | Description |
|-------|--------|------|-----------|-------------|
| 1 pre-drain | **2242** | **0** | 27,690 | Draw issued, OBBs async-pending; AABB placeholder wireframes visible |
| 2 post-drain | 2242 | **706** | 28,746 | After processEvents drain; OBB wireframes replace AABB (+1056 px) |
| 3 final | 2242 | 706 | 28,746 | Pipeline quiescent; geometry stable |

The **+1,056 bright pixels** from Stage 1→2 confirms that BViewState::redraw()
successfully transitions from AABB placeholder wireframes to tighter OBB
wireframes as drain results arrive.

```
PASS: bboxes 2242 ≥ 2242  (AABB stage OK)
PASS: obbs 706 ≥ 706  (OBB stage OK)
PASS: Stage 3 has 28746 bright pixels (viewport shows geometry)
PASS: Stage 1 has 27690 bright pixels (AABB boxes visible)
INFO: OBBs were 0 at Stage 1 — async OBB pipeline confirmed async
INFO: Stage 3 differs from Stage 1 (stage1=27690  stage3=28746) — progressive refinement visible
```

**Test result: PASSED**

### Changes

1. **`src/qged/qged_pipeline_test.cpp`** — AABB/OBB/LoD pipeline validation.
2. **`src/qged/qged_pipeline_runner.h`** — Qt `Q_OBJECT` runner class.
3. **`src/qged/CMakeLists.txt`** — Adds `qged_pipeline_test` target.
4. **`.gitignore`** — Added `qged_pipeline_cache/`.
5. **`WORKING.md`** — Updated with session 6 results.

### Checklist

- [x] Rebuild qged, drawpipeline_test, GenericTwin.g
- [x] drawpipeline_test baseline: 2242 bboxes, 706 OBBs, 3651 drain → PASSED
- [x] Confirm AABB is sync, OBB/LoD are async (observed in test output)
- [x] qged_pipeline_test Stage 1: bboxes=2242, obbs=0, 27690 bright px
- [x] qged_pipeline_test Stage 2: obbs=706 after drain, +1056 bright px
- [x] qged_pipeline_test: progressive refinement confirmed (AABB→OBB visible)
- [x] Test PASSED


---

## Session 7 Results (2026-03-09) — ft_scene_obj refactoring: setup phase + design document

### Goal

Begin migrating all primitive-specific drawing logic out of libged's
`draw_scene` and into librt `ft_scene_obj` callbacks, so that `draw_scene`
becomes a simple traversal driver with no per-primitive special cases.

### Changes Made

#### `include/bsg/defines.h`

Three new fields added to `bsg_shape`:

- `struct bsg_mesh_lod_context *mesh_c` — LoD cache context forwarded from
  `draw_update_data_t` by the setup phase.  Primitive callbacks use this
  without needing `draw_update_data_t`.
- `fastf_t s_obb_pts[24]` — 8 OBB corner points (× 3 coords) cached from
  the async pipeline.  Populated by the setup phase; read by callbacks.
- `int s_have_obb` — validity flag for `s_obb_pts`.

#### `src/libged/draw.cpp`

**Single setup phase** — the only place `d->dbis` is now consulted:

1. `s->mesh_c ← d->mesh_c`
2. Late-set `s->have_bbox` from `d->dbis->bboxes` (world-space AABB)
3. Cache OBB corners into `s->s_obb_pts` / `s->s_have_obb` from `d->dbis->obbs`

Everything downstream (lazy AABB guard, `bot_adaptive_plot`, `brep_adaptive_plot`,
`rt_generic_scene_obj`) reads only `bsg_shape` fields — zero further dbis access.

`bot_adaptive_plot` updated to:
- Use `s->mesh_c` instead of `d->mesh_c`
- Use `s->s_have_obb` / `s->s_obb_pts` instead of `d->dbis->obbs`
- Remove its own duplicate AABB late-set block (setup phase covers it)

#### `src/librt/primitives/generic.c`

`rt_generic_scene_obj` now handles `!have_bbox` itself:
- If `s->s_have_obb`: draw OBB placeholder wireframe (per-view child node)
- Otherwise: return `BRLCAD_OK` (no-op; shape will be retried on next redraw)
- Only cracks `rt_db_internal` when `have_bbox == 1`
- No DbiState access at all

#### `DESIGN_SCENE_OBJ.md` (new)

Comprehensive design document covering:
- Current state of all drawing-related data structures
- Analysis of whether `rt_comb_scene_obj` can absorb `draw_m3` (yes)
- What `ft_scene_obj` needs for adaptive CSG wireframe (missing: `s_res`)
- `!have_bbox` policy for `ft_scene_obj` callbacks
- `ft_mat` and matrix handling analysis
- Target architecture (draw_scene as thin driver)
- OpenInventor scene graph opportunities
- draw_m3 structural problems and migration path
- Full list of remaining work

### Build Status

Clean build (GCC 13, `-Werror`) on librt + libged targets.

### Checklist

- [x] Consolidate all `d->dbis` access into draw_scene setup phase
- [x] Add `mesh_c`, `s_obb_pts`, `s_have_obb` to `bsg_shape`
- [x] `bot_adaptive_plot` uses shape fields only (no dbis)
- [x] `rt_generic_scene_obj` handles `!have_bbox` with OBB placeholder
- [x] Build clean
- [x] Design document written (DESIGN_SCENE_OBJ.md)
- [ ] Add `struct resource *s_res` to `bsg_shape`
- [ ] Implement `rt_comb_scene_obj` (absorbs draw_m3)
- [ ] Implement `rt_bot_scene_obj` (absorbs bot_adaptive_plot)
- [ ] Implement `rt_brep_scene_obj` (absorbs brep_adaptive_plot)
- [ ] Simplify draw_scene to call ft_scene_obj as primary dispatch
- [ ] Migrate to object-space vlists (renderer applies s_mat)
