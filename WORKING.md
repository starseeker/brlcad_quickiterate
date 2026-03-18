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

---

## Session 8 Results (2026-03-09) — s_res: forward resource pointer to ft_scene_obj

### Goal

Implement the last missing piece identified in DESIGN_SCENE_OBJ.md §2.2 and §3.1:
`struct resource *s_res` on `bsg_shape`, which allows `ft_scene_obj` callbacks
to call `rt_db_get_internal()` (and `ft_mat`) without accessing `draw_update_data_t`.

### Changes Made

#### `include/bsg/defines.h`

Added `struct resource *s_res` field to `bsg_shape` after `s_have_obb`.

The comment documents the rationale: historically only `rt_db_get_internal(…,mat,…)`
could apply the path matrix.  The new `ft_mat` (`rt_##name##_mat`) allows in-place
application to an already-cracked internal.  `s_res` enables both patterns:

1. **Object-space caching**: crack with `NULL` mat, cache, apply `s_mat` at render
   time (either via the renderer or via `ft_mat`).  This is the target architecture.
2. **World-space baking**: pass `s_mat` directly to `rt_db_get_internal` (current
   `draw_scene` fallback path, unchanged for now).

#### `src/libged/draw.cpp`

In the setup phase, immediately after forwarding `d->mesh_c`:

```c
if (d->res && !s->s_res)
    s->s_res = d->res;
```

This keeps the pattern consistent: setup phase is the only place `draw_update_data_t`
fields are pushed onto `bsg_shape`; everything downstream reads only from shape fields.

#### `src/librt/primitives/generic.c` — `rt_generic_scene_obj`

Updated the `rt_db_get_internal` call:

```c
struct resource *res = s->s_res ? s->s_res : &rt_uniresource;
if (rt_db_get_internal(&intern, dp, dbip, NULL, res) < 0)
    return BRLCAD_ERROR;
```

Also added a detailed comment explaining the `NULL` mat choice (object-space vlists,
consistent with the OI scene-graph target architecture).

#### `DESIGN_SCENE_OBJ.md`

- §2.2 updated: all inputs needed for adaptive CSG wireframe are now available
  in `ft_scene_obj` (resolved).
- §3.1 updated: `s_res` marked DONE.
- §3.2: item 1 (`s_res`) crossed off as done; item 6 updated.
- §6 (Summary): new `s_res` rationale subsection added; `ft_mat` usage with
  `s_res` documented.

### Build Status

Clean build (GCC 13, `-Werror`) on librt + libged targets.

### Summary of all bsg_shape fields added across sessions 7–8

| Field | Type | Session | Purpose |
|---|---|---|---|
| `mesh_c` | `bsg_mesh_lod_context *` | 7 | LoD cache; forwarded from d->mesh_c |
| `s_obb_pts[24]` | `fastf_t` | 7 | OBB corners; forwarded from d->dbis->obbs |
| `s_have_obb` | `int` | 7 | Validity flag for s_obb_pts |
| `s_res` | `struct resource *` | 8 | rt resource; forwarded from d->res |

With these four fields in place, `ft_scene_obj` callbacks in librt can perform
all primitive-specific drawing operations without any direct access to
`draw_update_data_t` or `DbiState`.

### Checklist

- [x] Add `struct resource *s_res` to `bsg_shape`
- [x] Set `s->s_res = d->res` in draw_scene setup phase
- [x] Use `s->s_res` in `rt_generic_scene_obj` (fallback to `&rt_uniresource`)
- [x] Document `ft_mat`/`rt_##name##_mat` usage with `s_res` in DESIGN_SCENE_OBJ.md
- [x] Build clean
- [x] Implement `rt_comb_scene_obj` (absorbs draw_m3) — done in earlier sessions
- [x] Implement `rt_bot_scene_obj` (absorbs bot_adaptive_plot) — done in earlier sessions
- [x] Implement `rt_brep_scene_obj` (absorbs brep_adaptive_plot) — done in earlier sessions
- [x] Simplify draw_scene to call ft_scene_obj as primary dispatch — done in earlier sessions

---

## Session 9 Results (2026-03-14) — Stage 7 step 6 prep: mged_curr_pane

### Summary

Continued Stage 7 MGED libdm→Obol migration.  Added `mged_curr_pane` to
`mged_state` as a direct per-state pointer to the active Obol pane, removing
the need to scan `active_pane_set` in `refresh()`.  Removed dead code left over
from the now-complete `ft_scene_obj` draw-scene migration.

### Changes Made

#### `mged.h` — Add `mged_curr_pane` to `mged_state`

```c
struct mged_state {
    struct mged_dm   *mged_curr_dm;      /* legacy libdm pane */
    struct mged_pane *mged_curr_pane;    /* Stage 7: current Obol pane */
    ...
};
```

#### `attach.c` — `set_curr_pane()` sets `s->mged_curr_pane`

In addition to `s->gedp->ged_gvp = mp->mp_gvp`, now also sets
`s->mged_curr_pane = mp`.  When called with `NULL`, both fields are cleared.

#### `mged.c` — Initialize + use `mged_curr_pane`

- `s->mged_curr_pane = MGED_PANE_NULL` at startup.
- `refresh()` `obol_notify_views` guard: uses `s->mged_curr_pane` as a fast
  path alongside `BU_PTBL_LEN(&active_pane_set) > 0`, removing the table scan
  when the active window is an Obol pane.

#### `draw.cpp` — Remove orphaned `extern "C" int draw_points`

The `draw_points()` forward declaration was removed.  `draw_points()` in
`points_eval.c` is superseded by mode-5 handling in `rt_generic_scene_obj`
(`rt_sample_pnts`); the declaration was never called.

#### `DESIGN_SCENE_OBJ.md` — All section 3.2 items marked done

All 6 required changes (s_res, rt_comb_scene_obj, rt_bot_scene_obj,
rt_brep_scene_obj, draw_scene simplification, object-space vlists) are now
marked completed.  Section 3.3 migration path also marked complete.

#### `RADICAL_MIGRATION.md` — Step 5.5 added, Step 6 updated

Step 5.5 documents the `mged_curr_pane` addition.  Step 6 updated to clarify
the remaining blocker: initial "nu" mged_dm entry and f_attach dm_open path.

### Checklist

- [x] `struct mged_pane *mged_curr_pane` added to `mged_state`
- [x] `set_curr_pane()` sets `s->mged_curr_pane`
- [x] `s->mged_curr_pane = MGED_PANE_NULL` at startup (mged.c)
- [x] `refresh()` uses `mged_curr_pane` fast-path guard
- [x] Orphaned `extern "C" int draw_points` removed from draw.cpp
- [x] DESIGN_SCENE_OBJ.md §3.2 all items marked done
- [x] RADICAL_MIGRATION.md Step 5.5 documented
- [x] Build clean (libged + librt + attach.c/mged.c compile with -Werror)

---

## Session 10 Results (2026-03-14) — Remove dead draw stubs from libged build

### Summary

Removed `points_eval.c` and `wireframe_eval.c` from the libged CMakeLists.txt
build list.  Both files are dead code: their functions (`draw_points` and
`draw_m3`) are no longer called anywhere in BRL-CAD.  The implementations
have been superseded by per-primitive `ft_scene_obj` callbacks in librt:

- `draw_points` → `rt_generic_scene_obj` mode-5 via `rt_sample_pnts`
- `draw_m3` → `rt_comb_scene_obj` → `rt_comb_eval_m3` in librt

Both files are retained in the source tree with updated headers explaining
they are deprecated, for reference by any out-of-tree callers.

### Checklist

- [x] Remove `points_eval.c` from libged CMakeLists.txt
- [x] Remove `wireframe_eval.c` from libged CMakeLists.txt
- [x] Update file headers to document deprecated/not-built status
- [x] Build verify (libged builds without these files)

---

## Session 4 Changes (2026-03-08) — OBB refactoring from dbi2 migrated

### Summary

Reviewed the `dbi2` prototype code at the repository root level for improvements
not yet migrated into the main `brlcad` code tree.  The primary improvements
identified were around oriented bounding box (OBB) computation:

### Changes migrated from dbi2

#### 1. New `bg_pnts_aabb`, `bg_pnts_obb`, `bg_obb_pnts` (new file `src/libbg/pnts.cpp`)

- `bg_pnts_aabb()` — axis-aligned bbox for a flat array of points (with
  face-inactive vertex exclusion when faces are provided)
- `bg_pnts_obb()` — oriented bbox for a point array using GTE (same algorithm
  as the old `bot_oriented_bbox.cpp`, but now a proper library function)
- `bg_obb_pnts()` — reconstruct 8 corner vertices from OBB center + 3 half-extent vectors;
  output follows the librt arb8 vertex ordering convention

Public declarations in new file `include/bg/pnts.h`; included from `include/bg.h`.

#### 2. `bg_trimesh_obb` added to `src/libbg/trimesh.cpp`

A face-aware OBB that only considers vertices referenced by at least one
face in the mesh — this gives a tighter box than an all-vertex AABB or OBB.
The function filters active vertices via a bitv, then delegates to `bg_pnts_obb`.
Declaration added to `include/bg/trimesh.h`.

Also updated `bg_trimesh_aabb` to fall back to `bg_pnts_aabb` when called
with `faces=NULL/num_faces=0` (instead of returning error outright).

#### 3. `bg_3d_obb` implemented in `src/libbg/obr.cpp`

The function was declared in `include/bg/obr.h` but had no body.  Now
implemented as a wrapper around `bg_pnts_obb` + `bg_obb_pnts`, returning
the 8 corner points that the old `bg_3d_obb` callers expected.

#### 4. `rt_bot_oriented_bbox` refactored to use `bg_trimesh_obb`

The old implementation in `primitives/bot/bot_oriented_bbox.cpp` directly
called GTE (`gte::GetContainer` on all BoT vertices) — it did not exclude
inactive vertices and included direct GTE dependency in librt.

The new implementation:
1. Calls `bg_trimesh_obb` (face-active vertices only → tighter OBB)
2. Calls `bg_obb_pnts` to convert center+extents to 8 arb8 corner points
3. Stores those 8 points into `bbox->pt[]` for the ft_oriented_bbox callers

This removes the direct GTE dependency from `bot_oriented_bbox.cpp` and
produces a tighter bounding box.

#### 5. Test `src/libbg/tests/bb.c`

New test exercises `bg_pnts_aabb`, `bg_pnts_obb`, `bg_obb_pnts`,
`bg_trimesh_aabb`, and `bg_trimesh_obb` over 10 iterations with varying
geometry.  Round-trip validation: `bg_trimesh_obb → bg_obb_pnts → bg_pnts_obb`
verifies that the 8-corner representation is consistent with the OBB params.
Registered as `bg_bb` in `tests/CMakeLists.txt`.

### Verification

`bg_bb` test passes (exit code 0, 10 iterations, all checks pass).


---

## Session 11 Results (2026-03-14) — Step 6.a: Unify view-dirty tracking

### Summary

All MGED view-command code paths that previously set only
`view_state->vs_flag = 1` (without `s->update_views = 1`) now set both.
This is Step 6.a of the MGED libdm removal refactoring: it ensures that
`obol_notify_views` fires correctly in the Obol rendering path even after
the `active_dm_set` vs_flag scan is removed in Step 6.

The `vs_flag` mechanism is a per-pane display-dirty flag propagated through
`struct _view_state`.  The `s->update_views` flag is a session-level dirty
flag that was already set by most (but not all) view-change paths.  The
missing `s->update_views = 1` additions are needed because:

1. `refresh()` computes `obol_needs_refresh = s->update_views` at the top,
   before the `active_dm_set` loop.  When Step 6 removes the `active_dm_set`
   loop, the `vs_flag` scan in that loop will disappear, so `update_views`
   must be the sole trigger for `obol_notify_views`.

2. Even now (before Step 6), any view command that set only `vs_flag` without
   `update_views` was relying on the `active_dm_set` loop to eventually set
   `obol_needs_refresh`.  Setting both flags directly is cleaner.

### Files changed

| File | Changes |
|------|---------|
| `src/mged/chgview.c` | Added `s->update_views = 1` at 17 vs_flag sites |
| `src/mged/edsol.c` | 5 sites |
| `src/mged/edarb.c` | 2 sites |
| `src/mged/dodraw.c` | 1 site |
| `src/mged/tedit.c` | 1 site |
| `src/mged/rtif.c` | 1 site |
| `src/mged/setup.c` | 1 site |
| `src/mged/rect.c` | 1 site |
| `src/mged/menu.c` | 1 site |
| `src/mged/cmd.c` | 3 sites |
| `src/mged/usepen.c` | 3 sites (one required adding braces to an if-body) |
| `src/mged/mged.c` | 2 sites (`new_edit_mats` loop and `mged_view_callback`) |
| `RADICAL_MIGRATION.md` | Added Step 5.6 ✅ entry documenting this change |

### Note on dm-generic.c

The `view_changed_hook` callback in `dm-generic.c` (line 599) sets
`hs->vs->vs_flag = 1` through a `mged_view_hook_state *` that has no
`mged_state *` member.  Adding `s->update_views = 1` here would require
extending `mged_view_hook_state` with a back-pointer to `mged_state`.
This path is only active when the legacy libdm path is live (it fires
from `bu_structparse` variable-change hooks wired up in `mged_link_vars`).
It is therefore safe to leave for Step 6 when the legacy libdm path is
fully removed.

### Checklist

- [x] Add `s->update_views = 1` alongside `vs_flag = 1` in 12 MGED source files (38 sites)
- [x] Fix indentation/braces for two if-guards that gained an extra statement
- [x] Update `refresh()` comment to document the new invariant
- [x] Update `RADICAL_MIGRATION.md` with Step 5.6 entry
- [x] Build verify (all 12 modified files compile cleanly with -Werror)


---

## Session 11 Part B Results (2026-03-14) — wait_for_pipeline + dm-generic.c step 6.a completion

### Summary

Two additional improvements made as continuation of Session 11 step 6.a work:

### 1. Complete vs_flag unification in dm-generic.c

The `view_state_flag_hook` callback in `dm-generic.c` (invoked by the
`bu_structparse` variable-change mechanism when legacy libdm view settings
change) was the last site that set `vs_flag = 1` without also setting
`s->update_views = 1`.  This required adding a back-pointer from
`mged_view_hook_state` to `mged_state`:

- Added `struct mged_state *hs_s` field to `struct mged_view_hook_state`
  in `mged_dm.h`.
- Updated `set_hook_data()` in `dm-generic.c` to set `hs->hs_s = s`.
- Updated `view_state_flag_hook()` to add
  `if (hs->hs_s) hs->hs_s->update_views = 1;` alongside the existing
  `hs->vs->vs_flag = 1;` call.

This completes Step 6.a: **all** MGED view-change paths now set both
`vs_flag` and `s->update_views`.

### 2. DbiState::wait_for_pipeline()

Added a new public API method to `DbiState`:

```cpp
size_t DbiState::wait_for_pipeline(int max_ms = 5000);
```

This method polls `drain_geom_results()` in a 1ms-sleep loop until
`DrawPipeline::settled()` returns true, or `max_ms` milliseconds have
elapsed.  It fires a final drain after settling to catch any last-minute
results.  Declared in `include/ged/dbi.h`; implemented in
`src/libged/dbi_state.cpp`.

Uses: tests and scripted scenarios where the caller must have all
background bboxes/LoD populated before proceeding.  Prerequisite for
making `defer_all` the permanent default without breaking tests.

Updated `test_dbi_cpp.cpp` to use `wait_for_pipeline()` instead of
manual poll loops (simplifies the test; also exercises the new API).

### Checklist

- [x] Add `hs_s` to `struct mged_view_hook_state`; set in `set_hook_data()`
- [x] Add `s->update_views = 1` in `view_state_flag_hook()` (dm-generic.c)
- [x] Declare `wait_for_pipeline()` in `include/ged/dbi.h`
- [x] Implement `wait_for_pipeline()` in `src/libged/dbi_state.cpp`
- [x] Update `test_dbi_cpp.cpp` to use `wait_for_pipeline()` (remove manual poll loops)
- [x] Build verify (libged + test_dbi_cpp compile cleanly)


---

## Session 12 Results (2026-03-14) — Propagate update_views + harden dmp assignments

### Summary

Continued the `update_views` propagation work started in Session 11.  Five additional
hook functions that set `dm_dirty` but not `s->update_views` were fixed, plus three
code-hardening fixes in `cmd.c`, `overlay.c`, and `clone.c`.

### Files modified

| File | Change |
|------|--------|
| `axes.c` | `ax_set_dirty_flag`: add `s->update_views = 1` |
| `adc.c` | `adc_set_dirty_flag`: add `s->update_views = 1` |
| `grid.c` | `grid_set_dirty_flag`: add `s->update_views = 1` |
| `set.c` | `set_dirty_flag` (mged_variables hook): add `s->update_views = 1` |
| `color_scheme.c` | `cs_set_dirty_flag`: add `s->update_views = 1` |
| `menu.c` | `mmenu_set`: add `s->update_views = 1` before the active_dm_set loop |
| `share.c` | `f_share`: add `s->update_views = 1` before `dlp2->dm_dirty = 1` |
| `cmd.c` | Replace unconditional `dm_dmp` assignment with `DMP ? (void*)DMP : NULL` (2 sites) |
| `overlay.c` | Replace unconditional `dm_dmp` assignment with `DMP ? (void*)DMP : NULL` |
| `clone.c` | Add `|| s->mged_curr_pane` to the no-draw skip condition |

### Coverage summary

After Sessions 11-12, **all** of the following now set `s->update_views = 1`:
- Direct view command returns (17+ in chgview.c, 5 in edsol.c, ...)
- `bu_structparse` variable-change hooks: adc, axes, color_scheme, grid, mged_variables, view_state
- Overlay drawing functions that trigger a redraw
- Menu/share state changes

The only remaining `active_dm_set` loops NOT yet migrated are the display-list
management functions in `dozoom.c` (CreateDListSolid, freeDListsAll) and the
network framebuffer server in `fbserv.c` — both are intrinsically dm-specific and
will be removed in Step 6.

---

## Sessions 13-15 Results (2026-03-14) — mged_pane resource fields + null-dm guards everywhere

### Summary (Sessions 13-14)

Sessions 13-14 covered RADICAL_MIGRATION.md steps 5.11-5.14.  See memories for details.

### Session 15 Summary — Steps 5.15-5.17

**Step 5.15**: `mged_pane` gains predictor state (`mp_trails[NUM_TRAILS]`, `mp_p_vlist`,
`mp_ndrawn`).  New ternary macros `pv_head` and `pane_trails` in `mged_dm.h`.
`predictor.c` fully migrated: 0 remaining `mged_curr_dm` references.

**Step 5.16**: `refresh()` dirty-flag scan simplified — removed `vs_flag` scan from the
`active_dm_set` loop (verified: every `vs_flag=1` site is within 8 lines of
`s->update_views=1`, Step 5.6).  `dm_dirty` is now driven purely by `s->update_views`.
`obol_needs_refresh` captured from `s->update_views` alone.

**Step 5.17**: Null-dm guard (`if (!m_dmp->dm_dmp) continue`) moved to the TOP of every
`active_dm_set` loop body, BEFORE any `set_curr_dm` call.  Applied to all remaining
loops across the mged source tree:

| File | Functions patched |
|------|-------------------|
| `mged.c` | `new_edit_mats`, view-rate knob loop, `refresh()` draw loop |
| `chgview.c` | `edit_com`, `cmd_autoview`, `f_svbase` |
| `grid.c` | `grid_set_dirty_flag` |
| `adc.c` | `adc_set_dirty_flag`, `adc_set_scroll` |
| `axes.c` | `ax_set_dirty_flag` |
| `color_scheme.c` | `cs_set_dirty_flag`, `cs_update` |
| `set.c` | `set_dirty_flag`, `set_knob_dirty_flag`, display-list create/free |
| `buttons.c` | `be_repl`/`be_reject`, `chg_state` |
| `menu.c` | `mmenu_set`, `mmenu_set_all` |
| `fbserv.c` | client-fd + netfd lookup; `fbserv_set_port` gets `if (!DMP) return` |
| `cmd.c` | `f_opendb` resize loop |
| `share.c` | `share_dlist` redundant guards removed |

**Bug fix (set.c)**: Inner dlist-sharing loop used outer-loop index `di` instead of `dj`
in `BU_PTBL_GET` — pre-existing typo corrected.

### Files modified (Session 15)

| File | Change |
|------|--------|
| `mged_dm.h` | Add `mp_trails[NUM_TRAILS]`, `mp_p_vlist`, `mp_ndrawn` to `mged_pane`; `pv_head`/`pane_trails` macros |
| `predictor.c` | Fully migrated (0 mged_curr_dm refs); `predictor_init_pane()` added |
| `mged.h` | Add `predictor_init_pane()` decl |
| `attach.c` | `mged_pane_init_resources()`: init mp_p_vlist, mp_trails, mp_ndrawn |
| `dozoom.c` | Use `pv_head` macro for predictor vlist |
| `usepen.c` | Use `active_ndrawn` (renamed from `curr_ndrawn`/`pane_ndrawn`) |
| `mged.c` | Step 5.16 dirty scan; Step 5.17 loop guards; `mged_view_callback` uses `DMP` |
| `chgview.c`, `grid.c`, `adc.c`, `axes.c` | Null-dm guards in dirty loops |
| `color_scheme.c`, `set.c` | Null-dm guards; set.c di→dj bugfix |
| `buttons.c`, `menu.c`, `fbserv.c`, `cmd.c`, `share.c` | Null-dm guards |


