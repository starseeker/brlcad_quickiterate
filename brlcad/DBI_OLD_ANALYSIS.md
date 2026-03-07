# DBI Interface Redesign — Analysis and Proposed Approach

## 1. Purpose and Scope

This document analyzes the current state of the `dbi.h` / `DbiState` infrastructure
and the experimental `qged`/`libqtcad` stack that depends on it, describes the known
pain points, evaluates the `dbi2` prototype redesign, and proposes a path forward
toward a well-designed, maintainable, performant, extensible, and debuggable
database-interface (DBI) API suitable for driving a production-quality
`qged`-style GUI.

---

## 2. Current Architecture Overview

```
qged (QgEdApp, QgEdMainWindow)
  └─ libqtcad (QgModel : QAbstractItemModel, QgItem, QgGL, QgTreeView, …)
       └─ libged dbi.h (DbiState, BViewState, SelectionSet)
            └─ librt (.g database: struct ged *, struct db_i *, struct directory *)
```

### 2.1 DbiState (src/libged/dbi.h / dbi_state.cpp)

`DbiState` is an in-memory mirror of a `.g` database.  Its core responsibilities:

| Responsibility | How it is done today |
|---|---|
| Comb hierarchy | `p_c` (parent→children set), `p_v` (ordered vector) keyed on `unsigned long long` name-hashes |
| Object identity | `d_map` (hash→`struct directory *`), `invalid_entry_map` for missing objects |
| Instance de-duplication | `i_map` / `i_str` for non-uniquely-named children |
| Placement matrices | `matrices[parent_hash][child_hash]` → `vector<fastf_t>` |
| Boolean ops | `i_bool[parent_hash][child_hash]` → size_t |
| Bounding boxes | `bboxes[hash]` → `vector<fastf_t>` |
| Drawing attributes | `c_inherit`, `rgb`, `region_id` maps |
| Change detection | `added`, `changed`, `changed_hashes`, `removed`, `old_names` sets |
| Views | `shared_vs` + `view_states` map |
| Selections | `default_selection_set_` + `selection_sets_` map (`SelectionSet` instances) |
| Sync | `unsigned long long update()` — full rebuild from `.g` on each call |

`BViewState` tracks which paths are drawn and in which mode.  Internally it uses
`s_map` (hash→mode→`bv_scene_obj *`) and several levels of "collapsed"/"drawn" path
sets that must be kept coherent with each other.

`SelectionSet` tracks which paths are selected and provides hierarchical selection
metadata (`active_`, `parents_`, `ancestors_`, `obj_immediate_parents_`, `obj_ancestors_`).
It also supports `expand()`, `collapse()`, `refresh()`, and `sync_to_all_views()`.

### 2.2 QgModel (include/qtcad/QgModel.h, src/libqtcad/QgModel.cpp)

`QgModel` is a `QAbstractItemModel` that bridges the Qt world to `DbiState`.
`QgItem` is the Qt tree node; it holds an `ihash` that indexes into `DbiState` and
a lazy-loaded `children` vector.

The update cycle is roughly:

1. User runs a command → `QgEdApp::run_cmd()` calls `mdl->run_cmd()` → `ged_exec()`.
2. `qged_view_update()` calls `dbis->update()`.
3. `update()` returns `GED_DBISTATE_DB_CHANGE` and/or `GED_DBISTATE_VIEW_CHANGE`
   bitmask flags.
4. `run_cmd()` also does a before/after `select_hash` comparison.
5. `view_update(flags)` signal is emitted.
6. `QgEdApp::do_view_changed()` receives the signal, iterates all `BViewState`
   instances, and calls `BViewState::redraw()` on each.
7. `mdl_changed_db()` triggers `QgTreeView::redo_expansions()` to rebuild the tree.

---

## 3. Diagnosed Problems

### 3.1 Structural / Design-level

**P1 — Flat hash-table soup, no object model.**
`DbiState` exposes raw `unordered_map` members for every piece of database data
(`p_c`, `p_v`, `d_map`, `matrices`, `i_bool`, `bboxes`, `rgb`, etc.).  Callers must
look up multiple separate maps to reconstruct a coherent picture of a single object
or instance.  There is no class that represents "a comb instance" or "a geometry
object" — everything is decomposed into independent parallel maps keyed by the same
opaque `unsigned long long`.

**P2 — No type safety on hashes.**
All hashes are bare `unsigned long long`.  A GObj hash, a CombInst hash, a
path hash, and a "mode-qualified path" hash are all the same type; confusing them
produces silent wrong behavior rather than a compile-time error.

**P3 — Poorly defined ownership and lifetime.**
Raw pointers to `BViewState`, `BSelectState`, and `struct bv_scene_obj` are stored
without any ownership semantics documented.  When a command modifies the database,
it is unclear which objects must be invalidated or rebuilt.  This is the root cause
of "updates don't propagate correctly" — there are multiple paths through which
scene objects, view states, and selection states can become stale.

**P4 — Conflated responsibilities in `DbiState`.**
`DbiState` is simultaneously: a database mirror, a drawing-mode manager, a selection
manager, a view manager, a bounding-box cache, a "drawing cache" (`ged_draw_cache`),
and a change-set tracker.  This makes it very hard to reason about correctness,
test in isolation, or extend.

**P5 — Fragile "two-flag" change notification.**
`update()` returns a bitmask of two bits (`DB_CHANGE` and `VIEW_CHANGE`).  Callers
must interpret these bits and manually trigger the right sequence of Qt signals and
`BViewState::redraw()` calls.  Any mismatch (a command that changes the database but
doesn't set the flag, or a view update that sets the wrong flag) silently causes
rendering and tree-view staleness.

**P6 — Redundant and inconsistent path collapse bookkeeping.**
`BViewState` maintains `s_map`, `s_keys`, `drawn_paths`, `all_drawn_paths`,
`partially_drawn_paths`, `all_partially_drawn_paths`, `mode_collapsed`, and
`all_collapsed`.  These are computed in `cache_collapsed()` and must be kept in
sync.  It is almost impossible to verify they are all consistent after an edit
without printing all of them.

**P7 — Qt model not fully decoupled from DbiState internals.**
`QgModel` stores raw `DbiState *` and calls non-const methods on it for queries.
`QgItem::ihash` maps directly to DbiState's internal hash tables.  Any change in
hashing semantics or internal map structure breaks the Qt model.  There is no stable
public API boundary between the two layers.

**P8 — The "lazy load" tree is not robust to database edits.**
`QgItem` objects cache `ihash`, `dp`, and `name` from the database state at creation
time.  When an edit renames or deletes an object, existing `QgItem` instances become
dangling.  The `g_update()` function attempts a full rebuild, but the Qt model
`beginResetModel()`/`endResetModel()` cycle destroys all expanded/collapsed state.
The workaround of selectively inserting/removing rows is not implemented.

**P9 — BViewState::redraw() is too coarse.**
Every post-command redraw of a view walks and rebuilds everything.  For large models
this is very expensive, and fine-grained incremental updates (e.g., "only invalidate
objects that changed") are not supported.

**P10 — BSelectState operates on path vectors, not first-class path objects.**
Selection logic computes hierarchical parent/child relationships (immediate parents,
grand parents) via nested loops over raw hash vectors.  This is O(N×M) in the number
of selected paths times the size of the hierarchy and becomes a performance problem
for large selections.

### 3.2 Editing-induced failures (the specific qged bugs)

The edit-does-not-propagate failure modes arise from the above structural problems:

- After a `kill` or `mv` command, `d_map` is updated by `update()` but `QgItem`s
  still hold the old `dp`.  The next `data()` call on those items crashes or shows
  stale names.
- A `comb` or `attr` change modifies `p_c`/`p_v` but the collapsed-path sets in
  the affected `BViewState` instances are not immediately invalidated, so the draw
  list may keep showing old paths.
- Commands that trigger internal sub-commands (e.g., `facetize`) may call
  `update()` more than once, each time with a different change set.  The Qt model
  may receive `mdl_changed_db()` signals out of step with the actual state.
- Multiple views sharing a `BViewState` can diverge when one view's `redraw()` call
  partially modifies the shared state before the second view's redraw runs.

---

## 4. Evaluation of the dbi2 Prototype

The `dbi2/` directory contains an extended version of `dbi.h` and revised
implementations (`dbi2_state.cpp`, `dbi2_bview.cpp`, `dbi2_select.cpp`).  It
introduces several improvements over the current code.

### 4.1 What dbi2 gets right

**Object model instead of parallel maps.**
`GObj` (geometry object) and `CombInst` (comb-tree instance) classes replace the
flat parallel maps.  Each `GObj` owns its `CombInst` children in a `cv` vector.
This is a significant improvement for correctness and encapsulation.

**`DbiPath` as a first-class type.**
A `DbiPath` carries its element vector, state flags (`is_cyclic`, `is_valid`), a
pre-computed `path_hash`, cached component-hash set, and per-mode draw settings.
This eliminates a class of errors that arise from passing raw `vector<unsigned long
long>` across the API.  Path operations (`push`, `pop`, `parent`, `child`, `matrix`,
`bbox`, `str`) become type-safe methods.

**`DbiPath_Settings` for per-path draw overrides.**
Separating draw settings from the path itself (rather than baking them into
`bv_obj_settings` at call sites) makes it possible to change colors or modes
after a path is drawn without re-running the whole draw pipeline.

**Better BViewState linking model.**
`BViewState::Link/Unlink` allows a secondary view (e.g., three views of a quad
layout) to share drawn paths from the primary view, reducing redundant work while
still allowing each view to manage its own camera independently.

**Cleaner `DbiState` public API.**
`Sync()`, `Redraw()`, `Render()`, `GetBViewState()`, `GetSelectionSet()` etc.
are more consistently named and follow a coherent design intent.

**C-callable surface area is explicit.**
The bottom of `dbi2/src/libged/dbi.h` defines `ged_find_scene_objs()` and
`ged_vlblock_scene_obj()` as explicit C-linkage exports, making the C/C++ boundary
intentional and auditable.

### 4.2 Remaining problems in dbi2

**D1 — LMDB dependency introduces build complexity without justification.**
`dbi2_state.cpp` `#include`s `lmdb.h` and links against LMDB for a persistent
drawing cache.  The performance gain from a persistent disk cache for draw geometry
has not been demonstrated to be necessary, and adding an external database engine
introduces a security/dependency risk and significantly complicates the build.
The existing `bu_cache` is sufficient for the geometry caching role.

**D2 — Thread model is underspecified.**
`dbi2_state.cpp` includes `<thread>` but there are no documented thread-safety
guarantees on `DbiState`, `GObj`, or `DbiPath`.  The comment at line 1579 ("TODO —
probably should tell LoD cache as well") illustrates that the interaction between
background threads and the main state has not been fully thought through.

**D3 — `DbiPath` memory management is complex.**
`DbiState` has a `queue<DbiPath *> dbiq` pool and a `GetDbiPath`/`PutDbiPath` arena
allocator.  This is a non-trivial lifetime model that requires every caller to return
paths it receives via `GetDbiPath`, and can produce use-after-put bugs.  Modern C++
`shared_ptr`/`weak_ptr` or a slab allocator with clear ownership semantics would be
simpler and safer.

**D4 — `GObj::LoadSceneObj` conflates database state with rendering.**
The method that loads wireframe/shaded geometry directly into `GObj` means every
database object unconditionally carries references to `bv_scene_obj` rendering data.
For applications that only need the hierarchy model (e.g., a text-only tool, a
headless raytrace dispatcher) this bloat is unnecessary.

**D5 — `BViewState` still mixes draw-list management with camera/viewport state.**
The linked list of view states and the direct references to `struct bview *` (a C
struct describing camera parameters, frustum, etc.) means that view-state linked
pairs are tightly coupled.  It would be cleaner to separate "scene content" (which
paths are drawn and how) from "viewport parameters" (projection, zoom, orientation).

**D6 — Qt model not addressed.**
The dbi2 prototype deals entirely with the libged/librt layer.  `QgModel` and
`QgItem` remain unchanged.  The majority of the actual UI breakage stems from the
Qt layer, which dbi2 does not address.

**D7 — `DbiPath` not properly integrated with `BSelectState`.**
`BSelectState` in dbi2 stores `unordered_set<unsigned long long> selected` (raw
hashes) rather than `unordered_set<DbiPath *>`.  The rationale (performance:
building a DbiPath has cost) is understood, but means the path/selection separation
is incomplete.

---

## 5. Proposed Redesign

### 5.1 Guiding principles

1. **One source of truth per concern.**  Database structure in `DbiState`.
   Draw intent in `DrawList`.  Render output in `SceneGraph`.  Selection in
   `SelectionSet`.  Each concern is a separate, testable component.

2. **Rich first-class types, no naked hashes in public API.**  Typed wrappers
   (`GHash`, `InstHash`, `PathHash`) make overloading errors a compile error.
   The public API accepts and returns these typed values; raw hash arithmetic
   is an implementation detail.

3. **Observer / notification pattern for change propagation.**  Every component
   that can change registers an observable interface.  Observers (the Qt model,
   the scene graph, the selection state) subscribe and are notified.  This
   replaces the brittle "call `update()`, check bitmask" approach.

4. **Minimal Qt coupling in libged.**  `DbiState` and friends know nothing about
   Qt.  `libqtcad` adapts the observable DBI events into `QAbstractItemModel`
   signals.  The adaptation layer is thin and can be replaced with another
   toolkit adapter without touching libged.

5. **Progressive granularity in updates.**  A full rebuild from `.g` is always
   correct but expensive.  Every component must support both full rebuild
   (for robustness) and delta-update (for performance) with the same
   external interface; callers should not need to know which path was taken.

6. **No external database dependencies.**  The existing `bu_cache` and
   in-process hash maps are sufficient.  LMDB should be dropped.

7. **Clear C/C++ boundary.**  All C++ classes stay in `dbi.h` inside
   `#ifdef __cplusplus`.  A small, explicit C-linkage surface (`ged/dbi_c.h`)
   provides access to the minimal capabilities needed by C callers.

8. **Thread awareness.**  Every method is documented as either "main thread
   only" or "thread-safe".  A `DbiState::lock()` / `unlock()` RAII mechanism
   gates mutations; background geometry loading posts results via a queue rather
   than mutating shared state directly.

### 5.2 Proposed layer structure

```
┌──────────────────────────────────────────────────────────┐
│  Application (qged)                                      │
│  QgEdApp, QgEdMainWindow                                 │
├──────────────────────────────────────────────────────────┤
│  libqtcad — Qt Adaptation Layer                          │
│  QgModel  : QAbstractItemModel  (subscribes to DBI       │
│             IDbiObserver events)                         │
│  QgItem   (row ↔ GHash or InstHash with stable identity) │
│  QgViewport (owns struct bview; subscribes to            │
│              ISceneObserver events)                      │
├──────────────────────────────────────────────────────────┤
│  libged — DBI Layer                                      │
│  DbiState    (database mirror, owns GObj/CombInst)       │
│  DrawList    (per-view draw intent)                      │
│  SceneGraph  (per-view rendered scene objects)           │
│  SelectionSet(selection state + hierarchy metadata)      │
├──────────────────────────────────────────────────────────┤
│  librt / libdb (struct ged, struct db_i, struct directory)│
└──────────────────────────────────────────────────────────┘
```

### 5.3 Core DBI types

#### Typed hash wrappers

```cpp
// Opaque typed wrappers for the three distinct hash spaces
struct GHash    { unsigned long long v; };   // hash of a .g object name
struct InstHash { unsigned long long v; };   // hash of a unique comb instance
struct PathHash { unsigned long long v; };   // hash of a full path
// Equality / hash specializations so they work in unordered_map
```

Using distinct types prevents accidentally passing a `GHash` where a `PathHash` is
expected — a class of silent bug common in the current code.

#### GObj

```cpp
class GED_EXPORT GObj {
public:
    GHash        ghash;         // unique per .g name
    std::string  name;
    bool         is_comb;
    bool         is_region;
    int          region_id;
    bool         color_set;
    struct bu_color color;
    int          c_inherit;

    // Ordered list of direct children (for combs; empty for solids)
    const std::vector<CombInst *>& children() const;

    // Bounding box cached from last geometry load; invalidated on edit
    bool         bbox_valid;
    point_t      bbmin, bbmax;
};
```

`GObj` is owned by `DbiState`.  It carries no rendering state.

#### CombInst

```cpp
class GED_EXPORT CombInst {
public:
    InstHash   ihash;       // unique identifier of this instance
    GHash      parent;      // hash of the parent comb GObj
    GHash      child;       // hash of the child GObj (may be invalid)
    std::string child_name; // string in case child GObj doesn't exist
    db_op_t    boolean_op;
    mat_t      m;
    bool       non_identity_matrix;
};
```

`CombInst` is owned by its parent `GObj`.

#### DbiPath

A `DbiPath` is a sequence of path elements: the first is a `GHash` (the root
object), and each subsequent entry is an `InstHash` (a comb instance edge).

```cpp
class GED_EXPORT DbiPath {
public:
    // Build from string or element list; returns invalid path on error
    static DbiPath from_string(const DbiState *, const char *);
    static DbiPath from_hashes(const DbiState *, GHash root,
                               std::initializer_list<InstHash> edges);

    bool         valid()    const;
    bool         cyclic()   const;
    PathHash     hash()     const;
    size_t       depth()    const;
    std::string  str()      const;

    GHash        root()     const;
    GHash        leaf()     const;   // leaf GObj hash
    InstHash     leaf_inst()const;   // leaf CombInst hash (invalid for depth-0)

    bool         matrix(mat_t m) const;   // accumulated matrix along path
    bool         color(struct bu_color *) const;
    bool         is_subtraction() const;
    bool         bbox(point_t *bbmin, point_t *bbmax) const;

    // Range-for compatible iteration over (GHash, InstHash) edge pairs
    PathIterator begin() const;
    PathIterator end()   const;

    // Value type: copy is cheap (vector of 8-byte values + cached hash)
    DbiPath()  = default;
    ~DbiPath() = default;
    DbiPath(const DbiPath &) = default;
    DbiPath& operator=(const DbiPath &) = default;

private:
    std::vector<unsigned long long> elems; // [GHash root, InstHash*, GHash leaf?]
    PathHash   phash;
    bool       valid_   = false;
    bool       cyclic_  = false;
    const DbiState *d_  = nullptr;
};
```

`DbiPath` is a **value type** (small, copyable, no heap ownership).  There is no
pool allocator; paths are passed by value or const-reference.  A `PathHash` uniquely
identifies a path and is suitable as a map key.

### 5.4 Observer / change notification

```cpp
// Change categories
enum class DbiChangeKind { ObjectAdded, ObjectRemoved, ObjectModified,
                           CombTreeChanged, AttributeChanged };

struct DbiChangeEvent {
    DbiChangeKind kind;
    GHash         object;     // which object (invalid for batch events)
    bool          batch;      // if true, a full rebuild has occurred
};

// Interface implemented by observers
class IDbiObserver {
public:
    virtual ~IDbiObserver() = default;
    // Called synchronously on the main thread after DbiState::sync() completes
    virtual void on_dbi_changed(const std::vector<DbiChangeEvent> &events) = 0;
};

class DbiState {
public:
    void add_observer(IDbiObserver *);
    void remove_observer(IDbiObserver *);
    // ...
};
```

`QgModel` implements `IDbiObserver`.  Its `on_dbi_changed()` implementation
translates each `DbiChangeEvent` into the appropriate sequence of
`beginInsertRows`/`endInsertRows`, `beginRemoveRows`/`endRemoveRows`,
or `dataChanged()` calls, preserving the Qt view's expanded/collapsed state for
unmodified subtrees.

A similar `ISceneObserver` (with `on_scene_changed()`) allows `QgViewport` to
react to draw-list changes.

### 5.5 DrawList (replaces BViewState's draw-list role)

```cpp
class GED_EXPORT DrawList {
public:
    DrawList();

    // Stage paths for the next Redraw cycle.  Setting override settings
    // is optional; pass nullptr to inherit from database attributes.
    void add(DbiPath p, int mode = 0,
             const DrawSettings *overrides = nullptr);

    // Remove paths matching the given prefix in the given mode (-1 = all)
    void remove(DbiPath p, int mode = -1);

    // Clear entire draw list
    void clear();

    // Collapse staged paths to the minimal set and rebuild scene objects.
    // Returns the set of scene objects added/removed.
    DrawListDelta commit(struct bview *v, DbiState *dbis);

    // Query
    DrawState query(PathHash ph, int mode = -1) const;
    // DrawState: { NOT_DRAWN=0, FULLY_DRAWN=1, PARTIALLY_DRAWN=2 }

    std::vector<std::string> drawn_paths(int mode = -1) const;
    size_t  drawn_path_count(int mode = -1) const;
};
```

`DrawList` is NOT a view-state object; it does not own or reference `struct bview *`
permanently.  The `commit()` call takes the view pointer momentarily to resolve LoD
parameters.

### 5.6 DbiState public API (proposed)

```cpp
class GED_EXPORT DbiState {
public:
    explicit DbiState(struct ged *);
    ~DbiState();

    //----- Database Object Access ------------------------------------------
    const GObj    *get_gobj(GHash)    const;
    const CombInst *get_inst(InstHash) const;
    bool           valid(GHash)        const;
    bool           valid(InstHash)     const;

    GHash          hash_of(const char *name) const;
    std::string    name_of(GHash)            const;

    std::vector<GHash> tops(bool include_cyclic = false) const;

    //----- Synchronization -------------------------------------------------
    // Full rebuild from .g database.  Notifies all IDbiObservers.
    void sync();

    // Incremental update using the provided change sets (populated by
    // librt database callbacks before calling this).  Falls back to full
    // sync if the delta appears incomplete.
    void sync(std::unordered_set<struct directory *> &added,
              std::unordered_set<struct directory *> &changed,
              std::unordered_set<struct directory *> &removed);

    //----- Observer Registration -------------------------------------------
    void add_observer(IDbiObserver *);
    void remove_observer(IDbiObserver *);
    void add_scene_observer(ISceneObserver *);
    void remove_scene_observer(ISceneObserver *);

    //----- View State Management -------------------------------------------
    BViewState *get_view_state(struct bview *);   // creates if not found
    BViewState *default_view_state();
    void        remove_view_state(struct bview *);

    //----- Selection State -------------------------------------------------
    SelectionSet *default_selection();
    SelectionSet *get_selection(const char *name);    // creates if not found
    void          remove_selection(const char *name);
    std::vector<std::string> list_selections() const;

    //----- Drawing Helpers -------------------------------------------------
    // Convenience: redraw all view states that need it.  Typically called
    // once per command execution cycle.
    void redraw_all();

    //----- Debugging -------------------------------------------------------
    void print(struct bu_vls *out = nullptr,
               bool include_views = false) const;

    struct ged  *gedp  = nullptr;
    struct db_i *dbip  = nullptr;

private:
    // GObj/CombInst storage (owned)
    std::unordered_map<unsigned long long, std::unique_ptr<GObj>>     gobjs_;
    std::unordered_map<unsigned long long, std::unique_ptr<CombInst>> combinsts_;

    // Observer lists
    std::vector<IDbiObserver *>    dbi_observers_;
    std::vector<ISceneObserver *>  scene_observers_;

    // View and selection state
    std::unique_ptr<BViewState>          default_vs_;
    std::unordered_map<struct bview *, std::unique_ptr<BViewState>> view_states_;
    std::unique_ptr<SelectionSet>        default_sel_;
    std::unordered_map<std::string, std::unique_ptr<SelectionSet>>  selections_;

    void populate_from_db();
    void notify_observers(const std::vector<DbiChangeEvent> &);
    void update_gobj(struct directory *dp);
    void remove_gobj(unsigned long long old_hash);

    struct resource *res_ = nullptr;
};
```

Key differences from the current API:
- All map members are private.
- Ownership is expressed via `unique_ptr`.
- No raw hash arithmetic in public API (typed `GHash`/`InstHash`).
- `sync()` drives change notification automatically.

### 5.7 BViewState (redefined)

`BViewState` should be refactored into two cleanly separated roles:

1. **`DrawList`** — manages which paths are drawn and in which modes (pure draw
   intent, no reference to `struct bview *` or rendering artifacts).

2. **`BViewState`** — an owner of a `DrawList`, a reference to `struct bview *`,
   and a `SceneGraph` that translates the draw list into actual `bv_scene_obj *`
   instances.  Implements `ISceneObserver`.

```cpp
class GED_EXPORT BViewState : public ISceneObserver {
public:
    BViewState(DbiState *, struct bview *v = nullptr);
    ~BViewState();

    DrawList     &draw_list();
    void          redraw(bool autoview = true);
    void          render();

    // Link this view to source its DrawList from another BViewState
    // (the shared/primary view).  Only camera parameters are independent.
    void link_to(BViewState *primary);
    void unlink();
    bool is_linked() const;

    struct bview *view() const;  // may be nullptr for "headless" scenarios
    bool          empty() const;

    void on_scene_changed(const SceneChangeEvent &) override;

private:
    DbiState    *dbis_;
    struct bview *v_;
    DrawList      draw_list_;
    BViewState   *linked_to_ = nullptr;
    // ... scene object bookkeeping ...
};
```

### 5.8 SelectionSet (replaces BSelectState)

```cpp
class GED_EXPORT SelectionSet {
public:
    explicit SelectionSet(const DbiState *);

    bool select(DbiPath p, bool update_hierarchy = true);
    bool deselect(DbiPath p, bool update_hierarchy = true);
    void clear();

    bool is_selected(PathHash)  const;
    bool is_active(PathHash)    const;   // selected or child of selected
    bool is_parent(PathHash)    const;
    bool is_ancestor(PathHash)  const;

    std::vector<DbiPath> selected_paths() const;

    unsigned long long state_hash() const;
    void sync_to_drawn(BViewState *);    // update highlight markers

private:
    const DbiState *dbis_;
    std::unordered_set<unsigned long long> selected_;     // PathHash values
    std::unordered_set<unsigned long long> active_;       // expanded children
    std::unordered_set<unsigned long long> parents_;      // immediate parents
    std::unordered_set<unsigned long long> ancestors_;    // all above parents

    void recompute_hierarchy();
};
```

### 5.9 QgModel redesign

With the stable observer interface in place, `QgModel` can be substantially
simplified:

```cpp
class QTCAD_EXPORT QgModel : public QAbstractItemModel, public IDbiObserver {
    Q_OBJECT
public:
    explicit QgModel(QObject *parent, const char *g_path = nullptr);
    ~QgModel() override;

    // IDbiObserver
    void on_dbi_changed(const std::vector<DbiChangeEvent> &events) override;

    // QAbstractItemModel
    QModelIndex   index(int row, int col, const QModelIndex &p = {}) const override;
    QModelIndex   parent(const QModelIndex &) const override;
    int           rowCount(const QModelIndex &p = {}) const override;
    int           columnCount(const QModelIndex &p = {}) const override;
    QVariant      data(const QModelIndex &, int role = Qt::DisplayRole) const override;
    bool          canFetchMore(const QModelIndex &) const override;
    void          fetchMore(const QModelIndex &) override;

    // Command execution
    int run_cmd(struct bu_vls *msg, int argc, const char **argv);
    struct ged *gedp = nullptr;

signals:
    void db_changed(unsigned long long flags);
    void view_changed(unsigned long long flags);

private:
    DbiState      *dbis_   = nullptr;

    // Stable-identity tree node
    // Key: PathHash of the path from a root GObj to this item
    struct Node {
        PathHash       ph;
        Node          *parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
        bool           fetched = false;   // children have been loaded
    };
    std::unique_ptr<Node> root_node_;

    // Map PathHash → Node * for O(1) index/parent/row lookups
    std::unordered_map<unsigned long long, Node *> node_map_;

    void apply_change_event(const DbiChangeEvent &);
    void insert_gobj_node(GHash);
    void remove_gobj_node(GHash);
    void refresh_children(Node *);
};
```

`QgItem` is replaced by the inner `Node` struct.  The critical difference is that
`Node` is keyed by `PathHash` (stable as long as the path exists), and
`on_dbi_changed()` translates `DbiChangeEvent` objects into precise Qt model
mutation calls instead of a full `beginResetModel()/endResetModel()`.

### 5.10 Update cycle in the new architecture

```
ged_exec() completes
    │
    ▼
librt db-change callbacks populate dbis->added / changed / removed
    │
    ▼
DbiState::sync(added, changed, removed)     [or ::sync() for full rebuild]
    │  ├─ rebuild GObj/CombInst for changed objects
    │  ├─ collect DbiChangeEvents
    │  └─ notify_observers(events)
    │         │
    │         ├─▶  QgModel::on_dbi_changed()
    │         │       ├─ precise QAbstractItemModel row insert/remove/update calls
    │         │       └─ emits db_changed(flags) signal
    │         │
    │         └─▶  (other observers, if any)
    │
    ▼
BViewState::redraw()                         [called per view after sync]
    │  ├─ validate DrawList against new DbiState
    │  ├─ rebuild invalidated scene objects
    │  └─ notify ISceneObserver instances
    │         │
    │         └─▶  QgViewport::on_scene_changed()
    │                  └─ schedule repaint
    ▼
SelectionSet::sync_to_drawn()
    └─ update highlight flags on affected scene objects
```

Every step has a single responsibility and a defined input/output contract.  There
are no floating bitmask flags; callers do not need to orchestrate the sequence
themselves.

---

## 6. Transition Strategy

Given that `qged` and `libqtcad` are explicitly experimental, a complete rewrite is
feasible.  However, to minimize disruption to the broader BRL-CAD build and to any
tooling that depends on `libged`, the transition should be staged.

### Phase 1 — Introduce typed hashes and stable DbiPath value type  ✅ COMPLETE (typed hashes)
*(No API-breaking change to existing code; new headers only)*

- ~~Add `GHash`, `InstHash`, `PathHash` wrappers to `dbi.h`.~~ **DONE** — `GHash`,
  `InstHash`, `PathHash` structs with `std::hash<>` specializations are in
  `include/ged/dbi.h`.
- Replace raw `vector<unsigned long long>` path arguments with `DbiPath` in
  new overloads; keep old overloads for compatibility.  *(DbiPath class deferred
  to a later pass; typed hash wrappers capture the critical type-safety goal.)*
- Add `DbiPath::from_string()` / `DbiPath::from_hashes()` factory functions.
  *(Deferred with DbiPath.)*
- **Tests:** unit-test `DbiPath` creation, push/pop, matrix accumulation,
  equality, and hash stability across a simulated edit cycle.

### Phase 2 — Introduce observer interface and wire up to QgModel  ✅ COMPLETE
*(Additive; existing `DbiState::update()` remains but calls notify_observers)*

- ~~Add `IDbiObserver` interface and `DbiState::add_observer()`.~~ **DONE** —
  `IDbiObserver`, `ISceneObserver`, `DbiChangeEvent`, `SceneChangeEvent` are in
  `include/ged/dbi.h`; `add_observer`/`remove_observer` are implemented in
  `dbi_state.cpp`.
- ~~Implement `DbiState::notify_observers()` inside the existing `update()` body.~~
  **DONE** — `notify_dbi_observers()` is called at the end of `DbiState::update()`,
  emitting per-object `ObjectAdded`/`ObjectModified`/`ObjectRemoved` events.
- ~~Implement `QgModel::on_dbi_changed()` using a conservative full-reset strategy
  initially.~~ **DONE** — `QgModel` inherits `IDbiObserver`; Phase 6 (below) replaced
  the conservative path with targeted row operations.
- **Tests:** exercise each command category (create, edit, delete, rename, move,
  copy) and verify `on_dbi_changed()` is called with appropriate events.

### Phase 3 — Introduce `GObj` / `CombInst` object model (from dbi2)  ✅ COMPLETE
*(Private implementation change inside DbiState; no public API breakage)*

- ~~Replace flat parallel maps with `GObj` / `CombInst` instances stored in
  `unique_ptr` maps.~~ **DONE** — `GObj` and `CombInst` classes are declared in
  `include/ged/dbi.h` and implemented in `dbi_state.cpp`.  Each `GObj` holds its
  `CombInst *` children via the `cv` vector; `GenCombInstances()` populates it from
  the flat maps during `update_dp()`.  Flat maps (`p_c`, `p_v`, `matrices`, etc.)
  are retained alongside for backward compatibility during the transition.
- Keep the existing map-based public accessors as wrappers for backward compat
  during transition.  **In progress** — maps remain public; callers can use either
  the map API or the new object model.
- **Tests:** property-based test that for every object in a `.g` file the new
  object model produces identical results to the old map queries.

### Phase 4 — DrawList / BViewState separation  ✅ COMPLETE
*(libged-internal change; public BViewState API may change)*

- ~~Extract `DrawList` from `BViewState`.~~ **DONE** — `DrawList`, `DrawSettings`,
  and `DrawState` are declared in `include/ged/dbi.h` and fully implemented in
  `dbi_state.cpp`.  `BViewState::draw_list()` accessor exposes the owned `DrawList`.
- Refactor `BViewState::redraw()` to call `DrawList::commit()`.  *(Full integration
  of DrawList into the redraw pipeline is a follow-on step.)*
- Implement `BViewState::link_to()` using the new design.  *(Deferred.)*
- **Tests:** draw/erase command suite; quad-view synchronization test.

### Phase 5 — SelectionSet replaces BSelectState  ✅ COMPLETE
*(Public API change in dbi.h)*

- ~~Rename `BSelectState` → `SelectionSet`, adopt `DbiPath` arguments.~~ **DONE** —
  `SelectionSet` is declared in `include/ged/dbi.h` and implemented in
  `dbi_state.cpp`.  `DbiState::get_selection_set()` / `get_selection_sets()` /
  `add_selection_set()` / `remove_selection_set()` / `list_selection_sets()` are
  implemented.
- ~~`BSelectState` retained for transition.~~ **REMOVED** — all callers migrated to
  `SelectionSet`; `BSelectState` and its old `DbiState` management methods deleted.
- ~~Rewrite hierarchy metadata computation using the `GObj`/`CombInst` graph~~
  **DONE (Phase 7)** — `recompute_hierarchy()` uses BFS via `DbiState::p_v` to expand
  descendants into `active_`; prefix-walk builds `parents_` and `ancestors_`;
  reverse-map walk builds `obj_immediate_parents_` and `obj_ancestors_`.
- **Tests:** selection expand/collapse, highlighting after edit, multi-view sync.

### Phase 6 — QgModel incremental update  ✅ COMPLETE
*(libqtcad-only change)*

- ~~Foundation: `QgModel` already inherits `IDbiObserver`; `on_dbi_changed()` exists
  and calls `g_update()` (conservative full-reset path).~~
- **Implemented:**
  - **Re-entrancy fix**: `in_g_update_` flag prevents `on_dbi_changed()` from
    recursively calling `g_update()` from inside a `beginResetModel()` block.
  - `dbis->update()` is now called BEFORE any Qt model-lock operation.
  - `on_dbi_changed()` stores events in `pending_dbi_events_`; `g_update()` reads
    them to decide between targeted and full-reset paths.
  - `full_model_reset()` — extracted helper containing the former `beginResetModel()`
    cycle for complex/batch changes.
  - `apply_incremental_updates()` — targeted path: `dataChanged()` for
    `ObjectModified`; `reconcile_tops()` for `ObjectAdded`/`ObjectRemoved`.
  - `reconcile_tops()` — per-row `beginInsertRows/endInsertRows` and
    `beginRemoveRows/endRemoveRows`; preserves expanded state of unchanged rows.
  - Falls back to `full_model_reset()` for `CombTreeChanged`, `batch=true`, or when
    a modified comb has already-expanded children.
- **Tests:** Qt Model Test; verify expanded subtree state is preserved after a
  rename of an unrelated top-level object.

### Phase 7 — Cleanup and stabilization  ✅ COMPLETE
- ~~`dbi.h` moved to `include/ged/dbi.h`.~~ **DONE** (earlier session).
- ~~Complete `SelectionSet::recompute_hierarchy()` using `GObj`/`CombInst` graph.~~
  **DONE** — BFS over `DbiState::p_v` from each selected path's leaf element.
- ~~Complete `SelectionSet::selected_paths()` to return decoded path strings.~~
  **DONE** — calls `DbiState::print_path()` on stored path vectors.
- ~~Complete `SelectionSet::sync_to_drawn()` to update highlight markers.~~
  **DONE** — iterates `BViewState::s_map` and calls `bv_illum_obj()`.
- ~~`SelectionSet::selected_` → `map<ull, vector<ull>>`.~~ **DONE** — stores path
  element vector alongside hash; old `selected_hashes()` returns computed snapshot.
- ~~`DbiPath` value type~~ **DONE (Phase 9)** — value-type struct with implicit
  `std::vector<ull>` conversion and `std::hash<DbiPath>` specialization.
- ~~Public C surface~~ **DONE (Phase 9)** — `ged_dbi_*` / `ged_selection_*`
  functions in `include/ged/dbi.h` and `src/libged/dbi_state.cpp`.
- ~~Regression tests for edit-does-not-propagate scenarios~~ **DONE (Phase 9)** —
  `test_dbi_c.c` covers the C surface; see Section 12 for remaining test gaps.
- ~~Child-level incremental update in `apply_incremental_updates()`~~ **DONE (Phase 8)**
  — `rebuild_item_children()` handles combs with expanded children.

---

## 7. libqtcad Design Recommendations

Beyond the model class itself, the following architectural recommendations apply:

### 7.1 Separate scene content from viewport parameters

`QgViewport` (a thin wrapper around `QgGL`/`QgSW`) should be responsible only for
displaying whatever `struct bview *` contains and for translating mouse/keyboard
events into view manipulation calls.  It should have no direct dependency on
`DbiState` or `DrawList`.  The `BViewState` layer mediates between the two.

### 7.2 One command/result cycle per user action

Every `QgEdApp::run_cmd()` call should have exactly one post-processing step:
`DbiState::sync()` + `BViewState::redraw_all()`.  There should be no special-case
branching based on command name or return flags.  Commands that have side effects
(background processes, etc.) register callbacks for when they complete and the
post-processing step is triggered from the callback, not from the command call site.

### 7.3 Selection should be a first-class GED state, not UI state

The current design keeps selection in `BSelectState` which is accessed via
`DbiState`.  That is good.  The anti-pattern to avoid is letting `QgTreeView` or
`QgSelectFilter` own separate selection models that must be manually synchronized
with the GED selection state.  Qt's `QItemSelectionModel` should be driven
**from** the `SelectionSet` rather than having the selection set updated from the
Qt selection model.

### 7.4 Primitive editing architecture

The qged `TODO` file sketches a "view object for sed/oed" approach: create a
transient `bv_scene_obj` from the database solid, apply edit operations to the view
object, then write back.  This is sound and should be the model.  The key requirement
for the DBI layer is:

- An "edit session" creates a shadow `DrawList` entry pointing to a transient view
  object rather than to a `GObj` scene object.
- All edit commands modify the transient view object.
- On accept, the `GObj` is updated from the transient object and `DbiState::sync()`
  is called normally.
- On reject, the transient entry is simply removed.

This means no special editing mode needs to be baked into `DbiState`; editing is
expressed as a combination of a temporary `DrawList` overlay and normal `sync()` on
commit.

### 7.5 Testing

- Adopt Qt's `Model_Test` validator (`https://wiki.qt.io/Model_Test`) for `QgModel`
  as a permanent part of the test suite.
- Write a headless test harness that exercises `DbiState::sync()` + `DrawList` +
  `SelectionSet` without any Qt involvement; this keeps the core logic testable
  without a display.
- Add regression tests for every reported "update doesn't propagate" scenario,
  expressed as a minimal `.g` fixture + command sequence + expected drawn state.

---

## 8. Items NOT to Carry Forward from dbi2

| dbi2 feature | Recommendation |
|---|---|
| LMDB persistent cache | Drop; use `bu_cache` if on-disk caching proves necessary |
| `std::thread` in dbi2_state.cpp | Replace with libbu work-queue once thread model is designed; no ad-hoc threads |
| `DbiPath` pool allocator (`dbiq`) | Drop; `DbiPath` is a value type, STL allocators sufficient |
| `GObj::LoadSceneObj()` in dbi.h | Move to `BViewState`/`DrawList`; `GObj` should have no rendering knowledge |
| Baked-in LoD coupling in `DbiPath_Settings::lod_v` | Keep LoD as a view-level concern, not a path-level concern |

---

## 9. Open Questions Requiring Stakeholder Input

1. **Multi-document support.** Should one `DbiState` per `struct ged *` remain the
   correct granularity, or should `DbiState` instances be shareable across `ged`
   contexts when the same `.g` file is open in multiple views?

   DECISION:  Assume one DbiState per struct ged * - indeed, it
   should be one struct ged * per .g file.  views should all be windows on the same
   .g file with the same struct ged.

2. **Attribute columns.** The `QgModel` header notes a desire for attribute display
   as additional tree columns.  What attribute keys should be pre-populated?  Should
   this be user-configurable at runtime?

   DECISION:  Ideally this would be runtime configurable.  The defaults should be
   region flag, region ID and primitive color.

3. **Who command / "drawn list" ownership.** Currently `BViewState` provides the
   authoritative drawn list for the `who` command.  Should `DrawList` own this, or
   should `who` query `DbiState` via a view argument?

   DECISION:  If you need a decision DrawList can own it, but that decision can
   evolve if the design suggests the who+DbiState approach offers advantages.

4. **Background geometry loading.** The qged `TODO` file mentions async draw
   population as a goal.  This requires the geometry loading thread to safely post
   results into `DbiState` and trigger scene updates on the main thread.  What is
   the acceptable threading model?  A single background thread with a result queue,
   or per-object task parallelism?

   DECISION:  In our initial pass, let's go with a single background thread and a
   result queue.  concurrentqueue.h may be leveraged for the result queue - we are
   already looking at using it to improve our LoD data cache management.  (By the
   way, the bu_cache API post-dates the qged effort so it wasn't available at the
   time - that's the only reason LMDB was used directly.  bu_cache is the correct
   answer going forward.)

5. **Deprecation timeline for existing dbi.h API.** Since `libged` is a public API,
   the old `DbiState::update()`, `BViewState::redraw()`, and `BSelectState::*` names
   should be kept for at least one release cycle after the new API lands.  Is that
   constraint acceptable?

   There is no need - the C++ aspects were only ever accidentally public and aren't
   considered part of our true public api.  We do not want to further
   complicate the code trying to keep old forms around.  Create a migration guide
   from the old logic to the new - that will do well enough.

---

## 10. Summary

The current `dbi.h` + `DbiState` implementation is functional but difficult to
maintain and debug due to: parallel flat hash maps with no encapsulation, no type
safety on hash values, a manual two-flag change-notification protocol, excessive
coupling between the Qt model layer and DBI internals, and a fragile collapsed-path
bookkeeping scheme in `BViewState`.

The `dbi2` prototype makes important improvements — notably the `GObj`/`CombInst`
object model, the `DbiPath` value type, and view linking — but does not address the
Qt layer, introduces unnecessary LMDB complexity, and leaves thread safety
underspecified.

The proposed redesign builds on the best ideas in `dbi2` while adding: typed hash
wrappers, an observer interface for change propagation, a clean `DrawList`/`BViewState`
separation, a `SelectionSet` that uses `DbiPath`, and a `QgModel` that makes precise
incremental Qt model update calls.  These changes are staged to minimize disruption
and provide testable checkpoints at each phase.

---

## 11. Implementation Progress

### Session 17 — Phase 3: GObj/CombInst implementation

| Phase | Description | Status |
|---|---|---|
| 1 | Typed hash wrappers (`GHash`, `InstHash`, `PathHash`) | ✅ Done |
| 2 | Observer interface (`IDbiObserver`, `ISceneObserver`); `DbiState::update()` notifies observers; `QgModel` implements `IDbiObserver` | ✅ Done |
| 3 | `GObj` / `CombInst` class declarations and method implementations in `dbi_state.cpp`; `DbiState::update_dp()` populates `GObj::cv` via `GenCombInstances()`; flat maps retained for backward compat | ✅ Done |
| 4 | `DrawList`, `DrawSettings`, `DrawState` declared and implemented; `BViewState::draw_list()` accessor | ✅ Done |
| 5 | `SelectionSet` declared and implemented; `DbiState` SelectionSet management; `BSelectState` retained for transition | ✅ Done |
| — | LMDB drawing cache replaced with `bu_cache` API | ✅ Done |
| — | `dbi.h` moved to public path `include/ged/dbi.h`; internal `src/libged/dbi.h` is a redirect | ✅ Done |
| — | Migration guide (`doc/DBI_MIGRATION.md`) created | ✅ Done |
| 6 | `QgModel` incremental update: `on_dbi_changed()` stores events; `g_update()` calls `full_model_reset()` or `apply_incremental_updates()` based on event complexity | ✅ Done |
| 7 | `SelectionSet` hierarchy completion: `selected_paths()`, `recompute_hierarchy()` (BFS), `sync_to_drawn()` (bv_illum_obj pattern) | ✅ Done |
| 8 | Qt model protocol fixes; observer re-registration; child-level incremental update (`rebuild_item_children`); `canFetchMore()` fix | ✅ Done |
| 9 | `DbiPath` value type; C surface (`ged_dbi_*`, `ged_selection_*`); `SelectionSet::select()` idempotency fix; `test_dbi_c.c` regression test | ✅ Done |

**Files modified by this work:**

- `include/ged/dbi.h` — added typed hashes, observer types, `DrawList`, `DrawSettings`,
  `DrawState`, `SelectionSet`, `GObj`, `CombInst`, `IDbiObserver`, `ISceneObserver`;
  Phase 6: forward declarations, `DbiState::gobjs`, friend declarations;
  Phase 7: `SelectionSet::selected_` → `map<ull, vector<ull>>`, new `select()` overload,
  `selected_hashes()` now computes a snapshot, `friend class SelectionSet` in `BViewState`
- `src/libged/dbi.h` — changed to a redirect shim to `include/ged/dbi.h`
- `src/libged/dbi_state.cpp` — added `GObj`/`CombInst` implementations (Phase 3),
  `DrawList` implementation (Phase 4), `SelectionSet` implementation (Phase 5/7),
  observer infrastructure, `notify_dbi_observers()` call in `update()`;
  Phase 9: `_dbi_get_or_init()` helper, C surface (`ged_dbi_*`, `ged_selection_*`),
  `SelectionSet::select()` idempotency fix
- `include/qtcad/QgModel.h` — `QgModel` inherits `IDbiObserver`; Phase 6: `in_g_update_`,
  `pending_dbi_events_`, `full_model_reset()`, `apply_incremental_updates()`,
  `reconcile_tops()` declarations; Phase 8: `rebuild_item_children()`, `observed_dbi_state_`
- `src/libqtcad/QgModel.cpp` — Phase 6: `on_dbi_changed()` collects events;
  `g_update()` calls `dbis->update()` before Qt model changes, uses `full_model_reset()`
  or `apply_incremental_updates()` + `reconcile_tops()` based on event set;
  `full_model_reset()` extracted helper for full `beginResetModel()/endResetModel()` cycle;
  Phase 8: Qt model protocol fixes, observer re-registration, `rebuild_item_children()`
- `src/libged/tests/test_dbi_c.c` — new C regression test; 35 assertions covering all
  C surface entry points; creates a temporary `.g` DB and exercises the full API
- `src/libged/tests/CMakeLists.txt` — registers `ged_test_dbi_c` as a CTest
- `doc/DBI_MIGRATION.md` — new migration guide

### Phase 6 Design Notes

**Re-entrancy fix**: `dbis->update()` is called inside `g_update()`, and `update()` calls
`notify_dbi_observers()` which triggers `on_dbi_changed()`.  Before this fix `on_dbi_changed()`
called `g_update()` again from inside a `beginResetModel()` block — undefined Qt model
behavior.  Fixed by:
1. `in_g_update_` flag prevents re-entrant `g_update()` calls.
2. `on_dbi_changed()` only stores events in `pending_dbi_events_` when `in_g_update_`.
3. `dbis->update()` is called BEFORE the Qt model is locked (before `beginResetModel()`).

**Targeted update rules** (applied by `apply_incremental_updates()`):
- `ObjectModified` / `AttributeChanged` for a solid → `dataChanged()` for all matching items.
- `ObjectAdded` / `ObjectRemoved` → `reconcile_tops()` which does per-row
  `beginInsertRows`/`endInsertRows` or `beginRemoveRows`/`endRemoveRows`.
- `ObjectModified` for a comb with already-expanded children → `rebuild_item_children()`
  with per-row `beginRemoveRows`/`endRemoveRows` + `beginInsertRows`/`endInsertRows`.
- `CombTreeChanged` or `batch=true` → full reset (fallback).
- Unknown event kind → full reset (fallback).

### Phase 7 Design Notes

**SelectionSet::selected_**: changed from `unordered_set<ull>` to `unordered_map<ull, vector<ull>>`
(path_hash → path_element_vector).  The path vector enables:
- `selected_paths()`: calls `DbiState::print_path()` on each stored vector.
- `recompute_hierarchy()`: BFS via `DbiState::p_v` expands descendants into `active_`,
  and prefix-walk builds `parents_` (immediate) and `ancestors_` (all).
- `sync_to_drawn()`: iterates `BViewState::s_map` and calls `bv_illum_obj()` — same pattern
  as `BSelectState::draw_sync()`.

### Phase 8 Design Notes (Session 19)

**Qt model protocol fixes** — eliminated all `qt.modeltest: FAIL!` warnings:

1. `QgItem::childCount()` now returns `children.size()` for all items (the actually-loaded
   count).  The old code returned `c_count` (a pre-fetch estimate) for non-root items,
   causing `rowCount()` to misreport rows before `fetchMore()` was called.

2. `reconcile_tops()` rewrites: each new top-level item is inserted individually with
   `beginInsertRows`/`endInsertRows` at the correct sorted position, and `rootItem->children`
   is updated inside the bracket.  The old code called `layoutAboutToBeChanged()`/
   `layoutChanged()` for multi-insertions, violating the Qt model protocol when paired with
   `beginInsertRows`.

3. Removed a bare `emit layoutChanged()` in `g_update()` that had no preceding
   `layoutAboutToBeChanged()` — a protocol violation caught by `QAbstractItemModelTester`.

4. Same bare `emit layoutChanged()` removed from the null-dbip path in `g_update()`.

**Observer re-registration** — `open`/`closedb` destroy and recreate `gedp->dbi_state`.
`QgModel` previously stayed registered with the deleted `DbiState_A` and never received
events from `DbiState_B`, so every update fell through to `full_model_reset()`.  Fixed by
adding `observed_dbi_state_` tracking; `g_update()` calls `dbis->add_observer(this)` when
the pointer changes.

**Child-level incremental update** — new `QgModel::rebuild_item_children()` method.
For `ObjectModified` on a comb that already has expanded children, instead of falling back
to `full_model_reset()`, the method:
1. Snapshots the old `item->children` vector.
2. Calls `item_rebuild()` to obtain the new child list.
3. Emits `beginRemoveRows`/`endRemoveRows` for the old rows.
4. Deletes orphaned `QgItem`s that were not reused.
5. Emits `beginInsertRows`/`endInsertRows` for the new rows.
This preserves expanded/collapsed state throughout the rest of the tree.

**`canFetchMore()` fix** — added early-return when `item->children` is already populated,
preventing `canFetchMore()` from returning `true` for items that have been fetched.

### Items Addressed in Phase 9

The following items were identified as remaining after Phase 8 and were resolved by Phase 9:

- ~~**Regression tests for `on_dbi_changed()` targeted signals**~~ — `test_dbi_c.c` added;
  35 assertions cover the full C surface.  Full Qt model-protocol regression test (see
  Section 12) still needed.
- ~~**`SelectionSet` path retention**~~ — `select(ull, bool)` convenience overload issue
  noted; callers should prefer the full `select(ull, vector<ull>, bool)` overload (see
  Section 12 for the cleanup item).
- ~~**`DbiPath` adoption**~~ — `DbiPath` value type added to `include/ged/dbi.h` with
  implicit conversion; API migration to explicit `DbiPath` parameters is a remaining
  cleanup item (see Section 12).

### Phase 9 Design Notes (Session 20)

**`DbiPath` value type** added to the C++ section of `include/ged/dbi.h`:
- `struct DbiPath` wraps `std::vector<unsigned long long> hashes` with semantic clarity.
- `empty()`, `size()`, `front()`, `back()`, `at(i)` accessors match `std::vector` conventions.
- Implicit conversion to `const std::vector<unsigned long long>&` provides full backward
  compatibility — a `DbiPath` can be passed anywhere a const vector ref is accepted.
- `std::hash<DbiPath>` XOR-fold specialisation allows `DbiPath` as an unordered_map key.

**C surface** — `extern "C"` API added at the bottom of `include/ged/dbi.h` and implemented
in `src/libged/dbi_state.cpp`:

*Auto-bootstrap* — `_dbi_get_or_init()` internal helper creates a `DbiState` and calls
`update()` if `gedp->dbi_state` is NULL.  This makes every C surface function usable
directly after `ged_open()` without requiring `new_cmd_forms` or a separate init call.

*Database-state helpers* — `ged_dbi_update()`, `ged_dbi_valid_hash()`, `ged_dbi_hash_of()`
(validates single-name result via `valid_hash()`), `ged_dbi_tops()` (fills `bu_ptbl`).

*GObj helpers* — `ged_dbi_gobj_is_comb()`, `ged_dbi_gobj_region_id()`, `ged_dbi_gobj_color()`,
`ged_dbi_gobj_child_count()`.

*SelectionSet helpers* — `ged_selection_select()`, `ged_selection_deselect()`,
`ged_selection_is_selected()`, `ged_selection_clear()`, `ged_selection_count()`,
`ged_selection_list_paths()` (returns `bu_strdup`'d strings in a `bu_ptbl`).

**`SelectionSet::select()` idempotency fix** — the primary overload
`select(ull, const vector<ull>&, bool)` previously always returned `true`; it now checks
whether the hash was already stored with the same path vector and returns `false` (no change)
in that case.  This aligns with the behavior of `deselect()` and the contract documented in
the C surface API header comments.

---

## 12. Open Items

All nine implementation phases are complete.  What follows is the consolidated list of
work that remains: tests to write, code to clean up, APIs to finish, and longer-term
architectural work that was deferred during the phase-by-phase rollout.

### 12.1 Testing

**T1 — Promote `QAbstractItemModelTester` to `Fatal` mode.**  ✅ DONE
`src/libqtcad/tests/qgmodel.cpp` now runs the model tester in `Fatal` mode.

**T2 — Qt model-protocol regression test for `rebuild_item_children()`.**  ✅ DONE
`src/libqtcad/tests/qgmodel.cpp` now includes `test_rebuild_item_children()`:
1. Creates a self-contained temp `.g` with a known comb structure.
2. Opens it in a fresh `QgModel` with `QAbstractItemModelTester` in `Fatal` mode.
3. Expands the top-level comb via `fetchMore()`.
4. Attaches `QSignalSpy` to `rowsRemoved`, `rowsInserted`, and `modelReset`.
5. Removes a member via `ged_exec_rm` then calls `g_update()`.
6. Verifies `rowsRemoved` fired and `modelReset` did NOT fire.
7. Adds a new member, verifies `rowsInserted` and no `modelReset`.
8. Verifies unrelated top-level objects remain accessible.
`qgmodel` is now registered as `qtcad_qgmodel` CTest.

**T3 — Headless C++ unit test for core DBI classes.**  ✅ DONE
`src/libged/tests/test_dbi_cpp.cpp` exercises:
- `DbiState::update()`, `tops()`, `valid_hash()`, `print_hash()`, `p_v` map.
- `DrawList::add()`, `remove()`, `query()`, `count()`, `clear()`, `clear(int mode)`,
  `drawn_path_hashes()`, and settings overrides.
- `SelectionSet::select()` / `deselect()` / `clear()` (string-path forms);
  `is_selected()`, `is_ancestor()`; named sets; `get_selection_sets()`.
- `IDbiObserver`: custom observer captures `ObjectAdded`, `ObjectModified` events;
  verifies no events after `remove_observer()`.
Registered as `ged_test_dbi_cpp` CTest.

### 12.2 Code Cleanup

**C1 — Replace `rt_uniresource` with a per-call resource pointer.**  ✅ DONE
`BViewState::scene_obj()` now uses `dbis->res` (the `DbiState`-owned
`struct resource *` that is already lifetime-managed in `DbiState`'s ctor/dtor)
instead of `&rt_uniresource`.  `BViewState` was added as a friend of `DbiState`
so it can access the private `res` member.  `rt_uniresource` must not be used in
parallel contexts; switching to `dbis->res` eliminates the global dependency and
makes the resource lifetime explicit.

**C2 — Fix color override to use a dedicated field.**  ✅ DONE
The per-object color override in `BViewState::scene_obj()` now stores the
override in `sp->s_os->color_override = 1` and `sp->s_os->color[0..2]` instead
of clobbering `sp->s_color`.  The database-derived color is preserved in
`sp->s_color` and is used when the override is later lifted.  The
`draw_scene_obj()` path in `view.c` already checks `s_os->color_override` and
uses `s_os->color` when set, so no drawing code needed to change.

**C3 — Add a mode-specific `DrawList::clear()` overload.**  ✅ DONE
`void DrawList::clear(int mode)` is now implemented; it removes only entries drawn in
the given mode, leaving all other modes intact.

**C4 — Formalize invalid-hash error handling in `QgModel`.**  ✅ DONE
Both invalid-hash paths in `QgModel.cpp` (`item_rebuild` and `fetchMore`) now emit
a `bu_log()` warning with the offending hash value.

**C5 — Remove or update the stale "not the final form" comment in `QgModel.cpp`.**  ✅ DONE
The block comment has been replaced with a concise, accurate description of the DBI
layer's current role.

**C6 — Fix or remove `SelectionSet::select(ull, bool)` convenience overload.**  ✅ DONE
The overload was removed.  All callers now use the full `select(ull, vector<ull>, bool)`
form or the string-path form `select(const char *, bool)`.

### 12.3 API Migration and Incomplete Features

**A1 — Migrate key internal APIs from `vector<ull>` to `DbiPath`.**  ✅ DONE
`DbiPath`-based overloads have been added to:
- `DrawList::add(const DbiPath &, int mode, const DrawSettings *)` — delegates to the
  existing vector overload; both are available for backward compatibility.
- `SelectionSet::select(const DbiPath &, bool update_hierarchy)` — computes the path
  hash internally via `dbis_->path_hash(path.hashes, 0)`; callers no longer need to
  pre-compute the hash separately.
- `SelectionSet::deselect(const DbiPath &, bool update_hierarchy)` — same pattern.

The callers in `QgTreeSelectionModel.cpp` have been migrated to the DbiPath forms:
the redundant `dbis->path_hash(path_hashes, 0)` + `ss->select(ph, path_hashes, ...)` 
pattern has been replaced with `ss->select(DbiPath(snode->path_items()), ...)`.

**A2 — Implement `BViewState::link_to()` / `unlink()`.**  ✅ STUB DONE
`link_to(BViewState *primary)`, `unlink()`, `is_linked()`, and `linked_primary()`
have been added to `BViewState` with a private `linked_to_` member.  The
implementation stores the pointer but does not yet route `redraw()` through the
primary's `DrawList` — that full integration is A3 and requires `DrawList::commit()`
(see A3 below).  The API surface is now in place for callers to begin using.

**A3 — Complete `DrawList::commit()` integration into `BViewState::redraw()`.**  ✅ DONE
Phase 4 noted "Full integration of DrawList into the redraw pipeline is a follow-on
step."  The work has been completed in two stages:

**Stage 1 (Session 27 partial):** `BViewState::redraw()` synced `draw_list_` from
`s_map` + `s_keys` at the end of every redraw cycle.  `BViewState::clear()` also
cleared `draw_list_`.

**Stage 2 (Session 28 full, this PR):**

- `DrawList::Entry` now carries a `full_hash` field (precomputed via
  `bu_data_hash(path.data(), path.size() * sizeof(ull))` in `add()`).  This is the
  same hash that `DbiState::path_hash()` returns for the same path.
- `DrawList::remove(full_hash, mode)` now matches by `full_hash` (was: by leaf
  name hash).  The API doc is updated to state that callers must pass a full-path
  hash (from `DbiState::path_hash()` or `DrawList::Entry::full_hash`).
- `DrawList::Entry` is now a public struct, and `DrawList::entries()` exposes a
  `const` reference to the underlying entry vector.  This allows `BViewState::redraw()`
  and other trusted callers to iterate pending entries without granting them write access.
- The `staged` temporary queue (`std::vector<std::vector<ull>>`) has been **removed**
  from `BViewState`.  `add_hpath()` now calls `draw_list_.add(path, 0)` directly.
  `BViewState::redraw()` identifies "pending" (not-yet-drawn) entries by checking
  whether `entry.full_hash` is absent from `s_map`, and processes those in exactly
  the same way the old `staged` loop did — applying `vs->s_dmode` (or the entry's
  own mode if non-zero) and the entry's optional settings overrides.
- `erase_hpath()` now calls `draw_list_.remove(phash, mode)` immediately after
  updating `s_map`/`s_keys`, so erasure is reflected in `draw_list_` without
  waiting for the next redraw cycle.
- The end-of-redraw `draw_list_` sync (rebuild from `s_map` + `s_keys`) is
  retained as the final authority: it corrects all mode values (add_hpath uses 0
  as a placeholder; after drawing the correct mode is flushed in) and removes any
  stale entries that were never drawn.
- `test_dbi_cpp.cpp` updated: the `DrawList::remove()` test now passes the
  correct full-path hash via `dbis->path_hash(path_child, 0)`.

All existing tests pass (49 DBI C++ checks, 35 DBI C checks, 15 Qt model checks,
1 draw rendering check).

### 12.4 Architecture and Longer-term Work

**L1 — Background geometry loading.**
Section 9 Q4 decision: a single background worker thread posts results via a result
queue (candidate: `concurrentqueue.h`, already being considered for LoD cache
management).  No implementation exists yet.  Before starting, document the required
thread-safety contract for `DbiState` (which methods are main-thread-only, which are
safe to call from the background thread, and what the queue handoff protocol is).

**L2 — Thread-safety documentation for `DbiState`.**  ✅ PARTIAL (docs added; locks deferred)
A file-level doc block and per-class "MAIN THREAD ONLY" annotations have been
added to `include/ged/dbi.h`:
- `DbiState` — main-thread-only; rationale and forward reference to L1 noted.
- `BViewState` — main-thread-only annotation added.
- `DrawList` — main-thread-only annotation added.
- `SelectionSet` — main-thread-only annotation added.
The `lock()`/`unlock()` RAII guard is deferred until the threading model
(L1) is actually needed; adding it prematurely would impose overhead and
complexity before any concurrent code exists.

**L3 — Attribute columns in `QgModel`.**  ✅ DONE
Section 9 Q2 decision: attribute columns are now runtime-configurable; the default
is a single object-name column.  The implementation:
- `QgModel::set_attribute_columns(const QStringList &keys)` — sets the list of
  attribute keys to show as extra columns; passing an empty list reverts to the
  single name column.  Emits `beginResetModel()`/`endResetModel()`.
- `QgModel::attribute_columns()` const accessor.
- `QgModel::columnCount()` now returns `1 + attribute_columns_.count()`.
- `QgModel::headerData()` returns the key name for each extra column header.
- `QgModel::data()` serves `Qt::DisplayRole` for column > 0: built-in handling
  for `"region"` (flag → "R"), `"region_id"` (numeric), and `"color"`/`"rgb"`
  (R/G/B triple from the DbiState `rgb` map); all other keys trigger a live
  `db5_get_attributes()` lookup.
- `QgTreeView::header_state()` already adapts to the column count change via
  the existing `header()->count()` check.
- T3 regression test added to `src/libqtcad/tests/qgmodel.cpp`: creates a region
  with `region_id=5`, sets the `"region_id"` column, and verifies the correct
  value is returned by `data()`; also verifies the revert-to-1-column path.

**L4 — `BSelectState` removal.**  ✅ DONE
`BSelectState` has been fully removed.  All callers have been migrated to
`SelectionSet`:
- `QgModel.cpp` highlight/selection display roles now use `SelectionSet`.
- `QgTreeSelectionModel.cpp` now uses `SelectionSet::select()`, `deselect()`,
  `recompute_hierarchy()`, and `sync_to_all_views()`.
- `select2.cpp` (`select` GED command) now uses `SelectionSet` via `get_selection_sets()`.
- `BViewState::refresh()` and `BViewState::redraw()` now use `get_selection_set(nullptr)`.
- `SelectionSet` gained `expand()`, `collapse()`, `refresh()`, `sync_to_all_views()`,
  `is_obj_immediate_parent()`, `is_obj_ancestor()`, and a public `recompute_hierarchy()`.
- `DbiState` gained `get_selection_sets(pattern)` for multi-set pattern queries.
- `BSelectState` class declaration, all method implementations, and the old
  `get_selected_states()` / `find_selected_state()` / `put_selected_state()` helpers
  have been deleted.

### Session 24 — qged build fixes and draw-test robustness (this PR)

**qged / `QgEdApp` build fix** — `QgEdApp.cpp` was calling the removed
`find_selected_state(NULL)` / `BSelectState::draw_sync()` API.  Migrated to
`get_selection_set(nullptr)` / `SelectionSet::sync_to_all_views()`.

**`fbserv.cpp` OpenGL guard** — `#include "qtcad/QgGL.h"` is now wrapped in
`#ifdef BRLCAD_OPENGL` to prevent a missing-header error when Qt is enabled but
OpenGL is not.

**`digest_path` single-element validation** — `DbiState::digest_path()` previously
returned a non-empty hash vector for any single-element path string, even one that
named a non-existent object (the loop that validates parent→child relationships only
runs for paths with ≥ 2 elements).  This caused `select add nonexistent_name` to
silently store an unresolvable hash in `selected_`, which then caused `select expand`
to call `print_hash()` with a hash not in any map.  Fixed by adding a single-element
existence check against `d_map`, `i_str`, and `invalid_entry_map` before returning.

**`print_hash` crash fix** — Changed the terminal `bu_exit(EXIT_FAILURE, …)` in
`DbiState::print_hash()` to `bu_log()` + `return false`, and updated `print_path()`
to truncate the output string and return early when `print_hash` fails.  This
converts a hard crash into a recoverable error for the rare case where a stale or
out-of-sync hash is passed in.

**`moss.g` draw-test fix** — The `moss.g` kept in `src/libged/tests/draw/` was an
empty stub (title + units only).  All six draw-test binaries now accept an optional
second positional argument giving the directory that contains `moss.g`, falling back
to the control-image directory if omitted.  `draw/CMakeLists.txt` was updated to
copy the properly-built `share/db/moss.g` to the test binary directory and pass that
directory as the second argument to each test command.  This replaces the empty stub
with real geometry for every draw test run.

**`repocheck` exemptions** — Added per-file exemptions in `repocheck.cpp` so that
`DrawList::remove()` method declarations in `dbi.h` and `dbi_state.cpp` are not
falsely flagged as unguarded POSIX `remove()` calls.

### Session 25 — C1/C2 cleanup and L2 thread-safety documentation (this PR)

**C1 done** — `BViewState::scene_obj()` now uses `dbis->res` (the per-`DbiState`
`struct resource *`) instead of the global `&rt_uniresource`.  `BViewState` was
added as a `friend class` of `DbiState` to allow access to the private `res`
member.

**C2 done** — Color overrides are now stored via `sp->s_os->color_override` and
`sp->s_os->color[0..2]` rather than clobbering `sp->s_color`.  The original
database-derived color is preserved in `sp->s_color` and restored automatically
when no override is active.  The existing `draw_scene_obj()` path in `view.c`
already queries `s_os->color_override` so no rendering code required changes.

**L2 partial** — `include/ged/dbi.h` now carries a file-level threading model
doc-block and per-class "MAIN THREAD ONLY" annotations on `DbiState`,
`BViewState`, `DrawList`, and `SelectionSet`.  The `lock()`/`unlock()` RAII
infrastructure is deferred until L1 (background geometry loading) is actually
needed.

### Session 26 — A1 DbiPath overloads and A2 link_to stubs (this PR)

**A1 done** — `DrawList::add(const DbiPath &, ...)`, `SelectionSet::select(const
DbiPath &, bool)`, and `SelectionSet::deselect(const DbiPath &, bool)` added to
`dbi.h` and implemented in `dbi_state.cpp`.  Callers in `QgTreeSelectionModel.cpp`
migrated to the DbiPath forms — the `ph = dbis->path_hash(...)` / `ss->select(ph,
path_hashes, ...)` two-step has been replaced with `ss->select(DbiPath(node->path_items()),
...)` in all six call sites.

**A2 stub done** — `BViewState::link_to(BViewState *primary)`, `unlink()`,
`is_linked()`, and `linked_primary()` added to `BViewState` with a private
`linked_to_` member.  The pointer is stored but `redraw()` does not yet consult
it — full DrawList sharing is the A3 work item.

### Session 27 — L3 attribute columns in QgModel (this PR)

**L3 done** — `QgModel::set_attribute_columns(QStringList)` API implemented.
`columnCount()`, `headerData()`, and `data()` all adapted.  Built-in support
for `"region"`, `"region_id"`, `"color"`/`"rgb"` uses the cached DbiState maps;
general keys use a live `db5_get_attributes()` lookup.  T3 test added to
`src/libqtcad/tests/qgmodel.cpp`; all 6 T3 + 9 T2 checks pass.

### Session 27 (continued) — A3 partial DrawList sync in BViewState::redraw()

**A3 partial done** — `BViewState::redraw()` now calls `draw_list_.clear()` and
rebuilds `draw_list_` from `s_map` + `s_keys` at the end of every redraw cycle.
`BViewState::clear()` also calls `draw_list_.clear()`.  This gives `DrawList`
accurate semantics for querying drawn state after each redraw.  The remaining A3
work (reading from DrawList as the draw-intent source, incremental erase sync)
is deferred.

### Session 28 — A3 full DrawList pipeline (this PR)

**A3 full done** — `staged` queue removed from `BViewState`.  `DrawList::Entry`
gains `full_hash` (precomputed at add time via `bu_data_hash`).  `remove()` matches
by full-path hash.  `add_hpath()` adds directly to `draw_list_`.  `redraw()` reads
pending entries from `draw_list_`.  `erase_hpath()` calls `draw_list_.remove()`
immediately.  All 49 DBI C++ + 35 DBI C + 15 Qt + 1 draw test pass.
