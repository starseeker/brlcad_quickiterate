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
                               * "fraction", "none", NULL                      */
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
`include/rt/functab.h`.

### 4.2 JSON serialiser

```c
/**
 * Serialise a primitive edit descriptor to a JSON string.
 * The caller is responsible for bu_vls_init/bu_vls_free.
 * Returns 0 on success, non-zero on error.
 */
RT_EXPORT extern int
rt_edit_prim_desc_to_json(struct bu_vls *out,
                          const struct rt_edit_prim_desc *desc);

/**
 * Convenience wrapper: look up the EDOBJ entry for prim_type_id
 * and call rt_edit_prim_desc_to_json on its ft_edit_desc() result.
 * Returns 0 on success, BRLCAD_ERROR if prim_type_id has no descriptor.
 */
RT_EXPORT extern int
rt_edit_type_to_json(struct bu_vls *out, int prim_type_id);
```

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
    "cmd_id"   : integer,
    "label"    : string,
    "category" : string,
    "params"   : [ param-descriptor* ]
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

### 6.1 TOR — two radii

```json
{
  "prim_type": "tor",
  "prim_label": "Torus",
  "commands": [
    {
      "cmd_id": 1021,
      "label": "Set Radius 1",
      "category": "radius",
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

1. **Add types and structs to `include/rt/edit.h`** — the
   `rt_edit_param_desc`, `rt_edit_cmd_desc`, and `rt_edit_prim_desc` structs
   plus the `RT_EDIT_PARAM_*` constants.

2. **Add `ft_edit_desc` slot to `include/rt/functab.h`** — after the
   existing `ft_menu_item` slot, with `NULL` default for all existing
   entries.

3. **Implement `rt_edit_prim_desc_to_json` / `rt_edit_type_to_json` in
   `src/librt/edit.cpp`** — straightforward recursive JSON emission using
   `bu_vls`.

4. **Populate `ft_edit_desc` for simple scalar primitives first** —
   TOR, ELL, EPA, EHY, ETO, HYP, RPC, RHC, SUPERELL are the easiest
   (all parameters are single scalars or scalar magnitudes).  DSP, EBM,
   and VOL add boolean/enum/string parameters and can follow.  COMB and
   PIPE round out the set.

5. **Add a `rt_edit_desc` unit test** to `src/librt/tests/edit/` that
   calls `rt_edit_type_to_json` for each implemented primitive and checks
   that the output parses as valid JSON and that expected `cmd_id` values
   are present.

6. **Implement the Qt auto-widget generator in qged** once the librt side
   is stable — guided by the algorithm in §7 above.

---

## 10. Open Questions

1. **Units handling**: Should the parameter `units` field be an enumeration
   (to allow the GUI to apply a system-wide unit conversion) or a free string?
   A small enum of `"length"`, `"angle_deg"`, `"angle_rad"`, `"fraction"`,
   `"count"`, `"none"` may be sufficient.

2. **Live feedback vs. batch apply**: Should `ft_edit_desc` optionally mark
   a command as "live" (i.e. re-call `rt_edit_process` on every spinner
   change) vs. "apply on button press"?  An `"interactive": true/false`
   flag on `rt_edit_cmd_desc` could handle this.

3. **Current-value query**: The proposal covers how to *set* parameters.
   To pre-populate widget values, the GUI needs to *read* the current
   primitive state.  The existing `ft_write_params` / `rt_edit_tor_write_params`
   mechanism produces human-readable text, not a structured value map.
   A complementary `ft_read_params` returning the same `rt_edit_prim_desc`
   structure but with current values populated in parallel arrays would
   allow initialising widgets with the live primitive state.

4. **Multi-selection**: The COMB `ADD_MEMBER` command names a member via
   a string stored in the primitive struct, not in `e_para`.  A more
   general solution for string parameters (e.g. a per-command string array
   alongside `e_para`) could unify this.  For now, `prim_field` documents
   which struct field to populate.

5. **Grouping / ordering**: The `category` field provides coarse grouping.
   If finer ordering is needed (e.g. "show H before r1, r2") a `display_order`
   integer on `rt_edit_cmd_desc` could be added without breaking the grammar.
