# DrawPipeline Visualization Testing - Session Log

## Summary

The 5-stage concurrent DrawPipeline (attr→AABB→OBB→LoD→write) has been
implemented in the previous session.  This file tracks progress toward
end-to-end visual verification.

---

## Session: Visualization Testing (2026-03-08)

### Goal

Verify that the AABB→OBB→LoD progressive drawing pipeline works correctly in:
1. `gsh` + swrast (headless/scripted framebuffer renders)
2. Interactive `qged` (Qt6 GUI)

Use **GenericTwin.g** as the primary BoT-heavy test model.

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
cmake --build /home/runner/brlcad_build --target gsh Generic_Twin -j$(nproc)
# For qged:
cmake --build /home/runner/brlcad_build --target qged -j$(nproc)
```

### Test plan

1. **gsh/swrast AABB phase test**
   - Set `BRLCAD_CACHE_AABB_DELAY_MS=2000`
   - `draw /component` → swrast screenshot → should show empty/AABB wireframe
   - Wait 4s → screenshot → should show partial AABB fill
   - Wait further → screenshot → should show LoD mesh

2. **gsh/swrast normal draw test**
   - No delays, draw GenericTwin.g, screenshot, verify mesh visible

3. **qged interactive test**
   - Open GenericTwin.g in qged
   - Verify tree loads, draw command shows progressive refinement

### Status

- [ ] Build environment set up
- [ ] GenericTwin.g built successfully
- [ ] gsh swrast test: no delay - baseline draw
- [ ] gsh swrast test: AABB delay - AABB placeholder visible
- [ ] gsh swrast test: OBB delay - OBB placeholder visible
- [ ] qged interactive test
- [ ] Any bugs fixed

### Known issues (as of session start)

- `draw_update_data_t::dbis` is NULL for the `draw_data_t` code path in
  `draw.cpp` (line 1067), meaning OBB lookup in `bot_adaptive_plot` won't
  work for that path. Needs investigation.
- `db_i_internal` new `dcache`/`draw_pipeline` fields: C init order must
  be verified.
