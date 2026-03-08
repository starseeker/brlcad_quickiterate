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


