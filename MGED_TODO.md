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

- [x] Added `mged_impl.cpp` / `mged_impl.h` – callback-map registration
  helpers.
- [x] Registered MGED callbacks for `ECMD_PRINT_STR`, `ECMD_VIEW_UPDATE`,
  `ECMD_EAXES_POS`, `ECMD_MENU_SET`, `ECMD_PRINT_RESULTS`, `ECMD_GET_FILENAME`.
- [x] `sedit_menu` / `f_get_sedit_menus` replaced with `ft_menu_item()`.
- [x] `buttons.c`, `scroll.c`, `doevent.c`, `dm-generic.c`, `mged.c`:
  `SROT/STRANS/SSCALE` → `RT_PARAMS_EDIT_ROT/TRANS/SCALE`.
- [x] `menu.c`: removed ~1300 lines of primitive-specific menu arrays.
- [x] **Fixed all ECMD value mismatches in `sedit.h`**: Old MGED-local small
  values (5-92) updated to match librt's `ID_PRIM * 1000 + old_value` scheme:
  - TGC move/rotate: 5→2005, 6→2006, 7→2007, 8→2008, 81→2081, 82→2082
  - ARB: 11→4011, 12→4012, 13→4013, 14→4014, 15→4015
  - ETO_ROT_C: 16→21016; NMG: 19-27→11019-11027; HYP_ROT: 91-92→38091-38092
  - PIPE: 28-33→15028-15033; new PIPE scale ECMDs added (15065-15074)
  - ARS: 34-47→5034-5047; VOL: 48-52→13048-13052; EBM: 53-55→12053-12055
  - DSP: 56-60→25056-25060; BOT: 61-72→30061-30072; EXTR: 73-76→27073-27076
  - CLINE: 77-80→29077-29080; METABALL: 83-90→36083-36090
  - Added `ECMD_METABALL_PT_SET_GOO = 30119` (uses 30000-base in librt)
- [x] **Removed `pscale()` (~1015 lines)** from `edsol.c`.
- [x] **Removed `MENU_*` legacy constants** from `menu.h` (no longer used).
- [x] **`sedit()` fully delegated**: All primitive-specific cases now call
  `rt_edit_process(MEDIT(s))` and return.  Removed ~2300 lines of duplicate
  inline case handlers that were superseded by the delegate block.  Retained
  only `SSCALE`, `STRANS`, `ECMD_VTRANS`, `PTARB`/`EARB`, `SROT` – these are
  MGED-specific and don't have librt equivalents yet.
- [x] **Removed `dsp_scale()` dead function** and unused local variables.
- [x] **`chgview.c` librt path enabled**:
  - `using_librt_edit` branch removed; librt path always used.
  - Removed old functions: `wrap_angle_180`, `mged_knob_edit_process`,
    `mged_erot`, `mged_erot_xyz`, `mged_etran`, `mged_knob_edit_apply` (~340 lines).
  - Replaced `mged_erot`/`mged_etran` callers in `cmd_mrot`/`f_rot`/`f_arot`/
    `f_tra` with inline `rt_knob_edit_rot`/`rt_knob_edit_tran` calls.
  - `mged_librt_knob_edit_apply` now keeps `re->vp` current.
- [x] `MEDIT(s)->vp` set in `init_sedit`, `init_oedit_guts`, and
  `mged_librt_knob_edit_apply` so librt has view state for coordinate transforms.
- [x] `mged_param` updated to use `edit_flag` instead of `es_menu` for
  PIPE/METABALL parameter validation.
- [x] Build confirmed clean (`[100%] Built target mged`).

---

## Next Steps (in rough priority order)

### 1. Migrate remaining `sedit()` generic cases

The `SSCALE`, `STRANS`, `ECMD_VTRANS`, `PTARB`/`EARB`, `SROT` cases still have
inline MGED-specific code in `sedit()`.  The librt path now handles the
equivalent operations via `rt_edit_process()` when `edit_mode` is
`RT_PARAMS_EDIT_ROT/TRANS/SCALE`.  These old cases could be removed once
confirmed that `rt_edit_process()` covers all the same paths.

Specifically:
- `SSCALE` → `RT_PARAMS_EDIT_SCALE` already works for primitives; the inline
  MGED code handles the legacy `es_scale` path.
- `SROT` → `RT_PARAMS_EDIT_ROT` in librt handles rotation; but MGED's SROT
  also handles `acc_rot_sol` accumulation and keyboard-parameter parsing.
- `STRANS/ECMD_VTRANS` → handled by `rt_edit_process` with `RT_PARAMS_EDIT_TRANS`.
- `PTARB/EARB` → ARB edge/point moves; confirm librt handles these.

### 2. Simplify `SEDIT_*` macros in `sedit.h`

Once all primitives set `edit_mode` correctly in `ft_edit()`, replace the long
`||`-chains with simple `edit_mode` checks:

```c
/* current (many terms): */
#define SEDIT_SCALE (... || MEDIT(s)->edit_flag == ECMD_TOR_R1 || ...)
/* target (simple): */
#define SEDIT_SCALE (MEDIT(s)->edit_mode == RT_PARAMS_EDIT_SCALE)
```

Prerequisite: verify all primitives set `edit_mode` in `ft_edit_set_edit_mode()`.

### 3. `sedit_mouse()` – delegate to `ft_edit_xy()`

`sedit_mouse()` in `edsol.c` dispatches mouse events to primitive-specific
handlers.  These should delegate to `EDOBJ[idb_type].ft_edit_xy()` from librt.

### 4. Remove `edarb.c`, `edars.c`, `edpipe.c` from MGED

The Tcl commands `f_extrude`, `f_mirface`, `f_edgedir`, `f_permute`
(defined in `edarb.c`) need to be moved to `libged` or kept separately.
Once those are gone, these files can be removed from `CMakeLists.txt`.

### 5. Remove `IDLE`, `STRANS`, `SSCALE`, `SROT`, `EARB`, `PTARB` from `sedit.h`

Once the remaining inline sedit() cases are eliminated, these obsolete
`edit_flag` defines can be removed.

### 6. `init_sedit` / `init_oedit_guts` cleanup

Verify that `mged_edit_clbk_sync()` is called appropriately after init so
that librt's callback maps are in sync with MGED state.

---

## File summary

| File | Status |
|------|--------|
| `src/mged/menu.c` | Updated – primitive arrays removed, types updated |
| `src/mged/menu.h` | Updated – legacy MENU_* removed, new type |
| `src/mged/mged.h` | Updated – struct menu_item removed, menu.h included |
| `src/mged/mged_dm.h` | Updated – ms_menus type updated |
| `src/mged/buttons.c` | Updated – menu arrays and callbacks updated |
| `src/mged/scroll.c` | Updated – sl_halt/toggle_scroll signatures updated |
| `src/mged/doevent.c` | Updated – SROT/STRANS/SSCALE → RT_PARAMS_EDIT_* |
| `src/mged/dm-generic.c` | Updated – STRANS → RT_PARAMS_EDIT_TRANS |
| `src/mged/mged.c` | Updated – btn_head_menu call, RT_PARAMS_EDIT_* |
| `src/mged/cmd.c` | Updated – mged_print_result callback signature |
| `src/mged/edsol.c` | Mostly updated – pscale() removed; sedit() delegated |
| | to rt_edit_process() for all primitives except legacy |
| | SSCALE/STRANS/VTRANS/PTARB/EARB/SROT cases |
| `src/mged/sedit.h` | Updated – ECMD values corrected, SEDIT_* macros extended |
| `src/mged/chgview.c` | Updated – old knob functions removed, librt path active |
| `src/mged/edarb.c` | **TODO** – remove after Tcl cmds moved to libged |
| `src/mged/edars.c` | **TODO** – remove after migrating to librt |
| `src/mged/edpipe.c` | **TODO** – remove after migrating to librt |
