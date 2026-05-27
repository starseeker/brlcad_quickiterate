# BSG Scene-Graph Vocabulary Reference

This document is the authoritative vocabulary for BRL-CAD's BSG (BRL-CAD Scene
Graph) drawing API, established in Phase D0 of the drawing modernization plan
(`drawing_modernization.txt`).  New code should use these terms and the BSG_*
names defined in `include/bsg/`.  Legacy BV_* and display-list terminology in
existing code is being retired incrementally through Phases D1–D7.

## Core scene-graph concepts

| Term | Type | Description |
|------|------|-------------|
| **scene root** | `bsg_node` (BSG_NODE_ROOT) | Top-level node that owns a draw tree; one per view set or database session. |
| **scene group** | `bsg_scene_group` (BSG_NODE_GROUP) | Collection of shape leaves or sub-groups; records draw-command source path. |
| **transform node** | `bsg_node` (BSG_NODE_TRANSFORM) | Stores a matrix that is concatenated with parent transform during traversal. |
| **shape node** / **scene object** | `bsg_node` (BSG_NODE_SHAPE) | Leaf node carrying a geometry or overlay payload. |
| **LoD node** | `bsg_node` (BSG_NODE_LOD) | Adaptive level-of-detail proxy that selects among child shapes. |
| **view-scope node** | `bsg_node` (BSG_NODE_VIEW_SCOPE) | Container whose children are only traversed for a specific view (or all views when NULL). |

## Payload concepts (Phase D1)

A *payload* is the typed geometry or annotation data carried by a shape node.
Phase D1 introduces an explicit typed payload handle (`bsg_payload`) replacing
the untyped `s_i_data`/`draw_data` convention.

| Payload type | BSG_PAYLOAD_* flag | `bsg_payload_type` enum | Description |
|---|---|---|---|
| Raw vlist | `BSG_PAYLOAD_VLIST` | `BSG_PL_VLIST` | `bsg_vlist` line/triangle segments. |
| Text label | — | `BSG_PL_TEXT` | 3D-anchored text label (`bsg_label`). |
| HUD text | — | `BSG_PL_HUD_TEXT` | Screen-space text for diagnostics and measurements. |
| Line set | — | `BSG_PL_LINE_SET` | Ordered set of line segments; replaces raw vlist for overlay lines. |
| Polygon | `BSG_PAYLOAD_OVERLAY` | `BSG_PL_POLYGON` | Filled/stroked polygon region (`bsg_data_polygon_state`). |
| Mesh | `BSG_PAYLOAD_MESH` | `BSG_PL_MESH` | BoT / LoD mesh (`bsg_mesh_lod`). |
| CSG source | `BSG_PAYLOAD_CSG` | `BSG_PL_CSG` | Adaptive CSG wireframe from a database object. |
| BRep | `BSG_PAYLOAD_BREP` | `BSG_PL_BREP` | BRep surface payload. |
| Image | — | `BSG_PL_IMAGE` | Raster image underlay/overlay. |
| Framebuffer | — | `BSG_PL_FRAMEBUFFER` | Framebuffer display (underlay or overlay). |
| Axes | — | `BSG_PL_AXES` | Axes widget (`bsg_axes`). |
| Grid | — | `BSG_PL_GRID` | Grid overlay. |
| Sketch | — | `BSG_PL_SKETCH` | Live sketch edit source. |
| Annotation | — | `BSG_PL_ANNOTATION` | Measurement annotation. |

## Draw-intent concepts (Phase D2)

A *draw intent* (`bsg_draw_intent`) is metadata attached to a scene group that
records the user-level drawing command: source database path, draw mode,
material policy, LoD policy, and replacement/erasure semantics.

| Term | Description |
|------|-------------|
| **draw intent** | The full record of what a draw command requested. |
| **source path** | The full database path the draw command targeted. |
| **draw mode** | Wireframe, shaded, hidden-line, etc. |
| **intent group** | A scene group annotated with a draw intent. |

BSG actions for draw intent: `bsg_draw_intent_find`, `bsg_draw_intent_reevaluate`,
`bsg_draw_intent_erase`, `bsg_draw_intent_simplify`, `bsg_collect_visible`.

## Selection and pick concepts (Phase D3)

| Term | Type | Description |
|------|------|-------------|
| **selection set** | `bsg_selection` | Named set of selected BSG nodes. |
| **pick record** | `bsg_pick_record` | Result of a pick action: node, source path, hit distance, view. |
| **pick action** | BSG action | Point, rectangle, ray, nearest-primitive, nearest-handle, path pick. |
| **snap query** | BSG query | Requests snap candidates from scene payloads and HUD providers. |

## HUD and overlay concepts (Phase D4)

| Term | Description |
|------|-------------|
| **HUD root** | A per-view `bsg_node` sub-tree containing 2D/screen-space nodes that are drawn after the 3D scene. |
| **overlay role** | A typed classification for overlay nodes: `EDIT_HANDLE`, `MEASURE`, `SELECTION_RUBBER_BAND`, `SNAP_GUIDE`, `COMMAND_RESULT`, `DIAGNOSTIC`, `TCL_ADORNMENT`, `POLYGON_EDIT`, `SKETCH_EDIT`, `USER_ANNOTATION`. |
| **overlay lifecycle** | Policy for how long an overlay persists: persistent, per-command, per-tool, per-view, shared-view-set, auto-remove. |
| **overlay order** | Rendering order: model overlay, screen/HUD, xray/always-on, post-transparent. |

## Render pipeline concepts (Phase D5)

| Term | Description |
|------|-------------|
| **render request** | `bsg_render_request` — triggers a traversal that produces render items. |
| **render item** | A resolved drawable unit with payload, appearance, transform, and phase. |
| **render phase** | Ordered rendering pass: framebuffer underlay, opaque 3D, transparent 3D, model overlays, screen/HUD, framebuffer overlay, diagnostics. |
| **backend adapter** | The interface between BSG render items and a display manager: `prepare`, `draw`, `invalidate`, `free`, `capabilities`. |

Backend tags: `BSG_BACKEND_NONE`, `BSG_BACKEND_GL`, and future `BSG_BACKEND_OBOL`.

## Material and appearance concepts (Phase D3 / Phase D5)

| Term | Description |
|------|-------------|
| **source material** | Database-assigned color/material for a database object. |
| **appearance override** | Command-level color, transparency, line-width, draw-mode override. |
| **resolved appearance** | Final per-node render state computed from source material + overrides + inherited group settings + selection/edit state. |
| **highlight** | Selection/edit-state appearance layer; not a raw node flag. |

## Retired / deprecated terminology

The following terms from the legacy codebase should not be used in new BSG code:

| Retired term | Replacement |
|---|---|
| display list | scene group / draw intent (Phase D2) |
| solid list | shape children of a scene group |
| scene object | shape node / `bsg_node` (BSG_NODE_SHAPE) |
| view object | overlay shape node / view-scope shape |
| `BV_DBOBJ_BASED` | `BSG_SHAPE_DBOBJ` |
| `BV_VIEWONLY` | `BSG_SHAPE_VIEWONLY` |
| `BV_LINES` | `BSG_SHAPE_LINES` |
| `BV_LABELS` | `BSG_SHAPE_LABELS` |
| `BV_AXES` | `BSG_SHAPE_AXES` |
| `BV_POLYGONS` | `BSG_SHAPE_POLYGONS` |
| `BV_ANCHOR_*` | `BSG_ANCHOR_*` |
| `BV_BACKEND_NONE` | `BSG_BACKEND_NONE` |
| `BV_BACKEND_GL` | `BSG_BACKEND_GL` |
| `BV_DB_OBJS` | `BSG_OBJ_DB` |
| `BV_VIEW_OBJS` | `BSG_OBJ_VIEW` |
| `BV_LOCAL_OBJS` | `BSG_OBJ_LOCAL` |
| `BV_CHILD_OBJS` | `BSG_OBJ_CHILD` |
| `s_draw_custom` callback | live-source payload scope (Phase D6) |
| faceplate struct drawing | HUD root payload nodes (Phase D4) |
