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

## Completed work (as of this session)

- [x] Added `mged_impl.cpp` / `mged_impl.h` – callback-map registration helpers.
- [x] Registered MGED callbacks for all ECMD_* callback hooks.
- [x] `sedit_menu` / `f_get_sedit_menus` replaced with `ft_menu_item()`.
- [x] `buttons.c`, `scroll.c`, `doevent.c`, `dm-generic.c`, `mged.c`:
  all generic edit_flag assignments use `rt_edit_set_edflag()`.
- [x] `menu.c`: removed ~1300 lines of primitive-specific menu arrays.
- [x] **Fixed all ECMD value mismatches in `sedit.h`**:
  - `STRANS/SSCALE/SROT/IDLE` are aliases for librt's `RT_PARAMS_EDIT_TRANS/SCALE/ROT`
    and `RT_EDIT_IDLE` (numeric values were already identical)
  - `EARB` 9→4009, `PTARB` 10→4010 (matching librt `edarb.c`)
  - `ECMD_VTRANS` 17→9017 (matching librt `edbspline.c`)
- [x] **Removed `pscale()` (~1015 lines)** from `edsol.c`.
- [x] **Removed `MENU_*` legacy constants** from `menu.h`.
- [x] **`sedit()` fully delegated**: ALL cases now call `rt_edit_process(MEDIT(s))`
  and return, including:
  - All primitive-specific ECMD cases (TGC, TOR, ELL, ARB, PIPE, BOT, etc.)
  - `SSCALE/STRANS/SROT` (librt `edit_generic` → `edit_sscale/edit_stra/edit_srot`)
  - `PTARB/EARB` (librt ARB edit → `arb_mv_pnt_to/edarb_mousevec`)
  - `ECMD_VTRANS` (librt bspline edit handles vertex translate)
  - State synced into `rt_edit` before switch: `mv_context`, `local2base`,
    `base2local`, `vp`.
- [x] **Removed `dsp_scale()` dead function** and unused local variables.
- [x] **`chgview.c` librt path enabled**: All old MGED knob functions removed,
  librt knob path always active.
- [x] **`sedit_mouse()` fully delegated** to `ft_edit_xy()`: The ~400-line switch
  replaced with `(*EDOBJ[idb_type].ft_edit_xy)(MEDIT(s), mousevec)`.
- [x] **`rt_edit_set_edflag()` used consistently**: All generic edit_flag
  assignments in `buttons.c`, `doevent.c`, `mged.c`, `dm-generic.c`, `chgview.c`,
  `edarb.c` now use `rt_edit_set_edflag()` to keep `edit_mode` in sync.
- [x] **edit_mode save/restore**: All save_edflag/restore patterns in event
  handlers also save/restore `edit_mode`.
- [x] **Simplified SEDIT_* macros**: `SEDIT_SCALE`, `SEDIT_TRAN`, `SEDIT_ROTATE`
  replaced with simple `edit_mode` checks (from ~100-line ||–chains to 3–8 lines).
- [x] Build confirmed clean (`[100%] Built target mged`).

---

## Next Steps (in rough priority order)

### 1. Remove `edarb.c`, `edars.c`, `edpipe.c` from MGED

The Tcl commands `f_extrude`, `f_mirface`, `f_edgedir`, `f_permute`
(defined in `edarb.c`) need to be moved to `libged` or kept separately.
Once those are gone, these files can be removed from `CMakeLists.txt`.

### 2. Remove `STRANS/SSCALE/SROT/IDLE/EARB/PTARB/ECMD_VTRANS` aliases from `sedit.h`

These are now just aliases for the librt values. Code that uses these names
could be updated to use the librt names directly, and then the aliases removed.

### 3. Remove `update_edit_absolute_tran` from `edsol.c`

This function duplicates librt's `edit_abs_tra`. It's still used in
`objedit_mouse()`. Once `objedit_mouse()` also delegates to librt, it can be removed.

### 4. `init_sedit` / `init_oedit_guts` cleanup

Verify that `mged_edit_clbk_sync()` is called appropriately after init so
that librt's callback maps are in sync with MGED state.

### 5. Remove `SEDIT_PICK` macro

Once all pick operations go through librt's `RT_PARAMS_EDIT_PICK` edit_mode
(or are fully handled inside `ft_edit_xy`), simplify or remove `SEDIT_PICK`.

---

## File summary

| File | Status |
|------|--------|
| `src/mged/menu.c` | Updated – primitive arrays removed, types updated |
| `src/mged/menu.h` | Updated – legacy MENU_* removed, new type |
| `src/mged/mged.h` | Updated – struct menu_item removed, menu.h included |
| `src/mged/mged_dm.h` | Updated – ms_menus type updated |
| `src/mged/buttons.c` | Updated – rt_edit_set_edflag used for generic edits |
| `src/mged/scroll.c` | Updated – sl_halt/toggle_scroll signatures updated |
| `src/mged/doevent.c` | Updated – rt_edit_set_edflag, edit_mode save/restore |
| `src/mged/dm-generic.c` | Updated – rt_edit_set_edflag, edit_mode save/restore |
| `src/mged/mged.c` | Updated – rt_edit_set_edflag, edit_mode save/restore |
| `src/mged/cmd.c` | Updated – mged_print_result callback signature |
| `src/mged/edsol.c` | Fully delegated: sedit() delegates ALL cases to rt_edit_process(); sedit_mouse() delegates to ft_edit_xy() |
| `src/mged/sedit.h` | Fully updated: ECMD values correct, SEDIT_* macros use edit_mode |
| `src/mged/chgview.c` | Updated – rt_edit_set_edflag, edit_mode save/restore, librt knob path active |
| `src/mged/edarb.c` | Updated – rt_edit_set_edflag(IDLE) used; Tcl cmds still TODO |
| `src/mged/edars.c` | **TODO** – remove after migrating to librt |
| `src/mged/edpipe.c` | **TODO** – remove after migrating to librt |
