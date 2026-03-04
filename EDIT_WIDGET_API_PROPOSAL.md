# Proposal: Auto-Generated Edit Widgets via `ft_edit_desc`

## 1. Background and Motivation

BRL-CAD's `librt` now exposes a complete primitive editing API through
`rt_edit_process()` / `rt_edit_functab` (`EDOBJ[]`).  Every primitive that
has an `ed<prim>.c` file supports:

- Generic transforms (`RT_PARAMS_EDIT_TRANS`, `RT_PARAMS_EDIT_SCALE`, `RT_PARAMS_EDIT_ROT`, …)
- Primitive-specific operations (`ECMD_<PRIM>_*`) that accept their inputs
  through `s->e_para[]` (up to 20 `fastf_t` values) or through primitive-
  specific structs (shader/material strings, file names).

Each primitive also provides `ft_menu_item()` which returns an array of
`rt_edit_menu_item` structs — a human-readable label plus the `ECMD_*`
constant.  This is already enough for a text menu (as MGED's in-scene
faceplate provides), but it contains no information about *what inputs* each
command expects, so a GUI layer cannot automatically build an appropriate
edit widget.

The goal of this proposal is to define a minimal extension — a new
`ft_edit_desc()` slot in `rt_edit_functab` — that supplies enough
machine-readable metadata for a GUI to:

1. Enumerate the edit operations available for a primitive.
2. For each operation, know the number, types, ranges, labels, and units
   of the input parameters expected in `s->e_para[]`.
3. Automatically generate standard Qt edit widgets (spin boxes, sliders,
   combo boxes, check boxes, line edits, colour pickers, …).
4. Optionally, serialise the full description to JSON so that GUI layers
   can consume it at runtime without linking against `librt`.

---

## 2. Scope and Non-Goals

### In scope

- Simple parameter primitives: TOR, ELL, TGC, EPA, EHY, ETO, HYP, RPC,
  RHC, PART, SUPERELL, HRT, CLINE, EBM, DSP, VOL, ARS, METABALL, EXTRUDE,
  PIPE.
- Boolean/combination tree: COMB (material properties + tree ops).
- Primitive-independent generic ops: scale, translate, rotate.

### Out of scope (too specialised for a common grammar)

- **SKETCH**: Edit operations manipulate typed geometric segments (lines,
  arcs, Bezier curves, NURB curves) rather than scalar or vector
  parameters.  A specialised widget is required.
- **NMG / BREP**: Topology-level mesh/surface editing cannot be expressed
  as a parameter list.
- **BOT**: Multi-vertex move lists and face/edge selection are
  dataset-specific.
- **BSPLINE**: Control-point picks are geometry-specific.
- **Interactive pick commands** (`ECMD_*_PICK_*`): These are triggered by
  a mouse event in `ft_edit_xy`; there is no pre-call parameter list.

Even for out-of-scope primitives, `ft_edit_desc()` can still be provided
in a *partial* form for the subset of their operations that do accept plain
parameters.

---

## 3. Parameter Type System

The following type codes describe what kind of data a parameter slot holds
and, implicitly, what kind of Qt widget is appropriate.

```c
/* Parameter type codes for struct rt_edit_param_desc */
#define RT_EDIT_PARAM_SCALAR   1  /* single fastf_t; QDoubleSpinBox / QSlider   */
#define RT_EDIT_PARAM_INTEGER  2  /* truncated fastf_t; QSpinBox                */
#define RT_EDIT_PARAM_BOOLEAN  3  /* !NEAR_ZERO(val); QCheckBox                 */
#define RT_EDIT_PARAM_POINT    4  /* point_t (3 fastf_t); 3× QDoubleSpinBox     */
#define RT_EDIT_PARAM_VECTOR   5  /* vect_t  (3 fastf_t); 3× QDoubleSpinBox     */
#define RT_EDIT_PARAM_STRING   6  /* NUL-terminated; QLineEdit                  */
#define RT_EDIT_PARAM_ENUM     7  /* integer choice; QComboBox                  */
#define RT_EDIT_PARAM_COLOR    8  /* RGB triple stored as three fastf_t
                                   * (integer values 0–255) in e_para[0..2];
                                   * GUI should render a QColorDialog button  */
#define RT_EDIT_PARAM_MATRIX   9  /* 4×4 row-major; specialised matrix widget   */
```

For `RT_EDIT_PARAM_SCALAR` / `_INTEGER` / `_BOOLEAN` / `_ENUM` the value is
stored as `s->e_para[index]`.  For `RT_EDIT_PARAM_POINT` and `_VECTOR` three
consecutive slots starting at `e_para[index]` are used.
For `RT_EDIT_PARAM_MATRIX`, `e_para[0..15]` are used (existing convention for
`ECMD_COMB_SET_MATRIX`).
For `RT_EDIT_PARAM_STRING` the value is **not** in `e_para`; it lives in the
primitive-specific edit struct (e.g. `ce->es_shader`, `dsp->dsp_name`).
The `prim_field` member documents which field that is.
For `RT_EDIT_PARAM_COLOR` the values are stored as three `fastf_t` in
`e_para[0]`, `e_para[1]`, `e_para[2]` following the existing COMB colour
convention: each component holds an integer value in the range 0–255 encoded
as a `fastf_t`.  The GUI should read `(int)e_para[0]` etc. and clamp when
converting from its native QColor (0–255 integer range) back into `e_para`.

---

## 4. Proposed C Structs

The structs below are now implemented in `include/rt/edit.h`.

```c
/* sentinel value for "no range constraint" — chosen as -DBL_MAX so it can
 * be stored and round-tripped safely as a fastf_t (double) without
 * conflicting with legitimate parameter values. */
#define RT_EDIT_PARAM_NO_LIMIT  (-DBL_MAX)

/**
 * Describes a single input parameter that must be supplied in
 * s->e_para[] (or in a primitive string field) before calling
 * rt_edit_process() with the associated cmd_id.
 */
struct rt_edit_param_desc {
    const char  *name;        /* machine-readable id, e.g. "r1"               */
    const char  *label;       /* human-readable widget label, e.g. "Major Radius" */
    int          type;        /* RT_EDIT_PARAM_* type code                     */
    int          index;       /* offset into s->e_para[] (unused for STRING)   */
    fastf_t      range_min;   /* RT_EDIT_PARAM_NO_LIMIT = no lower bound       */
    fastf_t      range_max;   /* RT_EDIT_PARAM_NO_LIMIT = no upper bound       */
    const char  *units;       /* "length", "angle_deg", "angle_rad",
                               * "fraction", "count", "none", NULL             */
    /* RT_EDIT_PARAM_ENUM only */
    int          nenum;               /* number of choices                     */
    const char * const *enum_labels;  /* human-readable option strings         */
    const int   *enum_ids;            /* integer stored in e_para[index]       */
    /* RT_EDIT_PARAM_STRING only */
    const char  *prim_field;  /* name of the primitive struct field, e.g.
                               * "es_shader" or "dsp_name"                     */
};

/**
 * Describes a single edit command (one ECMD_* constant) together with
 * the parameters it requires.
 */
struct rt_edit_cmd_desc {
    int          cmd_id;      /* ECMD_* constant                               */
    const char  *label;       /* human-readable operation label                */
    const char  *category;    /* grouping hint: "radius", "geometry",
                               * "rotation", "material", "tree", "misc"        */
    int          nparam;      /* number of entries in params[]                 */
    const struct rt_edit_param_desc *params; /* NULL when nparam == 0         */
    int          interactive; /* non-zero: GUI re-calls rt_edit_process() on
                               * every widget change (live wireframe update).
                               * 0: apply only on explicit "Apply" button.     */
    int          display_order; /* suggested display order within the category
                                 * group; lower values appear first.  Ties are
                                 * broken by array order.                       */
};

/**
 * Top-level descriptor for a single primitive type.
 * Return value of ft_edit_desc().
 */
struct rt_edit_prim_desc {
    const char                    *prim_type;  /* "tor", "ell", "tgc", …      */
    const char                    *prim_label; /* "Torus", "Ellipsoid", …     */
    int                            ncmd;
    const struct rt_edit_cmd_desc *cmds;       /* array of ncmd entries        */
};
```

### 4.1 New slot in `rt_edit_functab`

```c
/* Returns static metadata describing all edit operations for this
 * primitive.  Returns NULL if the primitive does not support the
 * structured descriptor (e.g. NMG, BREP, SKETCH). */
const struct rt_edit_prim_desc *(*ft_edit_desc)(void);
```

This slot is placed after the existing `ft_menu_item` slot in
`include/rt/functab.h`.  **Status: implemented.**  All existing EDOBJ
entries default to `NULL`; TOR provides a complete descriptor as the
initial reference implementation.

### 4.2 JSON serialiser

```c
/**
 * Serialise a primitive edit descriptor to a JSON string appended to out.
 * The caller is responsible for bu_vls_init/bu_vls_free.
 * Returns BRLCAD_OK on success, BRLCAD_ERROR on error.
 */
RT_EXPORT extern int
rt_edit_prim_desc_to_json(struct bu_vls *out,
                          const struct rt_edit_prim_desc *desc);

/**
 * Convenience wrapper: look up the EDOBJ entry for prim_type_id
 * and call rt_edit_prim_desc_to_json on its ft_edit_desc() result.
 * Returns BRLCAD_OK on success, BRLCAD_ERROR if prim_type_id has no descriptor.
 */
RT_EXPORT extern int
rt_edit_type_to_json(struct bu_vls *out, int prim_type_id);
```

**Status: implemented** in `src/librt/edit.cpp` using `bu_vls_printf`
for manual JSON emission (no external library dependency).

### 4.3 JSON helper library (`json.hpp`)

`src/libbu/json.hpp` is the nlohmann/json v3.11.3 single-header library,
relocated from its previous location in `src/libged/lint/json.hpp`.  It is
available as a **private implementation detail** to any `src/` code that
needs to *parse* JSON at runtime (e.g. a future qged plugin reading
`rt_edit_type_to_json` output from an out-of-process server).  It must
never be placed in `include/bu/` or any other public header directory.
Consumer code in `src/libged/lint/` now includes it as
`"../libbu/json.hpp"`.

---

## 5. JSON Grammar

The serialised form follows a straightforward mapping from the C structs.
All numeric ranges use `null` for `RT_EDIT_PARAM_NO_LIMIT`.  Keys match
the C field names verbatim to keep the two representations in sync.

```
primitive-descriptor ::=
  {
    "prim_type"  : string,
    "prim_label" : string,
    "commands"   : [ command-descriptor* ]
  }

command-descriptor ::=
  {
    "cmd_id"        : integer,
    "label"         : string,
    "category"      : string,
    "interactive"   : boolean,   -- true = live wireframe update; false = batch
    "display_order" : integer,   -- lower values appear first in the UI group
    "params"        : [ param-descriptor* ]
  }

param-descriptor ::=
  {
    "name"    : string,
    "label"   : string,
    "type"    : one-of("scalar","integer","boolean","point","vector",
                        "string","enum","color","matrix"),
    "index"   : integer,           -- omitted for "string"
    "min"     : number | null,     -- omitted for non-numeric types
    "max"     : number | null,     -- omitted for non-numeric types
    "units"   : string | null,
    -- for "enum" only:
    "enum_labels" : [ string* ],
    "enum_ids"    : [ integer* ],
    -- for "string" only:
    "prim_field" : string
  }
```

---

## 6. Concrete Examples

### 6.1 TOR — two radii (reference implementation)

This example matches the `rt_edit_tor_edit_desc()` implementation in
`src/librt/primitives/tor/edtor.c`.

```json
{
  "prim_type": "tor",
  "prim_label": "Torus",
  "commands": [
    {
      "cmd_id": 1021,
      "label": "Set Radius 1",
      "category": "radius",
      "interactive": true,
      "display_order": 10,
      "params": [
        {
          "name": "r1", "label": "Major Radius",
          "type": "scalar", "index": 0,
          "min": 1e-10, "max": null, "units": "length"
        }
      ]
    },
    {
      "cmd_id": 1022,
      "label": "Set Radius 2",
      "category": "radius",
      "interactive": true,
      "display_order": 20,
      "params": [
        {
          "name": "r2", "label": "Minor Radius",
          "type": "scalar", "index": 0,
          "min": 1e-10, "max": null, "units": "length"
        }
      ]
    }
  ]
}
```

### 6.2 EHY — height vector magnitude + two radii + asymptote constant

```json
{
  "prim_type": "ehy",
  "prim_label": "Elliptical Hyperboloid",
  "commands": [
    {
      "cmd_id": 20053, "label": "Set H",   "category": "geometry",
      "params": [{"name":"h","label":"Height (magnitude)","type":"scalar","index":0,
                  "min":1e-10,"max":null,"units":"length"}]
    },
    {
      "cmd_id": 20054, "label": "Set A",   "category": "geometry",
      "params": [{"name":"r1","label":"Semi-Axis A","type":"scalar","index":0,
                  "min":1e-10,"max":null,"units":"length"}]
    },
    {
      "cmd_id": 20055, "label": "Set B",   "category": "geometry",
      "params": [{"name":"r2","label":"Semi-Axis B","type":"scalar","index":0,
                  "min":1e-10,"max":null,"units":"length"}]
    },
    {
      "cmd_id": 20056, "label": "Set c",   "category": "geometry",
      "params": [{"name":"c","label":"Asymptote Constant c","type":"scalar",
                  "index":0,"min":1e-10,"max":null,"units":"length"}]
    }
  ]
}
```

### 6.3 DSP — mixed scalar / boolean / enum / string

```json
{
  "prim_type": "dsp",
  "prim_label": "Displacement Map",
  "commands": [
    {
      "cmd_id": 25056, "label": "Name",  "category": "data",
      "params": [{"name":"fname","label":"Filename","type":"string",
                  "prim_field":"dsp_name","units":null}]
    },
    {
      "cmd_id": 25058, "label": "Set X", "category": "geometry",
      "params": [{"name":"xs","label":"X Cell Size","type":"scalar","index":0,
                  "min":1e-10,"max":null,"units":"length"}]
    },
    {
      "cmd_id": 25059, "label": "Set Y", "category": "geometry",
      "params": [{"name":"ys","label":"Y Cell Size","type":"scalar","index":0,
                  "min":1e-10,"max":null,"units":"length"}]
    },
    {
      "cmd_id": 25060, "label": "Set ALT", "category": "geometry",
      "params": [{"name":"alts","label":"Altitude Scale","type":"scalar","index":0,
                  "min":1e-10,"max":null,"units":"length"}]
    },
    {
      "cmd_id": 25061, "label": "Toggle Smooth", "category": "geometry",
      "params": [{"name":"smooth","label":"Smooth","type":"boolean","index":0,
                  "units":null}]
    },
    {
      "cmd_id": 25062, "label": "Set Data Source", "category": "data",
      "params": [
        {
          "name":"datasrc","label":"Data Source","type":"enum","index":0,
          "enum_labels":["File","Object"],
          "enum_ids":[102, 111],
          "units":null
        }
      ]
    }
  ]
}
```

### 6.4 COMB — boolean tree + material properties

```json
{
  "prim_type": "comb",
  "prim_label": "Combination / Region",
  "commands": [
    {
      "cmd_id": 12001, "label": "Add Member",    "category": "tree",
      "params": [
        {"name":"member_name","label":"Member Name","type":"string",
         "prim_field":"es_member_name","units":null},
        {
          "name":"op","label":"Boolean Operation","type":"enum","index":0,
          "enum_labels":["Union","Intersect","Subtract"],
          "enum_ids":[2, 3, 4],
          "units":null
        }
      ]
    },
    {
      "cmd_id": 12004, "label": "Set Region Flag", "category": "material",
      "params": [{"name":"region","label":"Is Region","type":"boolean","index":0,
                  "units":null}]
    },
    {
      "cmd_id": 12005, "label": "Set Color", "category": "material",
      "params": [{"name":"color","label":"Color (RGB)","type":"color","index":0,
                  "units":null}]
    },
    {
      "cmd_id": 12007, "label": "Set Shader", "category": "material",
      "params": [{"name":"shader","label":"Shader","type":"string",
                  "prim_field":"es_shader","units":null}]
    },
    {
      "cmd_id": 12009, "label": "Set Region ID", "category": "material",
      "params": [{"name":"region_id","label":"Region ID","type":"integer","index":0,
                  "min":0,"max":65535,"units":"none"}]
    }
  ]
}
```

---

## 7. How a Qt Layer Uses This

The sequence in qged (or any other Qt application using librt) would be:

```
1.  User selects an object for editing → librt returns prim_type_id.

2.  GUI calls rt_edit_type_to_json(json_vls, prim_type_id).
    (Or reads ft_edit_desc() directly for in-process use.)

3.  GUI parses the JSON and builds a QWidget panel:
    - One QGroupBox per unique "category".
    - Within each group, one row per command:
        - A QLabel showing cmd_desc.label.
        - For each param, a widget based on param.type:
            scalar  → QDoubleSpinBox  (range min..max, units label)
            integer → QSpinBox        (range min..max)
            boolean → QCheckBox
            point   → 3× QDoubleSpinBox with X/Y/Z labels
            vector  → 3× QDoubleSpinBox with X/Y/Z labels
            string  → QLineEdit (+ optional file-chooser button)
            enum    → QComboBox
            color   → QPushButton triggering QColorDialog
            matrix  → collapsible 4×4 spin-box grid
        - A QPushButton "Apply" that fires:
              s->e_inpara = read param values into s->e_para[];
              rt_edit_set_edflag(s, cmd_id);
              rt_edit_process(s);

4.  On Apply, the GUI reads widget values, converts to model space
    (applying s->base2local / s->local2base as appropriate), stuffs
    s->e_para[], sets s->e_inpara, then calls rt_edit_process().
```

For `RT_EDIT_PARAM_STRING` the GUI sets the primitive-specific field
directly (via a helper API call to be defined per-primitive) rather than
through `e_para`.  The `prim_field` name serves as documentation and as
a discriminator so that a GUI can discover which helper to call.

---

## 8. Relationship to Existing `ft_menu_item`

`ft_menu_item()` returns a flat array of `{ menu_string, callback, cmd_id }`
and already drives the MGED in-scene faceplate menus.  The new
`ft_edit_desc()` is **additive**: it does not replace `ft_menu_item` but
augments it with parameter-type information.  The `cmd_id` values are
identical, so a GUI can cross-reference both to obtain both the "needs a
menu entry" behaviour *and* the "here are its parameters" description.

Primitives can be migrated incrementally: `ft_edit_desc` returns `NULL`
until a descriptor is written, at which point the GUI gains the richer
widget; existing text-menu behaviour is unaffected.

---

## 9. Implementation Plan

### Completed

1. ✅ **Add types and structs to `include/rt/edit.h`** — the
   `rt_edit_param_desc`, `rt_edit_cmd_desc`, and `rt_edit_prim_desc` structs
   plus the `RT_EDIT_PARAM_*` constants, `RT_EDIT_PARAM_NO_LIMIT`, and
   the `interactive` / `display_order` fields.

2. ✅ **Add `ft_edit_desc` slot to `include/rt/functab.h`** — placed after
   the existing `ft_menu_item` slot, with `NULL` default for all existing
   entries.

3. ✅ **Implement `rt_edit_prim_desc_to_json` / `rt_edit_type_to_json` in
   `src/librt/edit.cpp`** — straightforward recursive JSON emission using
   `bu_vls_printf`.  No external JSON library is required for serialisation.

4. ✅ **Implement `ft_edit_desc` for TOR** — `edtor.c` provides the
   reference implementation with two `RT_EDIT_PARAM_SCALAR` parameters
   (`r1` major radius, `r2` minor radius), both marked `interactive = 1`.

5. ✅ **Relocate `json.hpp` to `src/libbu/json.hpp`** — moved from
   `src/libged/lint/json.hpp`.  Consumer code updated to use
   `"../libbu/json.hpp"`.  Remains a private implementation detail.

6. ✅ **Populate `ft_edit_desc` for additional primitives** —
   ELL, EPA, EHY, ETO, HYP, RPC, RHC, SUPERELL, CLINE, TGC all
   implemented with scalar parameters.  DSP adds boolean/enum/string
   parameters (FNAME/string, SCALE_X/Y/ALT/scalar, SET_SMOOTH/boolean,
   SET_DATASRC/enum).  EBM (FNAME, FSIZE/2-int, HEIGHT/scalar), VOL
   (FNAME, FSIZE/3-int, CSIZE/3-scalar, THRESH_LO/HI/integer), COMB
   (ADD_MEMBER, DEL_MEMBER, SET_OP, SET_REGION, SET_COLOR, SET_SHADER,
   SET_MATERIAL, SET_REGION_ID, SET_AIRCODE, SET_GIFTMATER, SET_LOS),
   and PIPE (PT_OD/ID/RADIUS, SCALE_OD/ID/RADIUS) all implemented.
   All new descriptors wired into `src/librt/primitives/edtable.cpp`.

7. ✅ **Add a `rt_edit_desc` unit test** — added as
   `src/librt/tests/edit/edit_desc.cpp`.  Calls `rt_edit_type_to_json`
   for all 17 implemented primitives, checks BRLCAD_OK return, verifies
   JSON is non-empty and contains `"commands"` and expected `cmd_id`
   integers.  Also verifies that a primitive without `ft_edit_desc`
   (ARB8) correctly returns `BRLCAD_ERROR`.  All 18 assertions pass.

### Remaining

8. **Implement the Qt auto-widget generator in qged** — guided by the
   algorithm in §7 above.  See §11 for critical architectural concerns
   that the qged layer must address.

---

## 10. Comb-Instance vs. Primitive-Edit Distinction

This section documents a fundamental architectural concern that **all
future qged edit widget sessions must be aware of**.

### 10.1 Background

When a user selects an object from the scene in qged, there are (at
least) two distinct interpretations of what editing operation is
appropriate:

**Primitive edit (solid params)**
  The user is editing the raw parameters of the leaf solid itself —
  e.g. changing a torus's major radius.  The change affects *every*
  instance of that solid in the scene, not just the path through which
  it was selected.  This is equivalent to MGED's `sed` command.

**Comb-instance edit (matrix / tree)**
  The user is editing the placement/scale of a particular *instance*
  of the object within a specific combination tree.  The change is
  local to that one `db_full_path`; other instances of the same solid
  are unaffected.  This is equivalent to MGED's `oed` command.  Valid
  operations are limited to comb-member transforms (translate, rotate,
  scale via the placement matrix) and boolean-operation changes.

qged already attempts to reflect which mode is active in the tree view.
Edit widgets must be designed with this distinction in mind.

### 10.2 Implications for Edit Widgets

| Concern | Primitive edit | Comb-instance edit |
|---------|---------------|--------------------|
| Scope of change | All instances in scene | One path only |
| EDOBJ functions used | `ft_edit`, `ft_edit_xy` (prim-specific) | Generic matrix ops |
| `rt_edit_prim_desc` applicable | Yes — full descriptor available | Only generic trans/rot/scale |
| Scene highlight | All instances of that solid | Only the selected instance |
| "Danger" level | High — unintended shared-solid mutation | Low — local change only |

### 10.3 Selection-to-Edit-Mode Mapping

The selection context determines which mode should be offered:

- **Top-level object selected** (no parent comb in the path): only
  primitive edit makes sense.
- **Non-top-level object selected**: both modes are valid.  The qged
  UI should present the user with a clear choice (e.g. two radio
  buttons: "Edit solid parameters" / "Edit instance placement") before
  activating the edit widget panel.

A recommended UX pattern (for future implementation):

```
Selection: /scene/hull/turret/gun_barrel  (a torus)

┌─ Edit Mode ──────────────────────────────────────────┐
│ ○ Solid parameters (affects ALL instances of tor.s)  │
│ ● Instance placement  (local to /hull/turret only)   │
└──────────────────────────────────────────────────────┘
```

The mode selector should be wired to:
- `rt_edit_create()` with the appropriate `db_full_path` (solid params)
- `rt_edit_create()` for comb instance editing (matrix mode)

### 10.4 Scene Highlighting Behaviour

Edit widget code should differentiate highlighting:

- **Primitive edit active**: the editing wireframe should be drawn for
  the solid itself (white, as in MGED), but application code should
  optionally *dim* all other instances of the same solid to make clear
  that they will all change.
- **Comb-instance edit active**: only the specific instance path
  should be highlighted; the model_changes matrix distorts the view
  for that instance without affecting the solid wireframe data.

This is "down the road" but the architecture must not preclude it.
Concretely: `struct rt_edit::comb_insts` already tracks the active
comb-member instance(s), and application code can use that to drive
selective highlighting.

### 10.5 Widget Panel Design Guidance

When `ft_edit_desc()` returns a descriptor:
- Show the descriptor-driven widget panel for **primitive edit mode**.
- For **comb-instance edit mode**, show only the generic
  translate/rotate/scale controls (no `ft_edit_desc` involvement).
- Keep both panels alive but toggle visibility based on the mode
  selector.

The `rt_edit_type_to_json` / `rt_edit_prim_desc_to_json` functions
produce the JSON that the qged panel factory should consume to build
the primitive-mode widget group.

---

## 11. Questions and Decisions

1. **Units handling** — **RESOLVED: string enum.**
   The `units` field uses a small fixed vocabulary: `"length"`,
   `"angle_deg"`, `"angle_rad"`, `"fraction"`, `"count"`, `"none"`, or
   `NULL`.  If future primitives require additional units this vocabulary
   can be extended.

2. **Live feedback vs. batch apply** — **RESOLVED: `interactive` flag
   in `rt_edit_cmd_desc`.**
   `interactive = 1` means the GUI calls `rt_edit_process()` on every
   widget change for live wireframe updates.  `interactive = 0` means
   the call happens only on an explicit "Apply" button press.  Most
   geometric parameter edits should be `interactive = 1`; expensive
   operations (e.g. DSP altitude scale on a large map) should use `0`.

3. **Current-value query** — **RESOLVED: add `ft_edit_get_params`.**
   A new `ft_edit_get_params` slot (future work) will read the current
   primitive state from `struct rt_edit *s` and return a parallel array
   of `fastf_t` values indexed by `rt_edit_param_desc::index` for each
   command in the descriptor.  This allows the qged widget panel to
   pre-populate spinners with live values on activation.
   The existing `ft_write_params` / `ft_read_params` text round-trip
   remains available for copy-paste and CLI workflows but is not
   suitable for structured widget initialisation.

4. **Multi-selection / general string parameter passing** — **RESOLVED:
   unified string parameter approach.**
   Rather than relying on primitive-specific named fields (`prim_field`),
   a future extension will add a `char **e_str` parallel string array
   alongside `e_para` in `struct rt_edit`, with `e_nstr` counting valid
   entries.  `RT_EDIT_PARAM_STRING` parameters will then use an `index`
   into `e_str` (instead of `prim_field`) matching the same convention
   as numeric parameters.  This makes multi-string commands (e.g. COMB
   ADD_MEMBER with a name + boolean operation) fully unified.  Until
   `e_str` is added, `prim_field` documents the struct field to populate.

5. **Grouping / ordering** — **RESOLVED: `display_order` field in
   `rt_edit_cmd_desc`.**
   Lower `display_order` values appear first within their `category`
   group.  Ties are broken by array order.  This allows primitives to
   control the visual layout of their widget rows without resorting to
   alphabetical sorting.

