# MGED Editing Rework — TODO & Validation Tracking

## Overview

This document tracks the in-progress migration of MGED's primitive-specific editing
logic from MGED itself into `librt`, using a new callback/functab architecture
(`rt_edit`, `rt_edit_map`, `EDOBJ[]`).  The goal is to:

1. Get all primitive-specific editing logic (menus, mouse interaction, label placement,
   keypoint logic, parameter read/write) out of MGED and into per-primitive files in
   `brlcad/src/librt/primitives/`.
2. Validate that the reworked MGED produces editing behavior **equal to or better than**
   the vanilla (upstream) MGED.
3. Document and fix any regressions found during validation.

The `brlcad_vanilla/` tree in this repo is the upstream BRL-CAD HEAD without our
changes; it serves as the ground-truth for correct editing behavior.

---

## Repository Layout Reminder

```
brlcad/           – working rework tree (librt editing + new MGED)
brlcad_vanilla/   – upstream BRL-CAD HEAD, ground-truth reference
brlcad_build/     – out-of-tree build directory (cmake configured separately)
brlcad_vanilla_build/ – out-of-tree build for vanilla (configure as needed)
```

---

## Build Setup

### Working (reworked) MGED

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
mkdir -p "$REPO_ROOT/brlcad_build"
cmake -S "$REPO_ROOT/brlcad" -B "$REPO_ROOT/brlcad_build" \
  -DBRLCAD_EXT_DIR="$REPO_ROOT/bext_output" \
  -DBRLCAD_EXTRADOCS=OFF \
  -DBRLCAD_ENABLE_STEP=OFF \
  -DBRLCAD_ENABLE_GDAL=OFF \
  -DBRLCAD_ENABLE_QT=OFF
cmake --build "$REPO_ROOT/brlcad_build" -j$(nproc)
```

### Vanilla (reference) MGED

```bash
mkdir -p "$REPO_ROOT/brlcad_vanilla_build"
cmake -S "$REPO_ROOT/brlcad_vanilla" -B "$REPO_ROOT/brlcad_vanilla_build" \
  -DBRLCAD_EXT_DIR="$REPO_ROOT/bext_output" \
  -DBRLCAD_EXTRADOCS=OFF \
  -DBRLCAD_ENABLE_STEP=OFF \
  -DBRLCAD_ENABLE_GDAL=OFF \
  -DBRLCAD_ENABLE_QT=OFF
cmake --build "$REPO_ROOT/brlcad_vanilla_build" -j$(nproc)
```

### X11 / Mesa / Xvfb Setup (for interactive testing)

```bash
sudo apt-get install -y \
  libx11-dev libxext-dev libxt-dev libxi-dev libxmu-dev \
  mesa-common-dev libglu1-mesa-dev \
  xvfb x11-utils
```

To run MGED headlessly with a virtual framebuffer:

```bash
Xvfb :99 -screen 0 1280x1024x24 &
export DISPLAY=:99
# Then run mged normally, e.g.:
$REPO_ROOT/brlcad_build/bin/mged test.g
```

To capture a screenshot for visual comparison:

```bash
DISPLAY=:99 import -window root /tmp/mged_screenshot.png
# or
DISPLAY=:99 scrot /tmp/mged_screenshot.png
```

---

## Architecture Summary

### New librt Editing Infrastructure

| File | Purpose |
|------|---------|
| `brlcad/include/rt/edit.h` | Public API: `rt_edit` struct, `RT_PARAMS_EDIT_*` and `ECMD_*` constants, `rt_edit_map` API |
| `brlcad/src/librt/edit.cpp` | `rt_edit_map` C++ implementation (callback dispatch per ECMD type) |
| `brlcad/src/librt/primitives/edgeneric.c` | Generic editing ops: `edit_sscale`, `edit_stra`, `edit_srot`, `edit_abs_tra`, `edit_keypoint`, matrix-edit XY handlers |
| `brlcad/src/librt/primitives/edit_private.h` | Internal declarations for generic edit helpers shared across primitive ed*.c files |
| `brlcad/src/librt/primitives/edtable.cpp` | `EDOBJ[]` table — one entry per primitive ID mapping to per-primitive edit callbacks |

### Per-Primitive Edit Callback Interface (declared via `EDIT_DECLARE_INTERFACE`)

Each primitive's `ed<prim>.c` file is expected to implement:

| Function | Purpose |
|----------|---------|
| `rt_edit_<prim>_labels` | Draw primitive parameter labels on screen |
| `rt_edit_<prim>_keypoint` | Return the editing keypoint (pivot for scale/rotate) |
| `rt_edit_<prim>_e_axes_pos` | Update the editing axes position |
| `rt_edit_<prim>_write_params` | Serialise parameters to a `bu_vls` (for `tedit`) |
| `rt_edit_<prim>_read_params` | Parse parameters from a string (for `tedit`) |
| `rt_edit_<prim>_edit` | Apply the current `edit_flag` operation to primitive data |
| `rt_edit_<prim>_edit_xy` | Apply a mouse-drag (view XY) edit to the primitive |
| `rt_edit_<prim>_prim_edit_create/destroy/reset` | Optional private per-edit-session state |
| `rt_edit_<prim>_menu_str` | Return the Tk/Tcl menu descriptor string |
| `rt_edit_<prim>_set_edit_mode` | Set `edit_flag` from a menu selection |
| `rt_edit_<prim>_menu_item` | Return the `rt_edit_menu_item[]` array for MGED menus |
| `rt_edit_<prim>_edit_desc` | (optional) Return `rt_edit_prim_desc` for auto-widget generation |
| `rt_edit_<prim>_get_params` | (optional) Get current scalar param values for a given ECMD id |

### MGED Glue (new files)

| File | Purpose |
|------|---------|
| `brlcad/src/mged/mged_impl.h` | C++ class `MGED_Internal` holding `std::map<int, rt_edit_map*>` per primitive type |
| `brlcad/src/mged/mged_impl.cpp` | `mged_state_create/destroy`, `mged_state_clbk_set/get`, `mged_edit_clbk_sync` |
| `brlcad/src/mged/edsol.c` | Greatly reduced MGED-side solid editing glue; still has TODOs (see below) |
| `brlcad/src/mged/sedit.h` | Minimal: macros `SEDIT_ROTATE`, `SEDIT_TRAN`, `SEDIT_SCALE`, `OEDIT_*`, `EDIT_*` |

---

## Code-Level TODOs / FIXMEs

These are tracked items found in the code that need to be resolved:

### `brlcad/src/mged/edsol.c`

- [ ] **Line 53** – `FIXME: Globals` — remaining module-level globals need to be moved into state structs
- [ ] **Line 329** – `TODO - figure out what this is doing` — unknown logic block needs audit
- [ ] **Line 571** – `TODO - fix` — incomplete fix for a specific code path
- [ ] **Line 665** – `TODO - this needs dbip because the ft_xform routines are calling ft_export and ft_import` — xform routines require database access; needs proper threading of `dbip` through the call chain
- [ ] **Line 765** – `TODO - this needs to move to the ft_edit_xy callbacks as MATRIX_EDIT` — matrix-edit XY mouse path not yet delegated to per-primitive callbacks
- [ ] **Line 779, 823** – `TODO - not using this anymore, revert/fix` — dead code or incorrect paths
- [ ] **Line 1240** – `XXX hack to restore MEDIT(s)->es_int after rt_db_put_internal blows it away` — should be handled cleanly
- [ ] **Line 1417** – `XXX This really should use import/export interface` — tedit parameter path
- [ ] **Line 1429** – `TODO - is es_int the same as ip here?` — consolidate `es_int` vs `ip` naming
- [ ] **Lines 1963, 2022** – `TODO - write to a vls so parent code can do Tcl_AppendResult` — output should go through `bu_vls` / callback, not direct `printf`

### `brlcad/src/mged/mged.h`

- [ ] **Lines 147-148** — `es_edclass` and `es_type` in `mged_edit_state` are marked with `TODO - can we eliminate these?` — should be replaced by `rt_arb_std_type()` and `EDIT_ROTATE/TRAN/SCALE` macros fully

### `brlcad/src/mged/buttons.c`

- [ ] **Lines 1043-1044** — Functions below that comment are "not yet migrated to libged, still referenced by mged's setup command table — migrate to libged"

### `brlcad/src/mged/share.c`

- [ ] **Line 286** — `TODO - is e_type actually used here?`

### `brlcad/src/librt/primitives/arb8/edarb.c`

- [ ] **Line 67** — Multiple-menu structure for ARB8 will complicate the `ft_set_edit_mode` callback
- [ ] **Line 745** — ARB-specific state (face/edge edit arrays) should be in a private arb editing struct rather than globals
- [ ] **Line 1281** — `return 1` instead of `break` skips `rt_edit_process` finalisation; needs investigation
- [ ] **Line 1565** — Comment notes solid edit menu was called in MGED — determine if still needed

### `brlcad/src/librt/primitives/bspline/edbspline.c`

- [ ] **Line 22** — `TODO - this whole bspline editing logic needs a re-look`; bspline editing is fragile and may need significant rework or may be best left as a stub pointing to a dedicated editor

### `brlcad/src/librt/primitives/metaball/edmetaball.c`

- [ ] **Lines 195, 211, 220, 240** — `TODO - should we really be calling this here?` — callback invocations at possibly wrong points in the dispatch chain

### `brlcad/src/librt/primitives/pipe/edpipe.c`

- [ ] **Lines 115, 131, 164** — Same callback-placement uncertainty as metaball

### `brlcad/src/librt/primitives/nmg/ednmg.c`

- [ ] **Lines 198, 219, 240, 408** — Callback placement uncertainty
- [ ] **Line 462** — `XXX Fall through, for now`
- [ ] **Line 1011** — `XXX Should just leave desired location in s->e_mparam`
- [ ] **Line 1264** — `XXX Nothing to do here (yet)`
- [ ] **Line 1318** — `XXX Should just leave desired location`

### `brlcad/src/librt/primitives/ars/edars.c`

- [ ] **Line 166** — Callback placement uncertainty

### `brlcad/src/librt/primitives/bot/edbot.c`

- [ ] **Line 169** — Callback placement uncertainty

### `brlcad/src/librt/primitives/vol/edvol.c`

- [ ] **Line 87** — Callback placement uncertainty

---

## Per-Primitive Status

Legend: ✅ ed*.c exists | ⚠️ stub/alias | ❌ no edit support | 🔲 not yet verified

### Primitives with edit files (need per-primitive testing)

| Primitive | ed*.c | `ft_edit_desc` | Known Issues | Tested |
|-----------|-------|----------------|--------------|--------|
| **TOR** (torus) | ✅ `tor/edtor.c` | ✅ | — | 🔲 |
| **TGC** (truncated general cone) | ✅ `tgc/edtgc.c` | ✅ | e_axes_pos implemented | 🔲 |
| **ELL** (ellipsoid) | ✅ `ell/edell.c` | ✅ | — | 🔲 |
| **ARB8** (arbitrary polyhedron) | ✅ `arb8/edarb.c` | — | Multiple menu complexity, globals in private struct, see TODOs above | 🔲 |
| **ARS** (arbitrary-radius solid) | ✅ `ars/edars.c` | — | Callback placement uncertainty | 🔲 |
| **HALF** (half-space) | ✅ `half/edhalf.c` | — | — | 🔲 |
| **BSPLINE** (B-spline surface) | ✅ `bspline/edbspline.c` | — | Major TODO: needs re-look; fragile | 🔲 |
| **NMG** (n-manifold geometry) | ✅ `nmg/ednmg.c` | — | Multiple XXX items; complex | 🔲 |
| **EBM** (extruded bitmap) | ✅ `ebm/edebm.c` | ✅ | — | 🔲 |
| **VOL** (volumetric data) | ✅ `vol/edvol.c` | ✅ | Callback placement uncertainty | 🔲 |
| **PIPE** | ✅ `pipe/edpipe.c` | ✅ | Callback placement uncertainty | 🔲 |
| **PART** (particle) | ✅ `part/edpart.c` | ✅ | — | 🔲 |
| **RPC** (right parabolic cylinder) | ✅ `rpc/edrpc.c` | ✅ | — | 🔲 |
| **RHC** (right hyperbolic cylinder) | ✅ `rhc/edrhc.c` | ✅ | — | 🔲 |
| **EPA** (elliptical paraboloid) | ✅ `epa/edepa.c` | ✅ | — | 🔲 |
| **EHY** (elliptical hyperboloid) | ✅ `ehy/edehy.c` | ✅ | — | 🔲 |
| **ETO** (elliptic torus) | ✅ `eto/edeto.c` | ✅ | — | 🔲 |
| **GRIP** | ✅ `grip/edgrip.c` | — | — | 🔲 |
| **DSP** (displacement map) | ✅ `dsp/eddsp.c` | ✅ | — | 🔲 |
| **SKETCH** | ✅ `sketch/edsketch.c` | — | — | 🔲 |
| **EXTRUDE** | ✅ `extrude/edextrude.c` | — | — | 🔲 |
| **CLINE** | ✅ `cline/edcline.c` | ✅ | — | 🔲 |
| **BOT** (bag of triangles) | ✅ `bot/edbot.c` | — | Callback placement uncertainty | 🔲 |
| **SUPERELL** (superellipsoid) | ✅ `superell/edsuperell.c` | ✅ | — | 🔲 |
| **METABALL** | ✅ `metaball/edmetaball.c` | — | Callback placement uncertainty | 🔲 |
| **HYP** (hyperboloid) | ✅ `hyp/edhyp.c` | ✅ | — | 🔲 |
| **DATUM** | ✅ `datum/eddatum.c` | — | — | 🔲 |

### Primitives without dedicated edit files

| Primitive | Edit Status | Notes |
|-----------|-------------|-------|
| **SPH** (sphere) | ⚠️ Uses ELL | SPH is a degenerate ELL; edit delegates to `edell.c` logic via `EDOBJ[ID_SPH]` table entry with ELL function pointers |
| **REC** (right elliptic cylinder) | ⚠️ Uses TGC | REC is a degenerate TGC; same pattern as SPH/ELL |
| **POLY** (polygon) | ⚠️ Minimal | Legacy type; `edtable.cpp` has `EDIT_DECLARE_INTERFACE(pg)` but no `pg/edpg.c` found yet |
| **ARBN** (arbitrary-norm polyhedron) | ❌ No edit | `EDIT_DECLARE_INTERFACE(arbn)` declared but no `arbn/edarbn.c`; ARBN editing not implemented in vanilla either |
| **BREP** (boundary representation) | ❌ No interactive edit | BREP editing requires specialized tooling; out of scope for basic prim editing |
| **HF** (height field) | ❌ No edit | Legacy type, likely not worth implementing |
| **HRT** (heart) | ❌ No edit | No `hrt/edhrt.c`; requires implementation |
| **REVOLVE** | ❌ No edit | No `revolve/edrevolve.c`; requires implementation |
| **PNTS** (point cloud) | ❌ No edit | Point cloud editing not yet implemented |
| **ANNOT** (annotation) | ❌ No edit | Annotation editing not yet implemented |
| **JOINT** | ❌ No edit | Joint editing not yet implemented |
| **SUBMODEL** | ❌ No edit | External reference; editing not applicable |
| **SCRIPT** | ❌ No edit | Internal use only |
| **COMB** (combination/boolean) | ⚠️ Matrix edit | Object edit (oed) of combs uses matrix editing path; no primitive param edit |

---

## Testing Methodology

### Goals for Each Primitive

For each primitive, verify the following in the **reworked MGED** matches the **vanilla MGED**:

1. **`make <prim>` then `sed`** — solid edit mode activates correctly
2. **Edit menu appears** — the faceplate menu shows the correct options
3. **Menu item selection** — selecting each menu option sets the correct edit flag/mode
4. **Keyboard parameter entry** — typing a value and pressing Enter applies it correctly
5. **Mouse drag (XY)** — dragging the primitive in the viewport updates it live
6. **Primitive labels** — vertex/parameter labels appear correctly on the wireframe
7. **Keypoint display** — the editing keypoint is displayed in the correct location
8. **Edit axes** — the editing axes appear at the correct position
9. **`accept`/`reject`** — accepting writes changes to the database; rejecting restores original
10. **`oed` (object edit)** — matrix editing mode works correctly (translate, rotate, scale)
11. **`tedit`** — text edit of parameters writes/reads correctly
12. **Wireframe update** — the wireframe updates live during editing

### Test Script Template (non-interactive)

Create a `.mged` script for each primitive testing basic edit workflows:

```tcl
# Example: test_ell_edit.mged
opendb /tmp/test_prim.g y
make ell ell.s
sed ell.s
# Set edit mode to scale A
p 50
accept
# Verify parameters changed
l ell.s
quit
```

Run with:
```bash
$REPO_ROOT/brlcad_build/bin/mged -c /tmp/test_prim.g < test_ell_edit.mged
```

### Interactive Test Procedure (Xvfb)

```bash
# 1. Start virtual display
Xvfb :99 -screen 0 1280x1024x24 &
export DISPLAY=:99

# 2. Create test database
$REPO_ROOT/brlcad_build/bin/mged -c /tmp/test.g <<EOF
make <prim> test.s
B test.s
EOF

# 3. Launch interactive MGED for visual comparison
$REPO_ROOT/brlcad_build/bin/mged /tmp/test.g &
$REPO_ROOT/brlcad_vanilla_build/bin/mged /tmp/test.g &

# 4. Exercise editing, take screenshots
DISPLAY=:99 scrot /tmp/rework_<prim>.png
DISPLAY=:99 scrot /tmp/vanilla_<prim>.png
```

---

## Per-Primitive Test Checklist

For each primitive below: open both reworked and vanilla MGED, create an instance with
`make`, display it with `B`, enter solid-edit mode with `sed`, exercise all menu options,
test mouse dragging, and verify parameter labels and keypoints.

### Phase 1: Simple Parametric Primitives (highest priority)

These have the simplest edit paths and are most likely to have regressions caught quickly.

- [ ] **TOR** — R1 (major radius), R2 (minor radius)
  - Menu: Set R1, Set R2
  - Mouse: drag R1 and R2 via viewport
  - Labels: R1 circle, R2 circle visible at correct positions

- [ ] **ELL** (and SPH) — A, B, C semi-axes
  - Menu: Set A, Set B, Set C, Set A,B,C
  - Mouse: drag each axis
  - Labels: A, B, C endpoints labeled

- [ ] **TGC** (and REC, CYL, RCC, etc.) — H, A, B, C, D vectors
  - Menu: scale H, scale A, rotate H, move end H, rotate A,B, etc.
  - Mouse: drag endpoint of H vector
  - e_axes_pos: verify axes appear at correct point (unique to TGC among simple prims)

- [ ] **EPA** — H, R1, R2
  - Menu: set H, R1, R2
  - Mouse: drag each

- [ ] **EHY** — H, R1, R2, c (hyperboloid factor)
  - Menu: set H, R1, R2, c
  - Mouse: drag each

- [ ] **ETO** — R (major radius), Rd (cross-section radius), rotate C, scale C
  - Menu: set R, Rd, rotate C, scale C
  - Mouse: drag each

- [ ] **RPC** — B, H, R vectors
  - Menu: set B, H, r
  - Mouse: drag each

- [ ] **RHC** — B, H, R, c (hyperboloid factor)
  - Menu: set B, H, r, c
  - Mouse: drag each

- [ ] **HYP** — H, A, B semi-axes, c (neck factor)
  - Menu: set H, scale A, scale B, set c; rotate H, rotate A
  - Mouse: drag each

- [ ] **PART** (particle) — V, H, r_v, r_h
  - Menu: set H, scale r_v (vertex end), scale r_h (tip end)
  - Mouse: drag endpoint

- [ ] **SUPERELL** — a, b, c semi-axes, n (superellipse exponent)
  - Menu: set A, B, C, A,B,C
  - Mouse: drag each axis

- [ ] **HALF** (half-space) — normal and distance
  - Menu: set normal, set distance
  - Keypoint: point on plane

### Phase 2: Complex / Structural Primitives

- [ ] **ARB8** (and ARB7, ARB6, ARB5, ARB4) — faces, edges, vertices
  - Menu: hierarchical (ARB main menu → ARB-specific menu)
  - Operations: move face, move edge, rotate face, permute
  - Verify that ARB type (ARB4..ARB8) is detected correctly and right sub-menu appears
  - Face rotation (`f_eqn`): enter equation for a face
  - Known issue: private state (face/edge arrays) uses globals currently

- [ ] **ARS** (arbitrary-radius solid) — curves and columns of points
  - Menu: pick menu, edit menu
  - Operations: pick point, move point, move curve, move column, delete curve/col, dup curve/col
  - Mouse: pick nearest point on wireframe

- [ ] **PIPE** — segments (bend/linear/cylinder segments with radii and endpoints)
  - Menu: pick point, next/prev segment, add/delete/move segment
  - Mouse: pick nearest pipe segment endpoint
  - Multiple sequential point selections

- [ ] **METABALL** — control points with field strength
  - Menu: pick point, add/delete/move control point, set field strength, set method/threshold
  - Mouse: pick nearest control point

- [ ] **EXTRUDE** (extrusion of a sketch)
  - Menu: scale H, move H, rotate H, set sketch name, scale A/B, rotate A/B
  - Special: setting sketch name opens a filename dialog

- [ ] **SKETCH** (2D sketch used by EXTRUDE/REVOLVE)
  - Menu: pick vertex, add/delete/move vertex, add/append bezier segments
  - Mouse: pick nearest sketch vertex

- [ ] **BOT** (bag of triangles)
  - Menu: pick vertex/edge/triangle, move vertex/edge/triangle, set mode/orientation/flags
  - Mouse: ray-cast pick (multi-hit for triangles)
  - Special: BOT callbacks (mode, orient, thick, flags, fmode) via `mged_impl.cpp`

- [ ] **NMG** (n-manifold geometry)
  - Menu: debug toggle, pick vertex/edge/loop/shell, move edge, extrude loop
  - Complex state machine; multiple XXX items in ednmg.c

### Phase 3: Bitmap/File-Referenced Primitives

- [ ] **EBM** (extruded bitmap)
  - Menu: set file name, set file size (X×Y), set extrusion depth
  - Special: file name entry via dialog

- [ ] **VOL** (volumetric)
  - Menu: set file name, set file size, threshold lo/hi
  - Special: file name entry via dialog

- [ ] **DSP** (displacement map)
  - Menu: set file name, set file size, set smooth flag, set data source, scale X/Y/Alt
  - Special: file name entry and data source toggle

### Phase 4: Miscellaneous / Less Common

- [ ] **CLINE** — scale H, move H, scale radius, scale thickness
- [ ] **GRIP** — C (center), N (normal), L (length)
- [ ] **DATUM** — point/line/plane editing
- [ ] **BSPLINE** — control point picking and moving (fragile; see TODO above)
- [ ] **HRT** — heart shape; currently no ed*.c, needs one or stub
- [ ] **REVOLVE** — revolution solid; currently no ed*.c, needs one or stub

---

## Object Edit (oed) Testing

In addition to solid edit (`sed`), object edit (`oed`) must be validated for all geometry types
since it exercises a different code path (matrix manipulation rather than primitive params).

For each primitive, verify:

- [ ] `oed <comb> <path>/<prim>` activates object edit mode
- [ ] Translate (be_o_x, be_o_y, be_o_xy, be_o_z buttons)
- [ ] Rotate (be_o_rotate button)
- [ ] Scale (be_o_scale, be_o_xscale, be_o_yscale, be_o_zscale buttons)
- [ ] `accept` writes matrix to database
- [ ] `reject` restores original matrix
- [ ] Mouse drag in oed mode

The `objedit_mouse()` function in `edsol.c` delegates to `(*EDOBJ[idb_type].ft_edit_xy)(MEDIT(s), mousevec)`.
The `be_o_*` button handlers set `MEDIT(s)->edit_flag` to `RT_MATRIX_EDIT_*` values so
`edit_generic_xy()` handles scale/translate correctly.

---

## Regression Test Plan

### Automated (non-interactive)

Use MGED's `-c` (classic/no-GUI) mode to drive scripted tests and compare output:

```bash
# Verify accept writes correct parameters
$REPO_ROOT/brlcad_build/bin/mged -c /tmp/test.g <<EOF 2>&1
make ell e1.s
sed e1.s
# set scale A to 50mm
p 50
accept
l e1.s
EOF
```

Compare the `l <prim>` output of reworked vs vanilla MGED for each parameter change
to catch numeric discrepancies.

### Adding regress/ Tests

New regression tests should follow the pattern in `brlcad/regress/mged/`:
- Create a `.mged` Tcl script with `puts "*** Testing ... ***"` markers
- Add to `brlcad/regress/mged/regress-mged.cmake.in`
- Run via `cmake --build brlcad_build --target regress-mged`

---

## Progress Summary

### Infrastructure

- [x] `rt_edit` struct and core API defined (`include/rt/edit.h`)
- [x] `rt_edit_map` callback dispatch system implemented (`src/librt/edit.cpp`)
- [x] Generic edit operations in `edgeneric.c` (sscale, stra, srot, abs_tra, keypoint, matrix-edit)
- [x] `EDOBJ[]` table in `edtable.cpp` wiring all primitives to their callbacks
- [x] MGED callback registration in `mged_impl.cpp` / `mged_impl.h`
- [x] `mged_state_clbk_set/get` and `mged_edit_clbk_sync` implemented
- [ ] Eliminate `es_edclass` and `es_type` from `mged_edit_state` (mged.h lines 147-148)
- [ ] Resolve remaining globals in `edsol.c` (FIXME on line 53)
- [ ] Fix `ft_xform` dbip threading (edsol.c line 665)
- [ ] Move MATRIX_EDIT XY path to `ft_edit_xy` callbacks (edsol.c line 765)
- [ ] Fix output to use `bu_vls` / callback (edsol.c lines 1963, 2022)
- [ ] Migrate un-migrated functions from `buttons.c` to libged (buttons.c line 1043)

### Primitives (ed*.c exists)

- [ ] TOR – verify all edit operations
- [ ] TGC – verify all edit operations
- [ ] ELL – verify all edit operations
- [ ] ARB8 – verify all edit operations (complex; multiple known TODOs)
- [ ] ARS – verify all edit operations
- [ ] HALF – verify all edit operations
- [ ] BSPLINE – needs re-examination; mark as low-priority / known fragile
- [ ] NMG – verify; complex; multiple XXX items
- [ ] EBM – verify all edit operations
- [ ] VOL – verify all edit operations
- [ ] PIPE – verify all edit operations
- [ ] PART – verify all edit operations
- [ ] RPC – verify all edit operations
- [ ] RHC – verify all edit operations
- [ ] EPA – verify all edit operations
- [ ] EHY – verify all edit operations
- [ ] ETO – verify all edit operations
- [ ] GRIP – verify all edit operations
- [ ] DSP – verify all edit operations
- [ ] SKETCH – verify all edit operations
- [ ] EXTRUDE – verify all edit operations
- [ ] CLINE – verify all edit operations
- [ ] BOT – verify all edit operations
- [ ] SUPERELL – verify all edit operations
- [ ] METABALL – verify all edit operations
- [ ] HYP – verify all edit operations
- [ ] DATUM – verify all edit operations

### Primitives (no ed*.c, need assessment)

- [ ] SPH – confirm ELL edit path works for sphere case
- [ ] REC – confirm TGC edit path works for right-elliptic-cylinder case
- [ ] ARBN – determine if interactive editing is needed/desired
- [ ] HRT – implement minimal ed*.c or document as out-of-scope
- [ ] REVOLVE – implement minimal ed*.c or document as out-of-scope
- [ ] BREP – document as requiring specialized editor, out-of-scope for basic prim edit

---

## Session Notes

*Add dated notes here as work progresses across sessions.*

### Session 1 (2026-03-06)

- Explored repository structure; confirmed `brlcad_vanilla/` is present as ground-truth
- Inventoried all `ed*.c` files in `brlcad/src/librt/primitives/` (27 primitive-specific files)
- Catalogued all code-level TODOs/FIXMEs in MGED and librt editing code
- Created this MGED_TODO.md tracking document
- Key findings:
  - The `EDOBJ[]` table in `edtable.cpp` registers all primitives but many entries have NULL for optional callbacks (`edit_desc`, `e_axes_pos`, etc.)
  - `mged_impl.cpp` registers MGED-specific callbacks for BOT (6 callbacks), NMG (1), EXTRUDE (1), ARB8 (1), plus 6 generic callbacks
  - `mged_edit_state` still carries `es_edclass` and `es_type` which should eventually be removed
  - `edsol.c` is 2139 lines (vs 7749 in vanilla) — the bulk of primitive-specific code has been migrated

---
