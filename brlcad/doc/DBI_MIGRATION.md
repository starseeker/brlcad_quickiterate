# DBI Modernization — Migration Guide

This document describes how to migrate code that used the old `dbi.h` API
(pre-modernization) to the new modernized API.  See `DBI_TODO.md` for the
full design rationale.

---

## 1. Typed Hash Wrappers

**Old code** used bare `unsigned long long` for all hash values:

```cpp
unsigned long long ghash = ...;   // object hash
unsigned long long ihash = ...;   // instance hash
unsigned long long phash = ...;   // path hash
```

**New code** uses typed wrappers to prevent accidentally mixing hash spaces:

```cpp
GHash    ghash = { ... };   // hash of a .g object name
InstHash ihash = { ... };   // hash of a unique comb instance
PathHash phash = { ... };   // hash of a full path
```

The raw value is accessed via `.v`:
```cpp
unsigned long long raw = ghash.v;
```

All three types are usable as `unordered_map`/`unordered_set` keys — the
required `std::hash<>` specializations are provided in `dbi.h`.

---

## 2. Observer Interface (replaces manual `update()` + flag checking)

**Old pattern**:
```cpp
unsigned long long flags = dbis->update();
if (flags & GED_DBISTATE_DB_CHANGE) {
    // do tree reset
}
if (flags & GED_DBISTATE_VIEW_CHANGE) {
    // trigger redraw
}
```

**New pattern** — implement `IDbiObserver` and register with `DbiState`:

```cpp
class MyObserver : public IDbiObserver {
public:
    void on_dbi_changed(const std::vector<DbiChangeEvent> &events) override {
        bool needs_reset = false;
        for (const auto &ev : events) {
            if (ev.batch) { needs_reset = true; break; }
            switch (ev.kind) {
                case DbiChangeKind::ObjectAdded:
                case DbiChangeKind::ObjectRemoved:
                case DbiChangeKind::CombTreeChanged:
                    needs_reset = true; break;
                case DbiChangeKind::ObjectModified:
                case DbiChangeKind::AttributeChanged:
                    // incremental update possible
                    handle_object_change(ev.object);
                    break;
                default: break;
            }
        }
        if (needs_reset) do_full_reset();
    }
    // ...
};

// Registration (typically in constructor):
DbiState *dbis = (DbiState *)gedp->dbi_state;
dbis->add_observer(&my_observer);

// De-registration (typically in destructor):
dbis->remove_observer(&my_observer);
```

`DbiChangeKind` values:
| Value | Meaning |
|---|---|
| `ObjectAdded` | A new object was added to the .g database |
| `ObjectRemoved` | An existing object was deleted |
| `ObjectModified` | An existing object's geometry was changed |
| `CombTreeChanged` | A comb's tree (children) changed |
| `AttributeChanged` | An object attribute changed (color, region_id, etc.) |
| `BatchRebuild` | A full rebuild occurred; treat all data as potentially changed |

---

## 3. GObj / CombInst Object Model (replaces parallel flat maps)

**Old code** accessed database structure via parallel hash maps:

```cpp
DbiState *dbis = ...;
// Is this a comb?  Check p_c:
auto it = dbis->p_c.find(hash);
if (it != dbis->p_c.end()) { /* it's a comb */ }

// Get children in order:
auto &children = dbis->p_v[hash];

// Get matrix:
auto &mat_row = dbis->matrices[parent_hash][child_hash];

// Get color:
auto &rgb_val = dbis->rgb[hash];
```

**New code** uses the `GObj` and `CombInst` object model:

```cpp
DbiState *dbis = ...;
// GObj and CombInst are additive alongside existing maps in this transition.
// Future phases will replace the maps entirely.
```

The `GObj` class holds:
- `name` — object name string
- `hash` — object hash (`unsigned long long`)
- `c_inherit`, `region_id`, `region_flag` — standard attributes
- `color`, `color_set` — drawing color
- `cv` — ordered vector of `CombInst *` children (for combs)
- `dp` — pointer to the librt `struct directory`

The `CombInst` class holds:
- `cname`, `oname`, `iname` — parent comb, instanced object, unique instance names
- `chash`, `ohash`, `ihash` — hashes of the above
- `boolean_op` — boolean operation (DB_OP_UNION, DB_OP_SUBTRACT, DB_OP_INTERSECT)
- `m`, `non_default_matrix` — placement matrix

---

## 4. DrawList (replaces BViewState draw-list management)

**Old code** called BViewState methods to manage drawn paths:

```cpp
bvs->add_path("path/to/object");
bvs->erase_path(-1, argc, argv);
std::vector<std::string> drawn = bvs->list_drawn_paths(-1, true);
int state = bvs->is_hdrawn(-1, phash);
```

**New code** accesses the `DrawList` owned by `BViewState`:

```cpp
DrawList &dl = bvs->draw_list();

// Add a path
dl.add(path_hashes, 0 /*mode*/, nullptr /*no overrides*/);

// Remove a path
dl.remove(leaf_hash, -1 /*all modes*/);

// Clear everything
dl.clear();

// Query draw state
DrawState state = dl.query(path_hash, -1);
// DrawState::NOT_DRAWN, FULLY_DRAWN, or PARTIALLY_DRAWN

// Get drawn paths
auto paths = dl.drawn_path_hashes(-1 /*all modes*/);
```

The `DrawSettings` struct allows per-path overrides:
```cpp
DrawSettings ds;
ds.has_color = true;
ds.color = my_color;
ds.mode = 1; // shaded
dl.add(path_hashes, 1, &ds);
```

---

## 5. SelectionSet (replaces BSelectState)

**Old code** used `BSelectState`:

```cpp
BSelectState *sel = dbis->default_selected;
sel->select_path("path/to/obj", true);
bool is_sel = sel->is_selected(hash);
std::vector<std::string> paths = sel->list_selected_paths();
```

**New code** uses `SelectionSet`:

```cpp
SelectionSet *sel = dbis->get_selection_set(); // default set
sel->select("path/to/obj", true);
bool is_sel = sel->is_selected(path_hash);
std::vector<std::string> paths = sel->selected_paths();

// Named selection sets
dbis->add_selection_set("my_set");
SelectionSet *my_sel = dbis->get_selection_set("my_set");
dbis->remove_selection_set("my_set");
std::vector<std::string> names = dbis->list_selection_sets();
```

`BSelectState` remains available during the transition period.

---

## 6. QgModel Observer Registration

`QgModel` now implements `IDbiObserver` and automatically registers/deregisters
with `DbiState` in its constructor/destructor.

**Old code** relied on manual update triggering:
```cpp
unsigned long long flags = dbis->update();
if (flags & GED_DBISTATE_DB_CHANGE)
    mdl->g_update(gedp);
```

**New code** — `QgModel` receives `on_dbi_changed()` automatically.  The
`run_cmd()` call still triggers `dbis->update()` to populate the change sets,
but the observer notification then drives the model update.  No change needed
at call sites that were already calling `update()` before `QgModel::g_update()`.

---

## 7. LMDB → bu_cache

The internal drawing cache was previously implemented using LMDB directly
(via a local `ged_draw_cache` wrapper in `dbi_state.cpp`).  It has been
replaced with the standard BRL-CAD `bu_cache` API.

If you had code that depended on the `ged_draw_cache` struct or the internal
`cache_get`/`cache_write`/`cache_del` functions, update to use `bu_cache`
directly:

```c
#include "bu/cache.h"

struct bu_cache *c = bu_cache_open("/path/to/cache_db", 1, 0);

// Write
struct bu_cache_txn *txn = NULL;
bu_cache_write(data, data_size, "key", c, &txn);
bu_cache_write_commit(c, &txn);

// Read
struct bu_cache_txn *txn2 = NULL;
void *data = NULL;
size_t sz = bu_cache_get(&data, "key", c, &txn2);
// use data ...
bu_cache_get_done(&txn2);

// Delete
bu_cache_clear("key", c, NULL);

// Close
bu_cache_close(c);
```

---

## 8. Thread Model

Per the design decisions:
- A single background thread handles geometry loading
- Results are posted via `concurrentqueue.h` to the main thread
- All `DbiState` mutations must occur on the main thread
- `IDbiObserver::on_dbi_changed()` is called synchronously on the main thread
  from within `DbiState::update()`

---

## 9. Summary of Deprecated APIs

| Old API | New API | Notes |
|---|---|---|
| `unsigned long long` hash (bare) | `GHash`, `InstHash`, `PathHash` | Typed wrappers prevent mixing hash spaces |
| `DbiState::update()` return flags | `IDbiObserver::on_dbi_changed()` | Observer pattern replaces bitmask |
| `DbiState::p_c`, `p_v`, `d_map`, `matrices`, `rgb`, etc. | `GObj`, `CombInst` | Object model replaces parallel maps |
| `BViewState::add_path()`, `erase_path()`, etc. | `DrawList::add()`, `DrawList::remove()` | DrawList owns draw intent |
| `BSelectState` | `SelectionSet` | Cleaner API; BSelectState kept for transition |
| `ged_draw_cache` + LMDB | `bu_cache` API | Standard BRL-CAD cache mechanism |
| `QgModel::g_update()` at call sites | Automatic via `IDbiObserver` | QgModel self-registers as observer |
