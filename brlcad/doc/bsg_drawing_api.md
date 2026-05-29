# BSG Drawing API (Phase D7)

This document records the current public drawing API surface for BSG after
Phase D7 compatibility cleanup.

## Header layout

- Primary API headers live under `include/bsg/`.
- In-tree code now uses canonical `BSG_*` names directly; transitional aliases
  remain available only through an explicit `#include <bsg/legacy_compat.h>`.
- `include/bsg/node_private.h` carries the in-tree `bsg_node` layout; public
  headers expose `bsg_node` only as an opaque type.
- `include/bsg/view_set.h` is the canonical view-set header; `bsg/view_sets.h`
  is a compatibility bridge.

## Scene and payload API surface

### Scene nodes

- `bsg/node.h`
  - lifecycle: `bsg_node_create`, `bsg_node_destroy`
  - hierarchy: `bsg_node_parent`, `bsg_node_child_count`, `bsg_node_child_at`,
    `bsg_node_add_child`, `bsg_node_remove_child`
  - identity/flags: `bsg_node_kind`, `bsg_node_is_kind`,
    `bsg_node_set_name`, `bsg_node_name`
  - state: `bsg_node_set_visible_state`, `bsg_node_visible`,
    `bsg_node_set_transform`, `bsg_node_transform`,
    `bsg_node_set_bounds`, `bsg_node_bounds`,
    `bsg_node_set_user_data`, `bsg_node_user_data`,
    `bsg_node_settings`, `bsg_node_revision`, `bsg_node_touch`

### Typed payloads

- `bsg/payload_typed.h`
  - payload builders for text, HUD text, line sets, mesh, CSG, BRep, image,
    framebuffer, axes, grid, sketch/live-source, annotation
  - payload lifecycle hooks and typed accessors

### Draw-intent, pick/snap/measure, overlays

- `bsg/draw_intent.h`: source-path draw intent metadata and revalidation API
- `bsg/pick.h`: typed pick records and pick action APIs
- `bsg/snap_action.h`: typed snap APIs
- `bsg/measure.h`: typed measurement APIs
- `bsg/overlay.h`: role, lifecycle, ordering, and ownership APIs

### Render pipeline

- `bsg/render.h`: render request lifecycle and traversal entry points
- `bsg/render_item.h`: resolved render item contract
- `bsg/backend_adapter.h`: backend adapter callbacks and invalidation reasons
- `bsg/appearance_action.h`: resolved appearance accumulation API
- `bsg/render_settings.h`: render policy object API

## Compatibility policy

1. New code should use only `BSG_*` symbols and node/payload accessors.
2. Existing `BV_*` aliases are transitional and are all routed through the
   optional compatibility header (`bsg/legacy_compat.h`).
3. Any unavoidable compatibility bridge must be documented here with a removal
   owner/reference before release freeze.

## Scheduled removals

| Bridge/shim | Location | Tracking reference |
|---|---|---|
| `BV_*` macro aliases (`BSG_ENABLE_LEGACY_BV_ALIASES`) | `include/bsg/legacy_compat.h` | `drawing_modernization.txt` Phase D7 Task 6 |
| Legacy `display_list` struct bridge | `include/bsg/tcl_data.h` | `drawing_modernization.txt` Phase D7 Task 4 |
| `bsg_scene_group` alias macro | `include/bsg/defines.h` | `drawing_modernization.txt` Phase D7 Task 2 |
