This branch is intended to evolve the libbv bview API and related APIs to a form
that makes them a better match for the Obol Inventor-style scene APIs.

Background:  BRL-CAD uses a very old libdm/libfb layer for 3D scene management
with limited capabilities - it has long hindered our graphics progress.  We are
working on a fork of Coin3d's Coin library called Obol designed for our purposes -
it is present in this repository in the obol directory for reference.  With some
variations, it is an Open Inventor style API - it provides much richer 3D capabilities
than BRL-CAD currently has access to.

BRL-CAD's current code uses bview and bv_scene_obj structures that have evolved in
an ad-hoc basis over the years, and have not benefited from proper design.  However,
we do not want to tie our internal code (libged and below) directly to the Inventor
API - we instead want to migrate our existing logic to a form better suited for easy
mapping to and from the Inventor API.

You will see two primary drawing paths in the code - the following src/libged files
are for a new drawing stack that is used by the second-generation attempt at a drawing
stack:

draw/draw2.cpp
edit/edit2.cpp
erase/erase2.cpp
lod/lod2.cpp
rtcheck/rtcheck2.cpp
select/select2.cpp
view/autoview2.cpp
who/who2.cpp
zap/zap2.cpp

The src/gtools/gsh console application with the --new-cmds" option enabled is
where to start when testing - it is able to use the swrast headless display
system to produce images when using dm attach and screengrab, so it is a good
"low impact" way to test visualization capabilities after making code changes.
qged is the other application that targets this second gen system, but in these
early stages you'll want to leave Qt off - there will be enough to deal with,
and once we have the basic work done we'll circle back to Qt.

The brlcad_viewdbi folder has a version of BRL-CAD with some experimental work
on cleaning up the view structures and their associated LoD logic, but it is
not in final form and conflates work on updating the dbi_state management that
we're not ready to tackle here.  Still, it makes view sets the application's
responsibility rather than having libbv try to manage per-view LoD itself, and
that's definitely something we want to do - Obol has LoD capabilities, so we
will want to hook up our backend LoD generation logic to their system rather
than trying to manage it ourselves.

The initial goal is to propose a design for a new bview/bv_scene_obj and related
APIs that will allow our code to be a more straightforward mapping to the Obol
Inventor API concepts.  If possible, try to come up with new names for our
libbv replacements - what we will want to do is migrate all BRL-CAD code to
using your new containers/functions/etc., but keep the old API as an isolated,
unused, deprecated API for a couple releases until we can phase it out.

In the first phase, we'll want to migrate our logic as-is while minimizing
behavioral changes - this may limit us in a few cases like the LoD handling,
but let's get as close to the Inventor-style API setup as we can without major
rework in phase 1.  Phase 2 we can look at being a little more invasive and try
to get as close as we can while preserving an "old" path for backwards compatibility,
but we want Phase 1 in place first to keep our diffs minimal and clean.  Phase 3
will be to look at adapting the brlcad_viewdbi rework and improvements to the LoD
caching system - right now it's all tied up with the dbi_state rework, but we'll want
to migrate it in without also having major disruptions to the dbi_state layer.  It's
smarter about storing multiple data types in the background, so we want to get that
enhancement in place even if we're not set up yet to make proper use of it in the
scene displays.

Please maintain and update this document with your progress, so successive AI sessions
can keep working incrementally to achieve the goal.  The libbv API is used fairly
widely in the code, and we want to change all of it out except for a backwards
compatibility/deprecation leftover in libbv, so this is understood to be a major refactor
and should be approached accordingly.

---

## Progress Log

### Session 1 — BSG API design and Phase 1 skeleton

**Status:** Phase 1 skeleton in place; compiles cleanly as part of libbv.

#### New files

| Path | Purpose |
|------|---------|
| `brlcad/include/bsg.h` | Top-level convenience include |
| `brlcad/include/bsg/defines.h` | Phase 1 type aliases + bsg_camera design target |
| `brlcad/include/bsg/util.h` | New bsg_* function declarations |
| `brlcad/include/bsg/lod.h` | New bsg_mesh_lod_* function declarations |
| `brlcad/include/bsg/compat.h` | Macro aliases bv_* → bsg_* for incremental migration |
| `brlcad/src/libbv/scene_graph.cpp` | Phase 1 implementations (trivial delegates to bv_*) |

#### API type mapping (Open Inventor → BSG → legacy bv)

| BSG type (new)         | Replaces (legacy)       | Obol/Inventor analog         |
|------------------------|-------------------------|------------------------------|
| `bsg_material`         | `bv_obj_settings`       | `SoMaterial` + `SoDrawStyle` |
| `bsg_shape`            | `bv_scene_obj`          | `SoShape`                    |
| `bsg_group`            | `bv_scene_group`        | `SoSeparator`                |
| `bsg_lod`              | `bv_mesh_lod`           | `SoLOD`                      |
| `bsg_mesh_lod_context` | `bv_mesh_lod_context`   | (LoD cache context)          |
| `bsg_camera`           | *(inline in bview)*     | `SoCamera` *(Phase 2)*       |
| `bsg_view_settings`    | `bview_settings`        | (render/snap settings)       |
| `bsg_view_objects`     | `bview_objs`            | SoSeparator children list    |
| `bsg_knobs`            | `bview_knobs`           | (dial-box input state)       |
| `bsg_view`             | `bview`                 | `SoCamera` + view state      |
| `bsg_scene`            | `bview_set`             | root `SoSeparator` + cameras |

**Phase 1 design decision (Sessions 1–2):** `bsg_shape` started as a typedef alias of
`bv_scene_obj`.  Most other `bsg_*` types remain typedef aliases of their `bv_*`
counterparts (`bsg_view` = `bview`, `bsg_scene` = `bview_set`, etc.).

**Session 3 refinement:** `bsg_shape` is now an **independent struct definition** with
the same field layout as `bv_scene_obj`.  This gives:
- External libbv users: `struct bv_scene_obj` is unchanged — same definition, same
  behaviour, no source breaks.
- New `bsg_*` code: `struct bsg_shape` is a parallel definition with identical initial
  layout.  `BU_LIST_FOR(sp, bsg_shape, &head)` now works because `struct bsg_shape`
  is a real struct tag, not a typedef.
- `scene_graph.cpp` wraps `bv_*` functions using `bso_to_bv()` / `bv_to_bso()`
  reinterpret-cast helpers.  Both casts are safe because both structs share the
  same memory layout throughout Phase 1.
- A C convenience self-typedef `typedef struct bsg_shape bsg_shape;` lets callers
  use `bsg_shape *` without the `struct` keyword (standard C idiom).

**bsg_camera:** Defined as a genuinely new struct (it does not exist in the
legacy API) that documents which `bview` fields will be extracted into a
dedicated camera node in Phase 2.  It is NOT yet embedded in `bsg_view` (which
is still just `bview` via typedef).

#### Function naming convention

Every `bv_*` function has a new `bsg_*` name.  See `brlcad/include/bsg/util.h`
for the full table.  Key pattern changes:

- `bv_obj_*`  →  `bsg_shape_*`  (scene objects are "shapes")
- `bv_set_*`  →  `bsg_scene_*`  (view sets are "scenes")
- `bv_update` →  `bsg_view_update`

#### Next steps (Phase 1 completion)

1. **Migrate libged draw2/erase2/select2/etc.** to use `bsg_*` function names.
   These files were already identified in the README as the new drawing stack
   targets.  Add `#include <bsg.h>` and replace `bv_obj_get/put/reset/…` calls
   with `bsg_shape_get/put/reset/…`.
2. **Migrate libbv's own internal helpers** (util.cpp, view_sets.cpp, lod.cpp)
   to export under `bsg_*` names natively rather than wrapping.
3. **Mark old bv_* declarations deprecated** in `bv/util.h` and `bv/view_sets.h`
   using `__attribute__((deprecated))` or similar, to alert call sites.
4. **Audit remaining callers** — run `grep -r 'bv_obj_get\|bv_find_obj\|bv_clear\b'`
   across the tree to find all sites that need updating.

#### Phase 2 design notes

Once Phase 1 migration is complete:
- Give `bsg_view` its own struct definition with `bsg_camera camera` embedded.
  Field migration: `v->gv_perspective` → `v->camera.perspective`, etc.
- Introduce proper SoSeparator-style state isolation for `bsg_group` nodes.
- Consider whether `bsg_view_settings` fields can be moved into proper
  scene-graph property nodes (analogous to `SoComplexity`, `SoDrawStyle`).
- Update `bv/defines.h` to make the legacy `bview` a typedef alias of the new
  `bsg_view`, reversing the Phase 1 relationship, so old code still compiles.

#### Phase 3 design notes

- Port the `brlcad_viewdbi` per-view LoD pool approach: the application owns
  the pool (`bsg_view_objects::free_shape`) and passes it to libbv rather than
  libbv managing its own pool per-view.
- Decouple from the `dbi_state` rework — import only the LoD caching
  improvements, not the broader dbi_state restructuring.


Background:  BRL-CAD uses a very old libdm/libfb layer for 3D scene management
with limited capabilities - it has long hindered our graphics progress.  We are
working on a fork of Coin3d's Coin library called Obol designed for our purposes -
it is present in this repository in the obol directory for reference.  With some
variations, it is an Open Inventor style API - it provides much richer 3D capabilities
than BRL-CAD currently has access to.

BRL-CAD's current code uses bview and bv_scene_obj structures that have evolved in
an ad-hoc basis over the years, and have not benefited from proper design.  However,
we do not want to tie our internal code (libged and below) directly to the Inventor
API - we instead want to migrate our existing logic to a form better suited for easy
mapping to and from the Inventor API.

You will see two primary drawing paths in the code - the following src/libged files
are for a new drawing stack that is used by the second-generation attempt at a drawing
stack:

draw/draw2.cpp
edit/edit2.cpp
erase/erase2.cpp
lod/lod2.cpp
rtcheck/rtcheck2.cpp
select/select2.cpp
view/autoview2.cpp
who/who2.cpp
zap/zap2.cpp

The src/gtools/gsh console application with the --new-cmds" option enabled is
where to start when testing - it is able to use the swrast headless display
system to produce images when using dm attach and screengrab, so it is a good
"low impact" way to test visualization capabilities after making code changes.
qged is the other application that targets this second gen system, but in these
early stages you'll want to leave Qt off - there will be enough to deal with,
and once we have the basic work done we'll circle back to Qt.

The brlcad_viewdbi folder has a version of BRL-CAD with some experimental work
on cleaning up the view structures and their associated LoD logic, but it is
not in final form and conflates work on updating the dbi_state management that
we're not ready to tackle here.  Still, it makes view sets the application's
responsibility rather than having libbv try to manage per-view LoD itself, and
that's definitely something we want to do - Obol has LoD capabilities, so we
will want to hook up our backend LoD generation logic to their system rather
than trying to manage it ourselves.

The initial goal is to propose a design for a new bview/bv_scene_obj and related
APIs that will allow our code to be a more straightforward mapping to the Obol
Inventor API concepts.  If possible, try to come up with new names for our
libbv replacements - what we will want to do is migrate all BRL-CAD code to
using your new containers/functions/etc., but keep the old API as an isolated,
unused, deprecated API for a couple releases until we can phase it out.

In the first phase, we'll want to migrate our logic as-is while minimizing
behavioral changes - this may limit us in a few cases like the LoD handling,
but let's get as close to the Inventor-style API setup as we can without major
rework in phase 1.  Phase 2 we can look at being a little more invasive and try
to get as close as we can while preserving an "old" path for backwards compatibility,
but we want Phase 1 in place first to keep our diffs minimal and clean.  Phase 3
will be to look at adapting the brlcad_viewdbi rework and improvements to the LoD
caching system - right now it's all tied up with the dbi_state rework, but we'll want
to migrate it in without also having major disruptions to the dbi_state layer.  It's
smarter about storing multiple data types in the background, so we want to get that
enhancement in place even if we're not set up yet to make proper use of it in the
scene displays.

Please maintain and update this document with your progress, so successive AI sessions
can keep working incrementally to achieve the goal.  The libbv API is used fairly
widely in the code, and we want to change all of it out except for a backwards
compatibility/deprecation leftover in libbv, so this is understood to be a major refactor
and should be approached accordingly.
