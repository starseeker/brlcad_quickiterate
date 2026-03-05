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

## Completed work (this PR + previous commit)

- [x] Added `mged_impl.cpp` / `mged_impl.h` – callback-map registration
  helpers (`mged_state_clbk_set`, `mged_state_clbk_get`,
  `mged_edit_clbk_sync`).
- [x] Registered MGED callbacks for `ECMD_PRINT_STR`, `ECMD_VIEW_UPDATE`,
  `ECMD_EAXES_POS`, `ECMD_MENU_SET`, `ECMD_PRINT_RESULTS`, `ECMD_GET_FILENAME`
  via `mged_state_clbk_set`.
- [x] `sedit_menu` in `edsol.c`: replaced 25-case primitive switch with a
  single `EDOBJ[idb_type].ft_menu_item()` call.
- [x] `f_get_sedit_menus` in `edsol.c`: replaced the entire primitive-specific
  switch (ARB, ARS, and all others) with a `ft_menu_item()` call – eliminates
  direct references to unexported librt menu arrays.
- [x] `sedit()` ARB cases (`ECMD_ARB_MAIN_MENU`, `ECMD_ARB_SPECIFIC_MENU`):
  replaced old direct `mmenu_set(…, cntrl_menu)` / `which_menu` calls with
  `rt_edit_process()` so the librt ARB8 code drives menu-setting via the
  `ECMD_MENU_SET` callback.
- [x] `sedit()` ARS cases (`ECMD_ARS_PICK_MENU`, `ECMD_ARS_EDIT_MENU`):
  same – replaced with `rt_edit_process()`.
- [x] `buttons.c`: menu arrays (`first_menu`, `second_menu`, `sed_menu`,
  `oed_menu`) converted from `struct menu_item` → `struct rt_edit_menu_item`;
  `btn_item_hit`, `btn_head_menu` updated to new callback signature
  `(struct rt_edit *, int, int, int, void *)`.
- [x] `buttons.c` `be_s_rotate/trans/scale`: `SROT/STRANS/SSCALE` →
  `RT_PARAMS_EDIT_ROT/TRANS/SCALE`.
- [x] `doevent.c`, `dm-generic.c`, `mged.c` `event_check`: all
  `SROT/STRANS/SSCALE` assignments replaced with `RT_PARAMS_EDIT_*`.
- [x] `scroll.c`: `sl_halt_scroll` / `sl_toggle_scroll` updated to the new
  menu-callback signature `(struct rt_edit *, int, int, int, void *)`.
- [x] `menu.c`: removed ~1300 lines of primitive-specific menu arrays and
  editor functions (`cline_ed`, `extr_ed`, `ars_ed`, …); updated all local
  `struct menu_item` references to `struct rt_edit_menu_item`; updated
  `mmenu_select` to call `(*(mptr->menu_func))(MEDIT(s), …, s)`.
- [x] `menu.h`: replaced old `struct menu_item` forward-declarations and
  `NMENU`/`MENU_L1` etc. with:
  - `#include "rt/edit.h"` (provides `struct rt_edit_menu_item`)
  - All `NMENU`/`MENU_*` position defines retained (needed by display code)
  - `MENU_TOR_R1` … `MENU_HYP_ROT_H` legacy constants retained (needed by
    `pscale()` in `edsol.c` – see Next Steps)
  - Declarations for `sed_menu[]`, `oed_menu[]`, `btn_head_menu`,
    `chg_l2menu`, `mmenu_set`, `mmenu_set_all`, etc. with updated types.
- [x] `mged.h`: removed `struct menu_item` definition, `NMENU`/`MENU_L1`
  defines, old display-constant defines; added `#include "./menu.h"`;
  removed `btn_head_menu`/`chg_l2menu` declarations (now in `menu.h`);
  updated `mged_print_result` to new callback signature.
- [x] `mged_dm.h`: `ms_menus[NMENU]` changed from `struct menu_item *` to
  `struct rt_edit_menu_item *`.
- [x] `cmd.c`: `mged_print_result` updated to new callback signature
  `(int, const char **, void *, void *)`.
- [x] `edsol.c`: all `mged_print_result(s, TCL_ERROR/OK)` calls updated to
  `mged_print_result(0, NULL, s, NULL)`.
- [x] Build confirmed working (`[100%] Built target mged`).

---

## Next Steps (in rough priority order)

### 1. Remove `pscale()` from `edsol.c` (big win)

`pscale()` is a ~500-line function in `edsol.c` that implements all
primitive-specific scaling logic (TGC, TOR, ELL, SUPERELL, ARS, RPC, RHC,
EPA, EHY, HYP, ETO, PIPE, METABALL, …) by switching on the `es_menu` global
and the `MENU_*` constants.

All this logic already exists in the librt primitives as `ECMD_*` operations
handled by each primitive's `ft_edit()` function.  The work is:

1. Replace each `pscale()` case with a call to `rt_edit_process(MEDIT(s))`,
   setting the appropriate `edit_flag` first (already done for some).
2. Once `pscale()` is empty, remove it.
3. Remove the `MENU_*` legacy constants from `menu.h` (they are only needed
   by `pscale()` and the now-removed menu-array switch statements).

### 2. Remove `sedit()` primitive-specific cases (big win)

`sedit()` in `edsol.c` is another ~4000-line function with many `case ECMD_*:`
blocks that duplicate logic already in `ft_edit()`.  Each case should be
replaced with `rt_edit_process(MEDIT(s))`.

Order of attack:
* ARB8 cases (many `ECMD_ARB_*`) – already started (ARB_MAIN_MENU,
  ARB_SPECIFIC_MENU done).
* TGC cases (`ECMD_TGC_*`).
* PIPE cases (`ECMD_PIPE_*`).
* ARS cases (PICK done; remaining MOVE/DEL/DUP etc.).
* Remaining primitives.

### 3. Remove `sedit_mouse()` primitive-specific cases

`sedit_mouse()` in `edsol.c` dispatches mouse events to primitive-specific
handlers.  These should delegate to `ft_edit_xy()` from librt.

### 4. Remove `edarb.c`, `edars.c`, `edpipe.c` from MGED

The diff removes these files from `src/mged/CMakeLists.txt`.  Prerequisite:
- The Tcl commands `f_extrude`, `f_mirface`, `f_edgedir`, `f_permute`
  (defined in `edarb.c`) need to be moved to `libged` or kept in a
  trimmed-down MGED-only file.
- `editarb()` in `mged/edarb.c` can be removed once `sedit()` uses
  `rt_edit_process()` for ARB cases.
- `newedge` global can be removed once `editarb()` is gone from MGED.

### 5. `chgview.c` – remove duplicate MGED edit-knob functions

The diff removes:
* `mged_knob_edit_process()` – replaced by direct `rt_edit_process()` call
* `wrap_angle_180()` / `mged_erot()` / `mged_erot_xyz()` / `mged_etran()` /
  `mged_knob_edit_apply()` – replaced by `rt_knob_edit_rot()` /
  `rt_knob_edit_tran()` from librt
* The `using_librt_edit` branch in `f_knob()` – always use librt path

The current `f_knob()` has both an old MGED path and a librt path gated on
`using_librt_edit`.  Removing the old path requires verifying that the librt
path covers all the cases (rotation, translation, scale for solids and for
matrix edit).

### 6. `sedit.h` – simplify `SEDIT_*` macros

Once the primitive-specific `edit_flag` values are no longer set from MGED
code, the `SEDIT_ROTATE`, `SEDIT_TRAN`, `SEDIT_SCALE` macros can be
simplified from their current long `||`-chains to just check `edit_mode`:

```c
/* current (many terms): */
#define SEDIT_ROTATE(s)  (MEDIT(s)->edit_flag == SROT || ... || \
                          MEDIT(s)->edit_flag == ECMD_TGC_ROT_H || ...)
/* target (simple): */
#define SEDIT_ROTATE(s)  (MEDIT(s)->e->edit_mode == RT_PARAMS_EDIT_ROT)
```

Prerequisite: all primitives must correctly set `edit_mode` in their
`ft_edit()` implementations (most already do – verify stragglers).

### 7. Remove `IDLE`, `STRANS`, `SSCALE`, `SROT`, `PSCALE`, `EARB`, `PTARB`
     from `sedit.h`

These are in the `/* These ECMD_ values go in MEDIT(s)->edit_flag */` block.
They can be removed once `pscale()` and the remaining primitive-specific
sedit() cases are gone.

---

## Important constants mapping (old MENU_* → new ECMD_*)

The `MENU_*` constants in `menu.h` (e.g. `MENU_TOR_R1 = 21`) correspond to
`menu_arg` values used by `pscale()`.  The corresponding `ECMD_*` values (in
librt primitive source files) are what `ft_edit()` uses.  Example:

| Old `es_menu` value   | New `ECMD_*` (in librt) |
|-----------------------|------------------------|
| `MENU_TOR_R1` (21)    | `ECMD_TOR_R1` (in edtor.c) |
| `MENU_TGC_SCALE_H` (27) | `ECMD_TGC_SCALE_H` (in edtgc.c) |
| `MENU_ELL_SCALE_A` (39) | `ECMD_ELL_SCALE_A` (in edell.c) |

When replacing a `pscale()` case, look up the matching `ECMD_*` in the
corresponding `src/librt/primitives/*/ed*.c` file.

---

## File summary

| File | Status |
|------|--------|
| `src/mged/menu.c` | Updated – primitive arrays removed, types updated |
| `src/mged/menu.h` | Updated – new type, legacy MENU_* retained for now |
| `src/mged/mged.h` | Updated – struct menu_item removed, menu.h included |
| `src/mged/mged_dm.h` | Updated – ms_menus type updated |
| `src/mged/buttons.c` | Updated – menu arrays and callbacks updated |
| `src/mged/scroll.c` | Updated – sl_halt/toggle_scroll signatures updated |
| `src/mged/doevent.c` | Updated – SROT/STRANS/SSCALE → RT_PARAMS_EDIT_* |
| `src/mged/dm-generic.c` | Updated – STRANS → RT_PARAMS_EDIT_TRANS |
| `src/mged/mged.c` | Updated – btn_head_menu call, RT_PARAMS_EDIT_* |
| `src/mged/cmd.c` | Updated – mged_print_result callback signature |
| `src/mged/edsol.c` | Partially updated – sedit_menu, f_get_sedit_menus, |
| | ARB/ARS menu cases migrated; pscale() still pending |
| `src/mged/edarb.c` | **TODO** – remove after Tcl cmds moved to libged |
| `src/mged/edars.c` | **TODO** – remove after migrating to librt |
| `src/mged/edpipe.c` | **TODO** – remove after migrating to librt |
| `src/mged/chgview.c` | **TODO** – remove duplicate knob-edit functions |
| `src/mged/sedit.h` | **TODO** – simplify SEDIT_* macros, remove SROT etc. |
