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
  `SROT/STRANS/SSCALE` → `RT_PARAMS_EDIT_ROT/TRANS/SCALE`.
- [x] `menu.c`: removed ~1300 lines of primitive-specific menu arrays.
- [x] **Fixed all ECMD value mismatches in `sedit.h`**:
  - All old MGED-local small values updated to match librt's scheme
  - `STRANS/SSCALE/SROT/IDLE` are now aliases for the librt values (numeric equality confirmed)
  - `EARB` 9→4009, `PTARB` 10→4010 (matching librt edarb.c)
  - `ECMD_VTRANS` 17→9017 (matching librt edbspline.c)
- [x] **Removed `pscale()` (~1015 lines)** from `edsol.c`.
- [x] **Removed `MENU_*` legacy constants** from `menu.h`.
- [x] **`sedit()` fully delegated**: All primitive-specific cases call
  `rt_edit_process(MEDIT(s))` and return.  Retained only `SSCALE`, `STRANS`,
  `ECMD_VTRANS`, `PTARB`/`EARB`, `SROT` which are MGED-specific generic edits.
- [x] **Removed `dsp_scale()` dead function** and unused local variables.
- [x] **`chgview.c` librt path enabled**: All old MGED knob functions removed,
  librt knob path always active, `mged_librt_knob_edit_apply` keeps `re->vp` current.
- [x] **`sedit_mouse()` fully delegated** to `ft_edit_xy()`: The ~400-line switch
  statement replaced with a single `(*EDOBJ[idb_type].ft_edit_xy)(MEDIT(s), mousevec)`
  call plus `sedit(s)` invocation. Librt handles all primitive-specific picking,
  moving, and scale operations.
- [x] **`rt_edit_set_edflag()` used consistently**: All generic edit_flag
  assignments in `buttons.c`, `doevent.c`, `mged.c`, `dm-generic.c`, `chgview.c`,
  `edarb.c` now use `rt_edit_set_edflag()` to keep `edit_mode` in sync with
  `edit_flag`.
- [x] `MEDIT(s)->vp` set in `init_sedit`, `init_oedit_guts`, and
  `mged_librt_knob_edit_apply`.
- [x] Build confirmed clean (`[100%] Built target mged`).

---

## Next Steps (in rough priority order)

### 1. SEDIT_* macro simplification

Now that `rt_edit_set_edflag()` is used consistently, `edit_mode` is kept in sync
with `edit_flag` for generic edits. Primitive-specific edits already set `edit_mode`
via `ft_edit_set_edit_mode()` in librt.

The long `||`-chains in `SEDIT_SCALE`, `SEDIT_TRAN`, `SEDIT_ROTATE` can be
replaced with simple `edit_mode` checks:

```c
/* target: */
#define SEDIT_SCALE   (s->global_editing_state == ST_S_EDIT && \
                       (MEDIT(s)->edit_mode == RT_PARAMS_EDIT_SCALE || \
                        MEDIT(s)->edit_mode == RT_MATRIX_EDIT_SCALE || ...))
#define SEDIT_TRAN    (s->global_editing_state == ST_S_EDIT && \
                       MEDIT(s)->edit_mode == RT_PARAMS_EDIT_TRANS)
#define SEDIT_ROTATE  (s->global_editing_state == ST_S_EDIT && \
                       MEDIT(s)->edit_mode == RT_PARAMS_EDIT_ROT)
```

**Prerequisite**: The save/restore patterns in `mged.c`, `doevent.c`, `chgview.c`,
and `dm-generic.c` save/restore `edit_flag` directly (`MEDIT(s)->edit_flag = save_edflag`).
Before simplifying the macros, these must also save/restore `edit_mode`, or be changed
to use `rt_edit_set_edflag(MEDIT(s), save_edflag)` for the restore (noting that
`rt_edit_set_edflag` sets `edit_mode = RT_EDIT_DEFAULT` for non-generic ECMDs, which
needs further handling).

### 2. `sedit()` – migrate remaining inline generic cases

The `SSCALE`, `STRANS`, `ECMD_VTRANS`, `PTARB`/`EARB`, `SROT` cases still have
inline MGED-specific code in `sedit()`. Once confirmed that `rt_edit_process()`
covers these paths (e.g. librt has `edit_generic` that handles RT_PARAMS_EDIT_*),
these can be removed.

- `SSCALE`/`SROT`/`STRANS`: librt's `edit_generic` handles RT_PARAMS_EDIT_SCALE/ROT/TRANS
- `PTARB`/`EARB`: librt's ARB edit handles these (4009/4010)
- `ECMD_VTRANS`: librt's bspline edit handles 9017

**Note**: The `SSCALE` case handles `acc_sc_sol` accumulation; `SROT` handles
`acc_rot_sol`. These may need to be migrated to librt if not already there.

### 3. Remove `edarb.c`, `edars.c`, `edpipe.c` from MGED

The Tcl commands `f_extrude`, `f_mirface`, `f_edgedir`, `f_permute`
(defined in `edarb.c`) need to be moved to `libged` or kept separately.
Once those are gone, these files can be removed from `CMakeLists.txt`.

### 4. Remove `STRANS/SSCALE/SROT/IDLE/EARB/PTARB/ECMD_VTRANS` from `sedit.h`

Once the remaining inline sedit() cases are eliminated, these obsolete
defines can be removed (currently they are aliases matching librt's values).

### 5. Remove `update_edit_absolute_tran` from `edsol.c`

This function duplicates librt's `edit_abs_tra`. It's still used in
`objedit_mouse()`. Once `objedit_mouse()` also delegates to librt, it can be removed.

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
| `src/mged/doevent.c` | Updated – rt_edit_set_edflag used |
| `src/mged/dm-generic.c` | Updated – rt_edit_set_edflag used |
| `src/mged/mged.c` | Updated – rt_edit_set_edflag used |
| `src/mged/cmd.c` | Updated – mged_print_result callback signature |
| `src/mged/edsol.c` | Updated – sedit_mouse() delegated to ft_edit_xy(); sedit() mostly delegated |
| `src/mged/sedit.h` | Updated – ECMD values corrected, STRANS/SSCALE/SROT/IDLE/EARB/PTARB/ECMD_VTRANS fixed |
| `src/mged/chgview.c` | Updated – rt_edit_set_edflag used, librt knob path active |
| `src/mged/edarb.c` | Updated – rt_edit_set_edflag(IDLE) used; Tcl cmds still TODO |
| `src/mged/edars.c` | **TODO** – remove after migrating to librt |
| `src/mged/edpipe.c` | **TODO** – remove after migrating to librt |
