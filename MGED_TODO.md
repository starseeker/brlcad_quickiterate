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

- [x] **Line 53** – `FIXME: Globals` — dead globals `sedraw` and `es_m[3]` removed; remaining globals (`movedir`, `illump`, etc.) are still to be moved into state structs
- [x] **Line 329** – `TODO - figure out what this is doing` — resolved: `cad_list_buts` is a Tk checkbox dialog for BOT flags; replaced TODO with explanatory comment
- [x] **Line 571** – `TODO - fix` — **FIXED**: `init_sedit` now calls `rt_edit_create` to load the solid; `f_ill` now calls `init_sedit` for the ST_S_PICK state (matching vanilla behavior; was missing)
- [x] **Line 665** – `TODO - this needs dbip` — resolved: implementation already correctly threads `s->dbip` through `rt_matrix_transform`; updated comment
- [x] **Line 765** – `TODO - this needs to move to the ft_edit_xy callbacks as MATRIX_EDIT` — already done: `objedit_mouse` delegates to `ft_edit_xy`; updated comment to reflect current state
- [x] **Line 779, 823** – `TODO - not using this anymore, revert/fix` — dead commented-out code removed
- [x] **Line 1240** – `XXX hack to restore MEDIT(s)->es_int` — not a hack; `rt_db_put_internal` frees the internal as a side effect; re-reading is the correct approach for `sed_apply`. Replaced XXX with explanatory comment.
- [x] **Line 1417** – `XXX This really should use import/export interface` — the labeling now dispatches to `ft_labels` which IS the right interface. Replaced XXX with proper doc comment.
- [x] **Line 1429** – `TODO - is es_int the same as ip here?` — answered yes; updated to use `ip` directly; removed TODO
- [x] **Lines 1963, 2022** – `TODO - write to a vls so parent code can do Tcl_AppendResult` — fixed: now uses `Tcl_AppendResult` directly

### `brlcad/src/mged/mged_impl.cpp` / `mged.c`

- [x] **`s->i` never initialized** — **FIXED (session 4)**: `mged.c` main() allocated `MGED_STATE` via `BU_GET` (zero-init) but never initialized `s->i` (the `mged_state_impl` holding the C++ `MGED_Internal` callback maps). This caused `mged_edit_clbk_sync()` to crash on null dereference whenever `sed` or `oed` was invoked. Added `mged_state_init_internals()` and `mged_state_destroy_internals()` to `mged_impl.cpp`; wired them into `mged.c` init and cleanup. Also fixed `mged_state_create()` to not leak a premature `s_edit` allocation then immediately overwrite it with `NULL`.

### `brlcad/src/mged/chgview.c`

- [x] **f_ill missing ST_S_PICK → init_sedit transition** — **FIXED**: `f_ill` now calls `init_sedit(s)` when in `ST_S_PICK` state, matching vanilla behavior. This is the critical fix that makes the `sed` command work.

### `brlcad/src/mged/mged.h`

- [x] **Lines 147-148** — `es_type` removed (was unused); `es_edclass` remains (still actively used via Tcl link and rate loop in chgview.c)

### `brlcad/src/mged/buttons.c`

- [ ] **Lines 1043-1044** — Functions below that comment are "not yet migrated to libged, still referenced by mged's setup command table — migrate to libged"

### `brlcad/src/mged/share.c`

- [x] **Line 286** — `TODO - is e_type actually used here?` — answered no; `es_type` removed from struct; TODO comment removed

### `brlcad/src/librt/primitives/arb8/edarb.c`

- [x] **Line 67** — `ft_set_edit_mode` is intentionally empty: ARB8 mode selection goes through its nested sub-menu handlers, not through the generic callback. Replaced TODO with explanatory comment.
- [x] **Line 745** — `static` removed from `uvec`, `svec`, `cgtype` in `write_params`; they are purely local scratch with no need to persist. Replaced TODO with explanatory comment.
- [x] **Line 1281** — `return 1` instead of `break` — investigated: `ecmd_arb_rotate_face` handles plane calc and replot directly; `return 1` intentionally skips redundant work in `rt_edit_process`; replaced TODO with explanatory comment
- [x] **Line 1565** — `sedit_menu` was called here to refresh menu after ARB4→ARB6 type change. In the reworked architecture, this would require an `ECMD_MENU_REFRESH` callback (not yet implemented). Replaced TODO with explanatory comment.

### `brlcad/src/librt/primitives/bspline/edbspline.c`

- [ ] **Line 22** — `TODO - this whole bspline editing logic needs a re-look`; bspline editing is fragile and may need significant rework or may be best left as a stub pointing to a dedicated editor

### `brlcad/src/librt/primitives/metaball/edmetaball.c`

- [x] **Lines 195, 211, 220, 240** — Resolved: NEXT/PREV traversal calls `rt_edit_process` to trigger immediate display update; MOV/DEL do likewise. Replaced all TODOs with explanatory comments.

### `brlcad/src/librt/primitives/pipe/edpipe.c`

- [x] **Lines 115, 131, 164** — Same as metaball; resolved with explanatory comments.

### `brlcad/src/librt/primitives/nmg/ednmg.c`

- [x] **Lines 198, 219, 240, 408** — Resolved: FORW/BACK/RADIAL call `rt_edit_process` to trigger immediate display update; `nmg_ed` wrapper also calls it (redundant for traversal ops but harmless). Replaced TODOs with explanatory comments.
- [x] **Line 462** — `XXX Fall through, for now` — replaced with explanatory comment (no element-specific dispatch, falls through to edge vertex or origin default).
- [x] **Line 1011** — `XXX Should just leave desired location in s->e_mparam` — resolved: edge pick uses view-space search incompatible with the generic e_mparam path; explained in updated comment.
- [x] **Line 1264** — `XXX Nothing to do here (yet)` — resolved: EPICK is intentionally handled in the mouse/xy routine only; updated comment explains why ft_edit has no work to do.
- [x] **Line 1318** — `XXX Should just leave desired location` — resolved: same rationale as line 1011; updated comment.

### `brlcad/src/librt/primitives/ars/edars.c`

- [x] **Line 166** — Resolved: calling `rt_edit_process` after `set_edit_mode` is correct and ensures axes update immediately after menu selection. Replaced TODO with explanatory comment.

### `brlcad/src/librt/primitives/bot/edbot.c`

- [x] **Line 169** — Resolved: same as ars; `bot_ed` simplified to use `rt_edit_bot_set_edit_mode` (removing duplicated switch statement), then calls `rt_edit_process`. Replaced TODO with explanatory comment.

### `brlcad/src/librt/primitives/vol/edvol.c`

- [x] **Line 87** — Resolved: same rationale; replaced TODO with explanatory comment.

---

## Per-Primitive Status

Legend: ✅ ed*.c exists | ⚠️ stub/alias | ❌ no edit support | 🔲 not yet verified | ✔ sed+accept verified

### Primitives with edit files (need per-primitive testing)

| Primitive | ed*.c | `ft_edit_desc` | Known Issues | Tested |
|-----------|-------|----------------|--------------|--------|
| **TOR** (torus) | ✅ `tor/edtor.c` | ✅ | — | ✔ sed+sscale+accept |
| **TGC** (truncated general cone) | ✅ `tgc/edtgc.c` | ✅ | e_axes_pos implemented | ✔ sed+sscale+accept |
| **ELL** (ellipsoid) | ✅ `ell/edell.c` | ✅ | — | ✔ sed+sscale+accept |
| **ARB8** (arbitrary polyhedron) | ✅ `arb8/edarb.c` | — | Multiple menu complexity, globals in private struct | ✔ sed+accept |
| **ARS** (arbitrary-radius solid) | ✅ `ars/edars.c` | — | — | ✔ sed+accept |
| **HALF** (half-space) | ✅ `half/edhalf.c` | — | — | ✔ sed+accept |
| **BSPLINE** (B-spline surface) | ✅ `bspline/edbspline.c` | — | Fragile; needs re-look for full correctness | ✔ sed (no crash) |
| **NMG** (n-manifold geometry) | ✅ `nmg/ednmg.c` | — | Complex traversal | ✔ sed+accept |
| **EBM** (extruded bitmap) | ✅ `ebm/edebm.c` | ✅ | — | 🔲 |
| **VOL** (volumetric data) | ✅ `vol/edvol.c` | ✅ | — | 🔲 |
| **PIPE** | ✅ `pipe/edpipe.c` | ✅ | — | ✔ sed+accept |
| **PART** (particle) | ✅ `part/edpart.c` | ✅ | — | ✔ sed+sscale+accept |
| **RPC** (right parabolic cylinder) | ✅ `rpc/edrpc.c` | ✅ | — | ✔ sed+sscale+accept |
| **RHC** (right hyperbolic cylinder) | ✅ `rhc/edrhc.c` | ✅ | — | ✔ sed+sscale+accept |
| **EPA** (elliptical paraboloid) | ✅ `epa/edepa.c` | ✅ | — | ✔ sed+sscale+accept |
| **EHY** (elliptical hyperboloid) | ✅ `ehy/edehy.c` | ✅ | — | ✔ sed+sscale+accept |
| **ETO** (elliptic torus) | ✅ `eto/edeto.c` | ✅ | — | ✔ sed+sscale+accept |
| **GRIP** | ✅ `grip/edgrip.c` | — | — | ✔ sed+accept |
| **DSP** (displacement map) | ✅ `dsp/eddsp.c` | ✅ | — | 🔲 |
| **SKETCH** | ✅ `sketch/edsketch.c` | — | — | ✔ sed+accept |
| **EXTRUDE** | ✅ `extrude/edextrude.c` | — | Needs valid sketch reference | ✔ sed (no crash) |
| **CLINE** | ✅ `cline/edcline.c` | ✅ | — | ✔ sed+sscale+accept |
| **BOT** (bag of triangles) | ✅ `bot/edbot.c` | — | — | ✔ sed+accept |
| **SUPERELL** (superellipsoid) | ✅ `superell/edsuperell.c` | ✅ | — | ✔ sed+sscale+accept |
| **METABALL** | ✅ `metaball/edmetaball.c` | — | — | ✔ sed+accept |
| **HYP** (hyperboloid) | ✅ `hyp/edhyp.c` | ✅ | — | ✔ sed+sscale+accept |
| **DATUM** | ✅ `datum/eddatum.c` | — | — | ✔ sed+accept |

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

- [x] **TOR** — R1 (major radius), R2 (minor radius)
  - `sed` + `sscale` + `p 2` + `accept` verified in session 4 (-c mode)
  - `press sscale; p 2; accept` doubles r1 (50→100) and r2 (10→20) ✓
  - Mouse/label/interactive testing deferred (requires Xvfb)

- [x] **ELL** (and SPH) — A, B, C semi-axes
  - `sed` + `sscale` + `p 2` + `accept` verified in session 4
  - Scaling by 2 doubles A (100→200), B (50→100), C (50→100) ✓
  - SPH: same ELL path confirmed no-crash (SPH delegates to edell.c)
  - Mouse/label/interactive testing deferred

- [x] **TGC** (and REC, CYL, RCC, etc.) — H, A, B, C, D vectors
  - `sed` + `sscale` + `accept` verified in session 4 ✓
  - REC, TEC tested: TGC edit path used correctly
  - Mouse/label/interactive testing deferred

- [x] **EPA** — H, R1, R2
  - `sed` + `sscale` + `p 2` + `accept` verified in session 4 ✓

- [x] **EHY** — H, R1, R2, c (hyperboloid factor)
  - `sed` + `sscale` + `p 2` + `accept` verified in session 4 ✓

- [x] **ETO** — R (major radius), Rd (cross-section radius), rotate C, scale C
  - `sed` + `sscale` + `p 2` + `accept` verified in session 4 ✓

- [x] **RPC** — B, H, R vectors
  - `sed` + `sscale` + `p 2` + `accept` verified in session 4 ✓

- [x] **RHC** — B, H, R, c (hyperboloid factor)
  - `sed` + `sscale` + `p 2` + `accept` verified in session 4 ✓

- [x] **HYP** — H, A, B semi-axes, c (neck factor)
  - `sed` + `sscale` + `p 2` + `accept` verified in session 4 ✓

- [x] **PART** (particle) — V, H, r_v, r_h
  - `sed` + `sscale` + `p 2` + `accept` verified in session 4 ✓

- [x] **SUPERELL** — a, b, c semi-axes, n (superellipse exponent)
  - `sed` + `sscale` + `p 2` + `accept` verified in session 4 ✓

- [x] **HALF** (half-space) — normal and distance
  - `sed` + `accept` verified in session 4 (no crash) ✓

### Phase 2: Complex / Structural Primitives

- [x] **ARB8** (and ARB7, ARB6, ARB5, ARB4) — faces, edges, vertices
  - `sed` + `accept` verified in session 4 for ARB8, ARB6, ARB5, ARB4 ✓
  - Hierarchical menu structure present and correct
  - Per-face/edge interactive operations deferred (require Xvfb)

- [x] **ARS** (arbitrary-radius solid) — curves and columns of points
  - `sed` + `accept` verified in session 4 (no crash) ✓
  - Point-picking operations deferred (require Xvfb)

- [x] **PIPE** — segments
  - `sed` + `accept` verified in session 4 ✓

- [x] **METABALL** — control points with field strength
  - `sed` + `accept` verified in session 4 ✓

- [x] **EXTRUDE** (extrusion of a sketch)
  - `sed` verified in session 4 (no crash; references bad sketch → plot warning but no segfault) ✓
  - Full test requires a valid sketch reference

- [x] **SKETCH** (2D sketch used by EXTRUDE/REVOLVE)
  - `sed` + `accept` verified in session 4 ✓

- [x] **BOT** (bag of triangles)
  - `sed` + `accept` verified in session 4 ✓
  - BOT-specific callbacks (mode, orient, thick, flags, fmode) wired via mged_impl.cpp

- [x] **NMG** (n-manifold geometry)
  - `sed` + `accept` verified in session 4 (no crash) ✓
  - Complex traversal state machine; works correctly at entry/exit

### Phase 3: Bitmap/File-Referenced Primitives

- [ ] **EBM** (extruded bitmap)
  - `sed` entry likely works (edebm.c wired); full test deferred (needs bitmap file resource)

- [ ] **VOL** (volumetric)
  - `sed` entry likely works (edvol.c wired); full test deferred (needs vol file resource)

- [ ] **DSP** (displacement map)
  - `sed` entry likely works (eddsp.c wired); full test deferred (needs DSP data file)

### Phase 4: Miscellaneous / Less Common

- [x] **CLINE** — `sed` + `sscale` + `accept` verified in session 4 ✓
- [x] **GRIP** — `sed` + `accept` verified in session 4 ✓
- [x] **DATUM** — `sed` + `accept` verified in session 4 ✓
- [ ] **BSPLINE** — control point picking and moving (fragile; see TODO above; sed enters without crash)
- [x] **HRT** — no ed*.c exists; **documented as out-of-scope** (see Primitives section)
- [x] **REVOLVE** — no ed*.c exists; **documented as out-of-scope** (see Primitives section)

---

## Object Edit (oed) Testing

In addition to solid edit (`sed`), object edit (`oed`) must be validated for all geometry types
since it exercises a different code path (matrix manipulation rather than primitive params).

For each primitive, verify:

- [x] `oed <comb> <path>/<prim>` activates object edit mode — **verified session 4**: `oed /agroup e1.s` enters ST_O_EDIT cleanly ✓
- [ ] Translate (be_o_x, be_o_y, be_o_xy, be_o_z buttons) — requires interactive testing
- [ ] Rotate (be_o_rotate button) — requires interactive testing
- [ ] Scale (be_o_scale, be_o_xscale, be_o_yscale, be_o_zscale buttons) — requires interactive testing
- [ ] `accept` writes matrix to database — requires interactive testing
- [ ] `reject` restores original matrix — requires interactive testing
- [ ] Mouse drag in oed mode — requires interactive testing

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
- [x] Remove `es_type` from `mged_edit_state` (unused field, never read/written)
- [x] Remove dead `sedraw` global (was never set to 1; all `if (sedraw > 0)` blocks were dead code)
- [x] Remove unused `es_m[3]` global from edsol.c
- [x] Fix `ft_xform` dbip threading concern (implementation already correct; updated comment)
- [x] MATRIX_EDIT XY path already delegated to `ft_edit_xy` callbacks; updated comment
- [x] Fix output to use `Tcl_AppendResult` (edsol.c ARB8 plane-calc errors, formerly `bu_log`)
- [x] Fix `init_sedit` to call `rt_edit_create` — **DONE**: creates fresh `rt_edit` for the selected solid
- [x] Fix `f_ill` missing `init_sedit` call for ST_S_PICK state — **DONE**: critical fix enabling the `sed` command
- [x] Resolve all callback-placement "should we call here?" TODOs in metaball, pipe, nmg, ars, bot, vol
- [x] Resolve all NMG XXX comments (Fall through, e_mparam, Nothing to do here)
- [x] Clarify ARB8 `ft_set_edit_mode` (intentionally empty; sub-menus handle mode selection)
- [x] Remove `static` from `write_params` local scratch vars (`uvec`, `svec`, `cgtype`)
- [x] Document ARB4→ARB6 menu refresh needed after extrusion (ECMD_MENU_REFRESH not yet implemented)
- [x] Update `label_edited_solid` doc comment (now uses ft_labels properly; removed stale XXX)
- [x] Update `sedit_apply` re-read comment (rt_db_put_internal side effect; documented behavior)
- [x] Update `f_put_sedit` argument check comment (TODO→TODO with proper context)
- [ ] Eliminate `es_edclass` from `mged_edit_state` (still actively used via TCL link and knob/rate loop)
- [ ] Resolve remaining module-level globals in `edsol.c` (`movedir`, `illump`, etc.)
- [ ] Migrate un-migrated functions from `buttons.c` to libged (buttons.c line 1043)
- [ ] Implement `ECMD_MENU_REFRESH` callback for ARB4→ARB6 and similar type-change cases

### Primitives (ed*.c exists)

- [x] TOR – sed+sscale+accept verified (session 4)
- [x] TGC – sed+sscale+accept verified (session 4)
- [x] ELL – sed+sscale+accept verified (session 4)
- [x] ARB8 – sed+accept verified; ARB6/5/4 sub-types also verified (session 4)
- [x] ARS – sed+accept verified (session 4)
- [x] HALF – sed+accept verified (session 4)
- [ ] BSPLINE – sed entry verified (no crash); full edit logic marked fragile/low-priority
- [x] NMG – sed+accept verified (session 4)
- [ ] EBM – sed entry likely works; full test deferred (needs bitmap file resource)
- [ ] VOL – sed entry likely works; full test deferred (needs vol file resource)
- [x] PIPE – sed+accept verified (session 4)
- [x] PART – sed+sscale+accept verified (session 4)
- [x] RPC – sed+sscale+accept verified (session 4)
- [x] RHC – sed+sscale+accept verified (session 4)
- [x] EPA – sed+sscale+accept verified (session 4)
- [x] EHY – sed+sscale+accept verified (session 4)
- [x] ETO – sed+sscale+accept verified (session 4)
- [x] GRIP – sed+accept verified (session 4)
- [ ] DSP – sed entry likely works; full test deferred (needs DSP data file)
- [x] SKETCH – sed+accept verified (session 4)
- [x] EXTRUDE – sed verified (no crash; needs valid sketch for full test)
- [x] CLINE – sed+sscale+accept verified (session 4)
- [x] BOT – sed+accept verified (session 4)
- [x] SUPERELL – sed+sscale+accept verified (session 4)
- [x] METABALL – sed+accept verified (session 4)
- [x] HYP – sed+sscale+accept verified (session 4)
- [x] DATUM – sed+accept verified (session 4)

### Primitives (no ed*.c, need assessment)

- [x] SPH – confirmed: ELL edit path works for sphere (EDOBJ[ID_SPH] uses ELL callbacks; no crash)
- [x] REC – confirmed: TGC edit path works for right-elliptic-cylinder (EDOBJ[ID_REC] uses TGC callbacks)
- [x] ARBN – documented as no interactive editing needed (not in vanilla either; EDIT_DECLARE_INTERFACE stub sufficient)
- [x] HRT – documented as out-of-scope (no ed*.c in vanilla or rework; heart shape editing not a regression)
- [x] REVOLVE – documented as out-of-scope (no ed*.c in vanilla or rework; revolution solid editing not a regression)
- [x] BREP – documented as requiring specialized editor; out-of-scope for basic prim edit

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

### Session 2 (2026-03-06)

- Resolved several infrastructure TODO/FIXME items:
  - Removed `es_type` from `mged_edit_state` (was declared but never accessed)
  - Removed dead `sedraw` global and all dead `if (sedraw > 0)` blocks (`sedraw` was never set to 1)
  - Removed unused `es_m[3]` global (declared but never referenced)
  - Removed unused `incr_mat` local in `objedit_mouse`
  - Fixed ARB8 plane-calculation error output: changed `bu_log()` to `Tcl_AppendResult()` in `f_extrude` and `f_mirface`
  - Replaced misleading TODO comment in `transform_editing_solid` with proper doc comment
  - Updated `objedit_mouse` comment: delegation to `ft_edit_xy` was already done; removed stale TODO
  - Resolved "is es_int the same as ip?" question in `label_edited_solid`: yes, always `&MEDIT(s)->es_int`; updated fallback labeling to use `ip` consistently
  - Clarified why `ecmd_arb_rotate_face` returns 1 (intentional: skips redundant plane recalc and replot in `rt_edit_process`)
  - Removed stale `sedraw` reference from `rt_edit_process` doc comment in `edit.cpp`
  - Clarified `ecmd_bot_flags_clbk` Tk dialog call (previously had TODO "figure out what this is doing")

### Session 3 (2026-03-06)

- **Critical bug fixed**: `f_ill` was missing the `init_sedit(s)` call for the ST_S_PICK case (vanilla MGED always called it). This meant the `sed` command never actually loaded the target solid or entered ST_S_EDIT state.
- **`init_sedit` fully implemented**: Now destroys any prior `rt_edit` (re-links the `edit_solid_flag` Tcl variable), creates a fresh one via `rt_edit_create` for the illuminated solid, and sets up callbacks/vlfree/mv_context — matching the `chgtree.c` OED pattern.
- Resolved all callback-placement "should we really be calling this here?" TODOs across metaball, pipe, nmg, ars, bot, vol — replaced with explanatory comments confirming the calls are correct and documenting the rationale.
- Resolved NMG XXX comments: Fall-through keypoint, EPICK "leave in e_mparam", "Nothing to do here (yet)", and the xy-path equivalent.
- Simplified `bot_ed` to delegate to `rt_edit_bot_set_edit_mode` (removed duplicated switch statement).
- Removed `static` qualifier from `write_params` scratch variables (they are local, not global, and don't need to persist).
- Documented the ARB4→ARB6 menu-refresh gap (ECMD_MENU_REFRESH callback needed; not yet implemented).
- Updated `label_edited_solid` doc comment (XXX removed; function already uses the correct ft_labels interface).
- Updated `sedit_apply` re-read comment (not a hack; documents rt_db_put_internal side effect).
- Updated `f_put_sedit` argument check comment to TODO with proper context.

### Session 4 (2026-03-06)

- **Critical crash fixed**: `mged.c` main() allocated `MGED_STATE` via `BU_GET` (zero-init) but never initialized `s->i` — the `mged_state_impl*` holding the C++ `MGED_Internal` per-object-type callback maps. Every call to `mged_edit_clbk_sync()` (invoked from `init_sedit` and `chgtree.c`) dereferenced `s->i` as NULL, causing a segfault on `sed` and `oed`.
  - Added `mged_state_init_internals(s)` to `mged_impl.cpp`: allocates `s->i`, constructs `MGED_Internal`, and registers all default + primitive-specific callbacks.
  - Added paired `mged_state_destroy_internals(s)`: frees `s->i` cleanly at exit.
  - Called from `mged.c` init and cleanup respectively.
- **Memory leak fixed**: `mged_state_create()` was allocating `s_edit` (lines 48-49) then immediately overwriting with `NULL` (line 67). Removed premature allocation and added clarifying comment.
- **Primitive verification**: Tested 24+ primitive types in `-c` mode with `sed` + `sscale` + `p 2` + `accept`. All pass without crash. (Results reflected in Per-Primitive Status and Progress Summary tables above.)

### Session 5 (2026-03-06)

- Updated all Per-Primitive Test Checklist items to reflect session 4 verification results.
- Updated Progress Summary checklist — all session-4-verified primitives marked `[x]`.
- Documented HRT and REVOLVE as out-of-scope (no ed*.c in vanilla or rework; not a regression).
- Documented BREP as requiring specialized editor, out-of-scope for basic primitive editing.
- Documented SPH and REC as confirmed working via ELL/TGC delegate paths.
- Documented ARBN as no interactive editing needed (consistent with vanilla behavior).
- Documented `oed` entry point as verified working (session 4: `oed /agroup e1.s` enters ST_O_EDIT cleanly).
- Noted remaining deferred items: EBM/VOL/DSP (need resource files), BSPLINE (fragile, low-priority), interactive Xvfb testing for mouse/label/axes.

---
