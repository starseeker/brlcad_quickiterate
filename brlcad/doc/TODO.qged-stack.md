# qged Technology Stack — Architecture, Current State, and TODO

This is the single source of truth for ongoing and future work on BRL-CAD's
qged / libqtcad / libged / libbsg technology stack.  It captures the current
architecture, what has been accomplished, what genuinely remains open, and a
prioritized work order for future sessions.

---

## 1. Architecture Overview

```
 ┌──────────────────────────────────────────────┐
 │               qged (application)            │
 │  QgEdApp  QgEdMainWindow  plugins/…         │
 └──────────────┬───────────────────────────────┘
                │ uses
 ┌──────────────▼───────────────────────────────┐
 │       libqtcad (Qt/GED bridge)              │
 │  QgModel  QgView  QgTreeView  QgGL/QgSW     │
 └──────────────┬───────────────────────────────┘
                │ uses
 ┌──────────────▼───────────────────────────────┐
 │          libged (command/state)              │
 │  DbiState  BViewState  DrawList              │
 │  SelectionSet  GeomLoader  GObj/CombInst     │
 └───────┬──────────────────────────────────────┘
         │ uses                  │ uses
 ┌───────▼────────┐    ┌────────▼──────────────┐
 │ librt / libdb  │    │  libbsg (scene graph) │
 │ raytrace, db_i │    │  bsg_view_traverse    │
 └────────────────┘    │  bsg_shape, sensors   │
                       └───────────────────────┘
                                 │ used by
                       ┌─────────▼─────────────┐
                       │      libdm            │
                       │  dm_draw_objs via     │
                       │  bsg_view_traverse    │
                       └───────────────────────┘
```

### Key files
| Component | Header | Implementation |
|-----------|--------|----------------|
| DBI state | `include/ged/dbi.h` | `src/libged/dbi_state.cpp` |
| Background loader | `include/ged/dbi.h` (`GeomLoader`) | `src/libged/dbi_state.cpp` |
| Scene graph | `include/bsg/defines.h`, `bsg/util.h` | `src/libbsg/scene_graph.cpp` |
| Qt model | `include/qtcad/QgModel.h` | `src/libqtcad/QgModel.cpp` |
| Draw manager | `src/libdm/view.c` | uses `bsg_view_traverse` |

---

## 2. DBI Layer — Current State (All Major Work Complete)

The DBI redesign was a nine-phase project that replaced the original parallel
flat-hash-map architecture in `dbi_state.cpp` with a clean, testable design.
All nine phases are complete.  A summary of what was accomplished:

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Typed hash wrappers `GHash`/`InstHash`/`PathHash`; `DbiPath` value type | ✅ |
| 2 | `IDbiObserver` + `DbiChangeEvent`; `QgModel` self-registers as observer | ✅ |
| 3 | `GObj`/`CombInst` object model; parallel flat maps augmented | ✅ |
| 4 | `DrawList` class; `BViewState` draw-list pipeline | ✅ |
| 5 | `SelectionSet` replaces `BSelectState` | ✅ |
| 6 | `QgModel` incremental row insert/remove via `IDbiObserver` | ✅ |
| 7 | Cleanup (C1–C6): resource ownership, color override, typed APIs | ✅ |
| 8 | `DrawList::commit()` integration into `BViewState::redraw()` | ✅ |
| 9 | C surface (`ged_dbi_*`, `ged_selection_*`); regression tests | ✅ |

Post-phase additions (session 25–29):
- **L1 Background geometry loading** — `GeomLoader` worker thread + drain pump
- **L2 Thread-safety docs** — "MAIN THREAD ONLY" annotations in `dbi.h`
- **L3 Attribute columns** — `QgModel::set_attribute_columns()` runtime-configurable
- **A1** `DbiPath` overloads for `DrawList::add` / `SelectionSet::select`
- **A2/A3** `BViewState::link_to()` fully implemented (draw-intent delegation to
  primary; `redraw()` processes primary's `draw_list_` entries before its own)

### What remains in the DBI layer

Nothing architecturally fundamental.  All ten originally diagnosed structural
problems have been resolved.  The open items are quality/feature work:

- See **Section 5** (qged/libqtcad open items) for application-layer gaps.
- See **Section 4** (BSG) for `loadview.cpp` camera P2 item.

---

## 3. DBI API Quick Reference

This section covers the key APIs for code that used the pre-redesign `dbi.h`.

### 3.1 Typed hash wrappers
```cpp
GHash    ghash = { bu_data_hash(name, len) };  // .g object name hash
PathHash phash = { dbis->path_hash(vec, 0) };  // full path hash
```
Raw value via `.v`.  All three types are `unordered_map`/`unordered_set` keys.

### 3.2 Observer pattern (replaces manual flag checking)
```cpp
class MyObserver : public IDbiObserver {
public:
    void on_dbi_changed(const std::vector<DbiChangeEvent> &events) override {
        for (const auto &ev : events) {
            if (ev.batch) { do_full_reset(); return; }
            switch (ev.kind) {
                case DbiChangeKind::ObjectAdded:
                case DbiChangeKind::ObjectRemoved:   needs_reset = true; break;
                case DbiChangeKind::ObjectModified:  handle_change(ev.object); break;
                default: break;
            }
        }
    }
};
DbiState *dbis = (DbiState *)gedp->dbi_state;
dbis->add_observer(&my_observer);   // in ctor
dbis->remove_observer(&my_observer);// in dtor
```

### 3.3 DrawList
```cpp
DrawList &dl = bvs->draw_list();
dl.add(path_hashes, 0 /*mode*/);            // add path
dl.remove(full_path_hash, -1 /*all modes*/);// remove
DrawState s = dl.query(path_hash, -1);      // NOT_DRAWN / FULLY_DRAWN / PARTIALLY_DRAWN
```

### 3.4 SelectionSet
```cpp
SelectionSet *sel = dbis->get_selection_set(); // default set
sel->select("path/to/obj", true);
bool is_sel = sel->is_selected(path_hash);
// Named sets:
dbis->add_selection_set("my_set");
SelectionSet *ns = dbis->get_selection_set("my_set");
```

### 3.5 Background geometry loading
```cpp
// Happens automatically at DbiState construction and after update() for
// added/changed solids.  Periodic drain in QgEdApp (100 ms QTimer):
size_t n = dbis->drain_geom_results();
if (n > 0) emit view_update(GED_DBISTATE_VIEW_CHANGE);
```
The `GeomLoader` worker thread uses its own `struct resource`; it never touches
DbiState STL containers.  All DbiState mutations remain main-thread-only.

### 3.6 View linking (quad-view sharing)
```cpp
BViewState *secondary = dbis->get_view_state(secondary_view);
secondary->link_to(primary_bvs);
// Now: add_hpath/erase_hpath delegate to primary; redraw() shows primary's geometry.
secondary->unlink(); // detach
```

---

## 4. BSG / libbsg Scene-Graph Layer — Current State

### What is complete

The `libbsg` library is the sole owner of all scene-graph functionality.
`libbv` has been removed.

| Phase | Description | Status |
|-------|-------------|--------|
| 2a | Scene-root creation at all view-init sites; `bsg_view_traverse` render loop | ✅ |
| 2b | LoD sensor system: `gl_register_dlist_sensor` / `gl_dlist_stale_cb` | ✅ partial |
| 2c | Camera-field accessor hygiene: all direct `gv_*` writes → `bsg_view_get/set_camera` | ✅ |
| 2d | `BSG_NODE_LOD_GROUP` support in `lod.cpp` and `dm-gl_lod.cpp` | ✅ |
| 2e | `dl_head_scene_obj` decommissioned; all shapes in scene-root `children` ptbl | ✅ |
| — | `gd_headDisplay` removed; draw/erase/who/zap/garbage_collect on scene-root | ✅ |
| — | All display-list compat stubs removed (`dl_bounding_sph`, `dl_color_soltab`, etc.) | ✅ |
| — | `ged_dl_notify` → `ged_rt_notify` rename; compat macros retained | ✅ |
| — | `bsg_view_scale/local2base/base2local/set_scale` accessors added to `bsg/util.h` | ✅ |
| — | `bsg_view_set_size(v, size)` accessor added to `bsg/util.h`; `loadview.cpp _ged_cm_vsize` migrated | ✅ |
| — | libqtcad + qged C++17 modernization (range-for, structured bindings, static_cast) | ✅ |
| — | `fb-swrast.cpp` already calls `bsg_scene_root_create()` at view init | ✅ |

### What remains open in BSG (minor / P2)

> The `loadview.cpp`, `fb-swrast.cpp`, and `mged/cmd.c` P2 items are all resolved.
> The only remaining BSG P2 item (camera migration for mged `_view_cache`) was
> already addressed: `_view_cache_save`/`_view_cache_restore` use
> `bsg_view_get_camera`/`bsg_view_set_camera`; scalar fields (gv_scale, gv_size, etc.)
> are accessed directly as they are not camera fields.

**P3 — `bsg_view_find_by_type()` and `bsg_sensor_fire()` — ✅ Done**
Both helpers are implemented in `src/libbsg/scene_graph.cpp` and declared in
`include/bsg/util.h`.  All present and functional in `src/libbsg/scene_graph.cpp`.
TODO updated accordingly.

**P3 — sensor-driven redraws in `libqtcad`**
`QgEdApp.cpp` already has `qged_register_view_sensors` / `qged_deregister_all_sensors`
and `qged_shape_stale_cb` sensor-driven redraws wired up (session 29).  The
`QgQuadView` and individual `QgView` widgets do not yet register repaint sensors;
they rely on the explicit `view_update` signal path.  This can be improved
incrementally without blocking any current functionality.

---

## 5. qged / libqtcad Open Items

Items are grouped by urgency.  Effort estimates are rough.

### Tier 1 — Correctness (fix first)

> **`QgConsoleListener` thread safety audit** — **DONE (session 35)**.
> `Qt::QueuedConnection` is confirmed to serialize all `ged_result_str` access on the
> main thread.  `Q_ASSERT(QThread::currentThread() == qApp->thread())` guard added to
> `on_callbackReady()` in `QgConsoleListener.cpp`.

> **Note**: `tmp_av` was previously flagged as using `delete` vs. `bu_free`.
> This has been fixed: the cleanup path now uses `bu_free` (QgEdApp.cpp:545–547).

### Tier 2 — Safety and maintenance

| Item | File(s) | Status | Effort |
|------|---------|--------|--------|
| Add `override` to virtual overrides in `qged/` and `libqtcad/` | Many headers | ✅ Done (session 36) — `QgKeyVal.h`, `QgAttributesModel.h`, `QgEdFilter.h`, `QgTreeView.h`, `QPolyCreate.h`, `QPolyMod.h`, `QEll.h`, `CADViewMeasure.h`, `CADViewSelector.h` | — |
| Move `raytrace.h`-dependent sketch export out of `libqtcad` into `qged` | `QgView.cpp`, `QgPolyFilter.cpp` | ✅ Unused `raytrace.h` includes removed (session 35); `QgSelectFilter.cpp` / `QgMeasureFilter.cpp` legitimately use raytrace for pick/measure | — |
| Migrate `fbserv.cpp` from `QTcpServer`/`QTcpSocket` to `QLocalServer`/`QLocalSocket` | `fbserv.cpp/.h` | Deferred — requires libpkg Unix-domain-socket support first | 4 h |
| Convert `QgModel::rootItem` / `items` (raw pointer forest) to `unique_ptr` ownership | `QgModel.cpp/.h` | ✅ Done (session 37) — `items` is now a value-type set; `rootItem_` is `unique_ptr<QgItem>`; `QgTreeView.cpp` updated | — |
| Consolidate view-update signal chain (`do_view_changed` → `view_update`) to eliminate reentrancy risk | `QgEdApp.cpp` | ✅ Coalescing wrapper added via `pending_view_flags_` + `flush_view_changed_` private slot (session 35) | — |

### Tier 3 — C++17 / Qt modernisation

| Item | Status | Effort |
|------|--------|--------|
| `[[nodiscard]]` on `run_cmd()` and other error-returning functions | ✅ Done (session 35) | — |
| Implement `QgFlowLayout` Qt6 geometry path (previously stub) | ✅ Done (session 35) — `margin()` removed, using `getContentsMargins()` | — |
| Implement `QgConsole` mid-string tab completion for Qt6 | Open | 3 h |
| Apply `[[maybe_unused]]` to replace `UNUSED()` macro calls | ✅ Done (session 37) — C++ member functions and free functions migrated; C callbacks (`qgmodel_*`, `rt_cmd_*`, `*_record`, `uniq_test`) intentionally kept with `UNUSED()` | — |

### Tier 4 — Feature completeness

| Item | Notes | Effort |
|------|-------|--------|
| **Primitive editing** (sed/oed) | `BViewState::begin_edit_session(DbiPath)` / `end_edit_session(bool accept)` pair; transient DrawList overlay; per-primitive Qt plugins. `QEll` is the prototype. | 3–4 weeks |
| **Key binding configuration** | `QShortcut` integration; `dm bindings` command; per-view key map | 1 week |
| **Raytrace control panel** | Qt widget wrapping `rt` process management | 2 weeks |
| **Plugin API expansion** | Dock widgets, full-window dialogs, GED command registration from plugins | 1–2 weeks |
| **Qt dialogs** for color/font settings | | 3 days |

---

## 6. DBI2 Analysis — What Was and Wasn't Incorporated

The `dbi2` branch was a prototype that preceded the current DBI redesign.
This section records what ideas came from it and what was deliberately left out.

### Incorporated from dbi2

| dbi2 concept | Current implementation |
|---|---|
| `GObj` / `CombInst` object model | Phase 3; `gobjs` map in `DbiState` |
| `DbiPath` value type | Phase 1; typed hash wrappers |
| View linking (higher-level view sets) | `BViewState::link_to()` fully done (session 29) |
| Background geometry loading intent | `GeomLoader` (session 29) |
| Per-path draw settings overrides | `DrawList::DrawSettings`, `DrawList::Entry` |
| `bu_cache` for on-disk bbox persistence | Replaced LMDB (which wasn't available when dbi2 was written) |

### Not incorporated from dbi2 (deliberate omissions)

| dbi2 feature | Why not carried forward |
|---|---|
| LMDB persistent cache | `bu_cache` is the correct BRL-CAD-standard answer |
| `std::thread` ad-hoc in dbi2_state | Replaced by `GeomLoader` with a clean producer/consumer contract |
| `DbiPath` pool allocator (`dbiq`) | Unnecessary; `DbiPath` is a value type, STL allocators sufficient |
| `GObj::LoadSceneObj()` in dbi.h | Rendering knowledge does not belong in the object model; it lives in `BViewState`/`DrawList` |
| Baked-in LoD coupling in `DbiPath_Settings::lod_v` | LoD is a view-level concern, not a path-level concern |

### What dbi2 pointed at that we didn't do yet

The dbi2 prototype showed ambition around showing **AABB/OBB placeholder geometry**
in the scene while full mesh data was loading.  The current `GeomLoader` populates
the `bboxes` map asynchronously and notifies via `ISceneObserver`, giving the scene
the data it needs.  The **placeholder rendering** step (drawing a wireframe box from
`bboxes[hash]` until the real geometry arrives) is not yet implemented.  This would
be a natural follow-on: `BViewState::redraw()` could emit a bbox wireframe for any
solid whose full draw data is not yet in the scene but whose bbox is already cached.

---

## 7. Architectural Recommendations (Forward-Looking)

### 7.1 Primitive editing architecture

The correct model (confirmed in the original DBI analysis and unchanged):
- `begin_edit_session(DbiPath)` creates a transient `DrawList` entry pointing to a
  modified copy of the shape (a view object, not the database object).
- All edit commands mutate the view object.
- `end_edit_session(true)` writes back via `DbiState::sync()` + normal update cycle.
- `end_edit_session(false)` removes the transient entry, restoring the original view.
- **No special editing mode** in `DbiState`; editing is expressed purely as a
  temporary DrawList overlay.

### 7.2 Selection as first-class GED state

`SelectionSet` is already owned by `DbiState`.  The remaining anti-pattern is
`QgTreeView` / `QgSelectFilter` maintaining Qt selection state that must be manually
synchronized with the `SelectionSet`.  Qt's `QItemSelectionModel` should be driven
*from* `SelectionSet` rather than the other way around.

### 7.3 One command/result cycle

Every `QgEdApp::run_cmd()` call should have exactly one post-processing step:
`DbiState::update()` → observer notifications → `BViewState::redraw()` for affected
views.  No special-case branching on command name.  Background-process commands
(raytrace, etc.) register a completion callback; the post-processing step fires from
there, not from the synchronous call site.

### 7.4 AABB/OBB → LoD progressive placeholder rendering ✅ Done (session 36 + DrawPipeline sessions 3–7)

BoT primitives now render through a single, consolidated placeholder mechanism
inside `bot_adaptive_plot()` in `draw.cpp`:

1. **AABB placeholder** — on the first `draw` command, `bot_adaptive_plot` is called
   for each BoT.  If the LoD key is not yet available, it draws an immediate AABB
   wireframe using `bsg_vlist_rpp()` with `vo->draw_data = NULL` (placeholder marker).
2. **OBB placeholder** — when the background `DrawPipeline` finishes the OBB stage,
   `drain_geom_results()` stores the 8-corner data in `dbis->obbs`.  The placeholder
   shape's `s_update_callback` (set to `bsg_mesh_lod_view`) fires, causing
   `bot_adaptive_plot` to redraw with the tighter `bsg_vlist_arb8()` OBB wireframe.
3. **LoD geometry** — when the LoD stage completes, `drain_geom_results()` calls
   `stale_mesh_shapes_for_dp()` (clears placeholder view-objects) and then
   `QgEdApp::drain_background_geom()` calls `do_view_changed(QG_VIEW_DRAWN)` which
   schedules `flush_view_changed_()` → `BViewState::redraw()` → `bot_adaptive_plot`
   with the LoD key now available → real `BSG_NODE_MESH_LOD` view-objects created.

The earlier `bbox_placeholders_` map in `BViewState` (a secondary dashed-grey
placeholder system that duplicated the above logic outside `bot_adaptive_plot`) was
removed.  It was a dead code path: `draw_list_` is always synced to `s_map` at the
end of each `redraw()`, so every entry in `draw_list_` already had an `s_map` entry
and the skip-condition `s_map.find(full_hash) != s_map.end()` was always true.
All placeholder work now flows exclusively through `bot_adaptive_plot()`.

---

## 8. Prioritized Work Order

### Completed (session 35)

- ✅ **`QgConsoleListener` thread safety audit**: `Q_ASSERT` guard added to
  `on_callbackReady()` confirming main-thread-only execution.
- ✅ **Unused `raytrace.h` removes**: Removed from `QgPolyFilter.cpp` and `QgView.cpp`
  where no raytrace symbols were used.  `QgSelectFilter.cpp` and `QgMeasureFilter.cpp`
  legitimately use raytrace for ray-cast pick/measure operations.
- ✅ **`bsg_view_set_size()` accessor**: Added to `bsg/util.h` + `scene_graph.cpp`;
  `loadview.cpp _ged_cm_vsize` now uses it.
- ✅ **`QgFlowLayout` Qt6 fix**: `minimumSize()` now uses `getContentsMargins()`
  instead of the removed Qt5 `margin()` method.
- ✅ **`[[nodiscard]]` on `run_cmd()` / `load_g_file()`**: Added to `QgEdApp.h` and
  `QgModel.h`.  Pre-existing ignore sites updated with explicit `(void)` casts.
- ✅ **View-update coalescing**: `do_view_changed` now accumulates flags into
  `pending_view_flags_` and dispatches work via a single `flush_view_changed_` private
  slot (QueuedConnection).  Eliminates synchronous reentrancy risk.
- ✅ **Qt `foreach` migration**: `QgFlowLayout`, `QgAccordion`, `QgToolPalette` migrated
  from deprecated Qt `foreach` to C++17 range-based for loops.
- ✅ **`QgConsole::setMargin` Qt6 fix**: `setMargin(0)` guarded block replaced with
  `setContentsMargins(0,0,0,0)` which works in both Qt5 and Qt6.
- ✅ **`QgAttributesModel` `QRegExp` simplification**: `val.split(QRegExp("/"))` with
  Qt5/Qt6 guard replaced by `val.split("/")` which works in both.
- ✅ **`QgConsole::historyAt` dangling pointer fix**: Stored `QByteArray` before taking
  `.data()` pointer, eliminating UB from reading a destroyed temporary.
- ✅ **`QgModel` destructor leak fix**: Added iteration-and-delete of all `QgItem *` in
  the `items` set before deleting the set container.
- ✅ **`mged/cmd.c` `_view_cache`**: Already complete — `_view_cache_save/restore` use
  `bsg_view_get/set_camera()` for camera fields; scalar fields are accessed directly.

### Completed (session 36)

- ✅ **`override` audit complete**: Added `override` to all missing virtual overrides:
  - `QgKeyVal.h`: `QgKeyValModel::index/parent/rowCount/columnCount/headerData/data/setData`;
    `QgKeyValDelegate::paint()`
  - `QgAttributesModel.h`: `hasChildren`, `canFetchMore`, `fetchMore`
  - `QgEdFilter.h`: `eventFilter`
  - `QgTreeView.h`: `gObjDelegate::paint()`
  - Plugin headers: `QPolyCreate`, `QPolyMod`, `QEll`, `CADViewMeasure`, `CADViewSelector`
    — all `eventFilter` methods now carry `override`.
- ✅ **BSG P3 helpers already implemented**: `bsg_view_find_by_type()`,
  `bsg_scene_root_camera()`, `bsg_view_mat_aet_camera()`, and `bsg_sensor_fire()` are
  all present and functional in `src/libbsg/scene_graph.cpp`.  TODO updated accordingly.
- ✅ **AABB placeholder rendering** (Section 7.4): Initial implementation added
  `bbox_placeholders_` to `BViewState`.  Superseded in session 7 of DrawPipeline by
  the consolidated `bot_adaptive_plot()` placeholder path (AABB → OBB → LoD).
  The `bbox_placeholders_` map has been removed; all placeholder logic is now
  exclusively in `draw.cpp::bot_adaptive_plot()`.

### Completed (session 37)

- ✅ **`QgModel` ownership migration** (Tier 2):
  - `QgModel.h`: `std::unordered_set<QgItem *> *items` → value-type `std::unordered_set<QgItem *> items`
    (eliminates outer heap allocation + null check).  `QgItem *rootItem` → `std::unique_ptr<QgItem> rootItem_`
    (automatic lifetime management; `<memory>` added to header).
  - `QgModel.cpp`: removed `new`/`delete` for `items` and `rootItem`; all `items->` → `items.`;
    all `*items` range loops → direct; all `rootItem == ...` comparisons → `rootItem_.get() == ...`;
    all member accesses `rootItem->` → `rootItem_->`; `new QgItem(...)` → `std::make_unique<QgItem>(...)`;
    return sites in `root()`/`getItem()` use `rootItem_.get()`.
  - `QgTreeView.cpp`: `*m->items` → `m->items`; `m->items->find()` → `m->items.find()`.
  - All existing qgmodel tests pass.
- ✅ **Tier 3 `[[maybe_unused]]` migration**: C++ member/free functions in `libqtcad` migrated from
  `UNUSED()` macro to `[[maybe_unused]]` attribute.  C callbacks (`qgmodel_*`, `rt_cmd_*`,
  `*_record`, `uniq_test`) intentionally retained with `UNUSED()`.
- ✅ **`BViewState::link_to()` test**: `test_view_state_linking()` added to `test_dbi_cpp.cpp`.
  Exercises `link_to()`, `unlink()`, `is_linked()`, `linked_primary()`, and `add_hpath()`
  delegation.  All 8 new assertions pass.

### Completed (DrawPipeline sessions 2-3)

- ✅ **lod_mu_ starvation fix** (`libbsg/lod.cpp`): `bsg_mesh_lod_key_get` and
  `POPState::cache_get` now use local `MDB_txn*` + `MDB_RDONLY`, no mutex on read paths.
  `lod_worker` has a fast warm-cache path via `bsg_mesh_lod_key_get` pre-check.
  703/706 GenericTwin BoTs deliver LoD results in first `drain_geom_results()` call.
- ✅ **`draw_data_t::dbis` propagation**: Added `struct DbiState *dbis` to `draw_data_t`
  (`ged_private.h`).  `draw_gather_paths` propagates it to `ud->dbis`.  `gobjs.cpp` sets it.
- ✅ **`bsg_vlist_arb8()`** added to `libbsg/vlist.c` + `include/bsg/vlist.h`:
  draws 12 edges of an arb8 wireframe from 8 arbitrary corner points.
- ✅ **OBB placeholder wireframe** in `bot_adaptive_plot` (`draw.cpp`) and
  `BViewState::redraw()` (`dbi_state.cpp`): both now use `bsg_vlist_arb8` when OBB
  data is available in `dbis->obbs`, falling back to `bsg_vlist_rpp` (AABB).
- ✅ **DrawPipeline tests**: `drawpipeline_test` asserts 2242 bboxes + 706 OBBs + 3651
  drain results; `test_dbi_cpp` has 63 checks; new `bsg_vlist_arb8_cmd_cnt` test in
  `libbsg/tests/arb8.c` verifies the 18-command output of `bsg_vlist_arb8()`.

### Short-term (open)

*(no Tier 2 or Tier 3 items remaining)*

### Medium-term (modernisation)

2. **`fbserv.cpp` → `QLocalSocket`** (Tier 2): requires libpkg to gain Unix-domain-
   socket support before the Qt layer can be switched.  Investigate in a libpkg session.

3. **`QgConsole` tab completion review** (Tier 3): the stale TODO comments in the tab
   completion code have been removed.  The existing `split_slash` mechanism handles
   mid-string path completions.  Verify in an interactive session that `draw foo/ba<TAB>`
   completes correctly.  No known Qt6 API breakage remains.

### Longer-term (features)

4. **Primitive editing** (Tier 4): the largest single feature gap.  The design
   (Section 7.1) is clear; implementation is substantial but incremental.

---

## 9. Test Coverage Summary

| Suite | File | Checks | Notes |
|-------|------|--------|-------|
| DBI C surface | `src/libged/tests/test_dbi_c.c` | 35 | C API regression |
| DBI C++ | `src/libged/tests/test_dbi_cpp.cpp` | 63+ | DbiState/DrawList/SelectionSet/IDbiObserver/GeomLoader LoD |
| Qt model | `src/libqtcad/tests/qgmodel.cpp` | 15+ | QAbstractItemModelTester in Fatal mode |
| Draw rendering | `src/libged/tests/draw/basic.cpp` | — | BViewState::redraw pipeline |
| DrawPipeline | `src/libged/tests/draw/drawpipeline_test.cpp` | 3 | 2242 bboxes + 706 OBBs + 3651 drain results (GenericTwin.g) |
| libbsg vlist | `src/libbsg/tests/vlist.c` | 6 | bsg_vlist_cmd_cnt |
| libbsg arb8 | `src/libbsg/tests/arb8.c` | 1 | bsg_vlist_arb8 generates 18 vlist commands |

Gaps:
- No test for `GeomLoader` drain-pump behavior (would need mock `dbip`)
- ~~No test for `BViewState::link_to()` draw sharing~~ — ✅ Added `test_view_state_linking()` to `test_dbi_cpp.cpp` (session 37): covers `link_to()`/`unlink()`/`is_linked()`/`linked_primary()`/delegation via `add_hpath()`
- No integration test for `IDbiObserver` → `QgModel` → repaint cycle
