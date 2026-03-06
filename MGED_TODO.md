# MGED Structural Rework: Progress and Next Steps

## Overview

This document tracks the ongoing refactoring of MGED's solid-edit machinery to
eliminate primitive-specific logic from MGED and move it into librt via the
`rt_edit` / `rt_edit_functab` (EDOBJ) callback infrastructure.

The primary reference is **`mgedrework.diff`** in the repo root, which is a
delta between an experimental BRL-CAD branch and the main tree.  It shows the
**design intent** for the refactor.  Key caveats:

* The diff has **not been vetted for correctness** – treat it as directional
  guidance, not a drop-in patch.
* **Do not** try to create and destroy per-edit `rt_edit` state on each
  operation.  Reuse the single, global MGED edit state (`MEDIT(s)`, alias for
  `s->s_edit->e`).  In MGED there is no advantage to constructing/destructing
  it per-edit; the global reuse model already in place is correct.

---

## Completed work

- [x] Added `mged_impl.cpp` / `mged_impl.h` – callback-map registration helpers.
- [x] Registered MGED callbacks for all ECMD_* callback hooks.
- [x] `sedit_menu` / `f_get_sedit_menus` replaced with `ft_menu_item()`.
- [x] `buttons.c`, `scroll.c`, `doevent.c`, `dm-generic.c`, `mged.c`:
  all generic edit_flag assignments use `rt_edit_set_edflag()`.
- [x] `menu.c`: removed ~1300 lines of primitive-specific menu arrays.
- [x] **Fixed all ECMD value mismatches** in `sedit.h`:
  - `EARB` 9→4009, `PTARB` 10→4010 (matching librt `edarb.c`)
  - `ECMD_VTRANS` 17→9017 (matching librt `edbspline.c`)
- [x] **Removed `pscale()` (~1015 lines)** from `edsol.c`.
- [x] **Removed `MENU_*` legacy constants** from `menu.h`.
- [x] **`sedit()` fully delegated**: ALL cases now call `rt_edit_process(MEDIT(s))`
  and return.
- [x] **`sedit_mouse()` fully delegated** to `ft_edit_xy()`.
- [x] **`rt_edit_set_edflag()` used consistently** and `edit_mode` save/restore added.
- [x] **Simplified `SEDIT_*` macros** to use `edit_mode`.
- [x] **Migrated MGED per-primitive globals to librt `ipe_ptr`**:
  - `es_peqn[7][4]` / `es_menu` / `newedge` → `ARB_EDIT(s)->…`
  - `es_eu` / `es_s` / `lu_copy` → `NMG_EDIT(s)->…`
  - `es_pipe_pnt` → `PIPE_EDIT(s)->…`
  - `es_metaball_pnt` → `METABALL_EDIT(s)->…`
  - `es_ars_crv` / `es_ars_col` / `es_pt` → `ARS_EDIT(s)->…`
  - `bot_verts[3]` → `BOT_EDIT(s)->…`
  - `fixv` → `ARB_EDIT(s)->fixv`
  - `spl_surfno` / `spl_ui` / `spl_vi` → `BSPLINE_EDIT(s)->…`
  - `lu_pl` / `lu_keypoint` — dead code, removed
- [x] **Removed `edars.c`, `edpipe.c` from MGED** (stubs; real code is in librt).
- [x] **Convenience macros** added: `ARB_EDIT`, `NMG_EDIT`, `PIPE_EDIT`,
  `METABALL_EDIT`, `ARS_EDIT`, `BOT_EDIT`, `BSPLINE_EDIT`.
- [x] **Removed IDLE/STRANS/SSCALE/SROT aliases** from `sedit.h`: code now uses
  `RT_EDIT_IDLE`, `RT_PARAMS_EDIT_TRANS`, `RT_PARAMS_EDIT_SCALE`,
  `RT_PARAMS_EDIT_ROT` directly.
- [x] **Exposed `EARB`/`PTARB`/`ECMD_ARB_*`** in public `include/rt/primitives/arb8.h`.
  Removed private definitions from librt `edarb.c` and MGED `sedit.h`.
- [x] **Exposed `ECMD_VTRANS`** in public `include/rt/geom.h` (near `rt_nurb_internal`).
  Removed private definitions from librt `edbspline.c` and MGED `sedit.h`.
- [x] **Exposed `struct rt_bspline_edit`** in public `include/rt/geom.h`.
  Removed private definitions from `edbspline.c` and `bspline.cpp` test.
- [x] **Exposed `struct rt_ars_edit`** in public `include/rt/primitives/ars.h`.
- [x] **Exposed `struct rt_metaball_edit`** in public `include/rt/primitives/metaball.h`.
- [x] **Removed stale externs** from `sedit.h`:
  `es_mat`, `es_invmat`, `es_keypoint`, `es_keytag`, `curr_e_axes_pos`,
  `lu_copy`, `lu_keypoint`, `lu_pl`, `es_peqn`, `es_menu`, `es_eu`, `es_s`,
  `es_pipe_pnt`, `es_metaball_pnt`.
- [x] **Fixed all cross-type `ipe_ptr` reset blocks**: `sedit_apply`,
  `sedit_reject`, `f_sedit_reset`, `init_sedit` all guard resets by `idb_type`.
- [x] Build confirmed clean: `mged`, `librt`, `rt_edit_test_arb8`, `rt_edit_test_bspline`.
- [x] **Fixed rt_ecmd_scanner mechanism**: ECMD_* defines belong in source files (ed*.c),
  not in public headers. The scanner auto-generates `rt/rt_ecmds.h` from LIBRT_PRIMEDIT_SOURCES:
  - Restored all ECMD_* `#define` blocks to their respective `ed*.c` files
  - Removed ECMD_* from all public `include/rt/primitives/*.h` headers (struct definitions kept)
  - Removed ECMD_VTRANS from `include/rt/geom.h` (restored to `edbspline.c`)
  - Removed ECMD_COMB_* from `include/rt/comb.h` (kept `struct rt_comb_edit` there)
  - Added `comb/edcomb.c` to `LIBRT_PRIMEDIT_SOURCES` so scanner picks up ECMD_COMB_*
  - Added `#include "rt/rt_ecmds.h"` to `src/mged/sedit.h`
  - Replaced non-scanned aliases in MGED: ECMD_PIPE_PICK→ECMD_PIPE_SELECT,
    ECMD_TGC_S_H_CD→ECMD_TGC_SCALE_H_CD, ECMD_TGC_S_H_V_AB→ECMD_TGC_SCALE_H_V_AB
  - EARB/PTARB kept in `arb8.h` (not ECMD_* names, not scanned)
  - Generated `rt_ecmds.h` now contains 170+ ECMD_* defines covering all primitives

---

## Next Steps (in rough priority order)

### ~~1. Remove `update_edit_absolute_tran` from `edsol.c`~~

~~This function duplicates librt's `edit_abs_tra`. It's still used in
`objedit_mouse()`. Once `objedit_mouse()` also delegates to librt, it can be removed.~~

**Done** – function removed; librt's `edit_abs_tra` (called from inside
`edit_generic_xy`) is the only implementation now.

### ~~2. `objedit_mouse()` delegation to librt~~

~~`objedit_mouse()` still contains MGED-specific logic for object-edit mouse events.
Delegates to a librt callback once the appropriate `ft_edit_xy` / objedit path exists.~~

**Done** – `objedit_mouse()` now calls `(*EDOBJ[idb_type].ft_edit_xy)(MEDIT(s), mousevec)` (matching `sedit_mouse()`).  The `be_o_*` button handlers set `MEDIT(s)->edit_flag` to the appropriate `RT_MATRIX_EDIT_*` value so `edit_generic_xy()` selects the right operation.

### ~~3. Remove `SEDIT_PICK` macro~~

~~Once all pick operations go through librt's `RT_PARAMS_EDIT_PICK` edit_mode
(or are fully handled inside `ft_edit_xy`), simplify or remove `SEDIT_PICK`.~~

**Done** – `SEDIT_PICK` simplified to a single `edit_mode == RT_PARAMS_EDIT_PICK` check.

### ~~4. Migrate `sedraw` global to `rt_edit`~~

~~`sedraw` remains as an MGED-specific global. A corresponding flag could be
added to `rt_edit` if needed to support multi-app use.~~

**Done** – `int sedraw` added to `struct rt_edit`; MGED updated to use `MEDIT(s)->sedraw`.

---

## Next Steps (after current session)

The `mgedrework.diff` reference diff contains additional refactors in `tedit.c` and `chgtree.c`:
- `tedit.c`: `writesolid`/`readsolid` still contains large primitive-specific switch blocks. The diff suggests delegating to `rt_edit_process()`/librt functab but that requires a new `ft_tedit_write`/`ft_tedit_read` pathway.
- `chgtree.c cmd_oed`: The `rt_edit_create` / `mged_edit_clbk_sync` initialization call should be added here.

---

## File summary

| File | Status |
|------|--------|
| `src/mged/menu.c` | Updated – primitive arrays removed, types updated |
| `src/mged/menu.h` | Updated – legacy MENU_* removed, new type |
| `src/mged/mged.h` | Updated – removed externs for globals now in ipe_ptr; removed es_type, es_edclass |
| `src/mged/mged_dm.h` | Updated – ms_menus type updated |
| `src/mged/buttons.c` | Updated – rt_edit_set_edflag used for generic edits; be_o_* set RT_MATRIX_EDIT_* edit_flag |
| `src/mged/scroll.c` | Updated – sl_halt/toggle_scroll signatures updated |
| `src/mged/doevent.c` | Updated – rt_edit_set_edflag, edit_mode save/restore |
| `src/mged/dm-generic.c` | Updated – rt_edit_set_edflag, edit_mode save/restore |
| `src/mged/mged.c` | Updated – rt_edit_set_edflag, edit_mode save/restore; es_edclass init removed |
| `src/mged/cmd.c` | Updated – mged_print_result callback signature; edit_class Tcl link removed |
| `src/mged/edsol.c` | Fully delegated: sedit()/sedit_mouse()/objedit_mouse() delegate to librt; es_type/es_edclass assignments removed; rt_arb_std_type called on demand |
| `src/mged/sedit.h` | Cleaned up: SEDIT_PICK simplified to edit_mode==RT_PARAMS_EDIT_PICK; EDIT_CLASS_ comment updated |
| `src/mged/chgview.c` | Updated – rt_edit_set_edflag; es_edclass reads replaced with EDIT_ROTATE/TRAN/SCALE macros |
| `src/mged/edarb.c` | Updated – uses ipe_ptr for ARB state; rt_arb_std_type on demand |
| `src/mged/facedef.c` | Updated – es_type replaced with rt_arb_std_type() local |
| `src/mged/titles.c` | Updated – pl[8+1] initialized with RT_POINT_LABELS_INIT |
| `src/mged/edars.c` | **REMOVED** from build (stub; real ARS edit in librt) |
| `src/mged/edpipe.c` | **REMOVED** from build (stub; real PIPE edit in librt) |
| `include/rt/edit.h` | Updated – int sedraw field added to struct rt_edit |
| `src/librt/edit.cpp` | Updated – sedraw initialized to 0 in rt_edit_create |
| `include/rt/primitives/ars.h` | **NEW** – exposes `struct rt_ars_edit` publicly |
| `include/rt/primitives/metaball.h` | Updated – exposes `struct rt_metaball_edit` publicly |
| `include/rt/primitives/arb8.h` | Updated – exposes EARB/PTARB/ECMD_ARB_* publicly |
| `include/rt/geom.h` | Updated – exposes ECMD_VTRANS and `struct rt_bspline_edit` publicly |
| `src/librt/primitives/ars/edars.c` | Updated – uses public `rt_ars_edit` from ars.h |
| `src/librt/primitives/metaball/edmetaball.c` | Updated – uses public `rt_metaball_edit` from metaball.h |
| `src/librt/primitives/arb8/edarb.c` | Updated – uses public EARB/PTARB/ECMD_ARB_* from arb8.h |
| `src/librt/primitives/sketch/edsketch.c` | Updated – ECMD_SKETCH_* now from public sketch.h |
| `src/librt/comb/edcomb.c` | Updated – ECMD_COMB_* and rt_comb_edit now from public comb.h |
| `include/rt/primitives/sketch.h` | Updated – exposes ECMD_SKETCH_* publicly |
| `include/rt/comb.h` | Updated – exposes ECMD_COMB_* and `struct rt_comb_edit` publicly |
| `src/librt/tests/edit/sketch.cpp` | Updated – uses public ECMD_SKETCH_* from sketch.h |
| `src/librt/tests/edit/comb.cpp` | Updated – uses public ECMD_COMB_* and rt_comb_edit from comb.h |
| `src/librt/tests/edit/nmg.cpp` | Updated – uses public ECMD_NMG_* from rt/primitives/nmg.h |
| `src/librt/tests/edit/ebm.cpp` | Updated – uses public ECMD_EBM_* from rt/primitives/ebm.h |
| `src/librt/tests/edit/dsp.cpp` | Updated – uses public ECMD_DSP_* from rt/primitives/dsp.h |
