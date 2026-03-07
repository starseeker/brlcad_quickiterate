# qged Technology Stack — Current-State Code and Design Review

*Date: 2026-03 — conducted after completion of the nine-phase DBI redesign*

---

## 1. Purpose and Scope

This document is a clean-room code and design review of the current state of the
`qged` application and its supporting `libqtcad` library.  The focus is on:

- Architectural soundness and separation of concerns
- Compliance with C++17 best practices
- Compliance with Qt best practices
- Remaining technical debt, open TODOs, and missing features
- A prioritised list of next steps

The review covers:

| Directory | Files | Approx. LOC |
|---|---|---|
| `src/qged/` | 34 (app + plugins) | ~6,900 |
| `src/libqtcad/` | 27 | ~9,500 |
| `include/qtcad/` | 18 public headers | ~2,400 |
| **Total** | **79** | **~18,800** |

---

## 2. Architecture Overview (Current State)

### 2.1 Layer diagram

```
┌──────────────────────────────────────────────────────────┐
│  qged Application                                        │
│  QgEdApp (QApplication subclass)                         │
│  QgEdMainWindow, QgEdFilter, fbserv                      │
│  plugins/ (polygon, edit/ell, view/{info,select,         │
│            settings,measure})                            │
├──────────────────────────────────────────────────────────┤
│  libqtcad — Qt Widgets / Model Library                   │
│  QgModel  : QAbstractItemModel, IDbiObserver             │
│  QgTreeView / QgTreeSelectionModel                       │
│  QgGL (OpenGL), QgSW (software), QgView, QgQuadView      │
│  QgConsole / QgConsoleListener (I/O thread)              │
│  QgToolPalette / QgAccordion / QgDockWidget              │
│  QgPolyFilter / QgSelectFilter / QgMeasureFilter         │
│  QgAttributesModel / QgKeyVal                            │
│  bindings (key/mouse binding table)                      │
├──────────────────────────────────────────────────────────┤
│  libged — DBI Layer                                      │
│  DbiState   (database mirror, owns GObj/CombInst)        │
│  DrawList   (per-view draw intent)                       │
│  BViewState (scene object management, camera)            │
│  SelectionSet (path selection + hierarchy metadata)      │
│  IDbiObserver / ISceneObserver (change notification)     │
├──────────────────────────────────────────────────────────┤
│  librt / libdb / libbsg                                  │
│  struct ged, struct db_i, struct directory, bsg_view *   │
└──────────────────────────────────────────────────────────┘
```

### 2.2 What the DBI redesign accomplished

The nine-phase DBI redesign (documented in `DBI_OLD_ANALYSIS.md`) addressed the
worst structural problems in the *libged* layer:

| Phase | Change | Status |
|---|---|---|
| 1 | Typed hash wrappers (`GHash`, `InstHash`, `PathHash`) | ✅ Done |
| 2 | Observer interface (`IDbiObserver`); `notify_dbi_observers()` in `update()` | ✅ Done |
| 3 | `GObj` / `CombInst` object model replaces flat parallel maps | ✅ Done |
| 4 | `DrawList` / `DrawSettings` extracted from `BViewState` | ✅ Done |
| 5 | `SelectionSet` replaces `BSelectState`; `DbiPath` overloads | ✅ Done |
| 6 | `QgModel` incremental update via `on_dbi_changed()` events | ✅ Done |
| 7 | `SelectionSet` hierarchy (BFS), `selected_paths()`, `sync_to_drawn()` | ✅ Done |
| 8 | Qt model protocol fixes; `rebuild_item_children()`; observer re-registration | ✅ Done |
| 9 | `DbiPath` value type; C surface (`ged_dbi_*`, `ged_selection_*`); regression tests | ✅ Done |

Cleanup items C1-C6, A1-A3, L2-L4 are also complete.  The only remaining DBI
open item is **L1 (background geometry loading)**, which has a decision but no
implementation.

The DBI layer is now architecturally sound for its current feature set.

---

## 3. C++17 Compliance Assessment

The qged/libqtcad application layer has *not* been modernised in lockstep with
the DBI layer.  It was written mostly in C++11/C++14 style (or earlier) and has
not been systematically updated.

### 3.1 `nullptr` vs `NULL`

**Finding:** 45 occurrences of `NULL` in `src/qged/`, 94 in `src/libqtcad/` and
`include/qtcad/`.  Nearly all pointer initializations and comparisons use `NULL`
rather than `nullptr`.

Representative examples:
```cpp
// include/qtcad/QgModel.h:129, 131, 167, 216, 221
QgModel *mdl = NULL;
QgItem *parentItem = NULL;
struct directory *dp = NULL;
explicit QgModel(QObject *p = NULL, const char *npath = NULL);
struct ged *gedp = NULL;

// src/qged/QgEdApp.h:65, 100
QgModel *mdl = NULL;
QgEdMainWindow *w = NULL;
```

`NULL` is a C macro that expands to `0` or `(void*)0` depending on context.
Using it in C++ is technically correct but masks type errors that `nullptr` would
catch at compile time.  All new code in the codebase should use `nullptr`.

**Severity:** Low (style), but systematic.

### 3.2 `override` keyword

**Finding:** The `override` keyword appears on `QgModel`'s `on_dbi_changed()` and
the `QAbstractItemModel` virtual methods (e.g., `data()`, `rowCount()`), but it
is absent from virtually every other virtual override in the codebase.

Missing `override` in headers:
- `include/qtcad/QgMeasureFilter.h:75` — `virtual bool get_point()`
- `include/qtcad/QgPolyFilter.h:55` — `virtual bool eventFilter()`
- `include/qtcad/QgSelectFilter.h:54` — `virtual bool eventFilter()`
- All `paintEvent`, `resizeEvent`, `mousePressEvent` etc. overrides in
  `QgGL.cpp`, `QgSW.cpp`, `QgView.cpp`, `QgTreeView.cpp`

The C++11 `override` specifier causes a compile error if the function does not
actually override a base-class virtual, catching signature mismatches that would
otherwise silently create a new virtual instead of overriding the intended one.

**Severity:** Medium — safety risk, easy to fix mechanically.

### 3.3 `auto` and type deduction

**Finding:** `auto` is used in ~7 places in `QgModel.cpp` (range-based for
loops and event iteration).  Elsewhere it is essentially absent.  The codebase
makes heavy use of explicit iterator types written out by hand:

```cpp
// src/libqtcad/QgModel.cpp:441, 488, 504 — verbose iterator loops
for (nh_it = nh.rbegin(); nh_it != nh.rend(); nh_it++) { ... }
for (s_it = items->begin(); s_it != items->end(); s_it++) { ... }
```

These would be clearer as range-based `for` loops with `auto`.  While not
incorrect, the inconsistency makes the code harder to read and maintain.

**Severity:** Low (style/readability).

### 3.4 Absent C++17 language features

None of the following C++17 features appear anywhere in `src/qged/` or
`src/libqtcad/`:

| Feature | Use case |
|---|---|
| `constexpr` | Compile-time constants (e.g., draw mode values, color literals) |
| `[[nodiscard]]` | Functions whose return values must not be ignored |
| `[[maybe_unused]]` | Parameters that may be unused (eliminate `UNUSED()` macros) |
| `std::optional<T>` | Functions that may or may not return a value (e.g., `get_hdp()`) |
| `std::string_view` | Non-owning string references replacing `const char *` |
| Structured bindings | `for (auto &[k, v] : map)` instead of `.first`/`.second` |
| `if constexpr` | Compile-time branching in templates |
| Class template argument deduction (CTAD) | Simplify container initialization |

While not every feature is applicable everywhere, their complete absence
indicates that the application layer code has not been updated for C++17.

**Severity:** Medium — functional impact is low but indicates technical debt.

### 3.5 Smart pointers

**Finding:** The entire `src/qged/` and `src/libqtcad/` codebase uses exactly
one smart pointer: a `QPointer<QgConsoleWidgetCompleter>` in `QgConsole.cpp`.
Every other dynamically allocated object (Qt parent-owned widgets aside) is
managed via raw pointers with manual `delete`.

Examples of raw-pointer manual management:
```cpp
// QgEdApp.cpp
mdl = new QgModel();           // line 167
// ... many lines later ...
delete mdl;                    // line 311

// QgModel.cpp
delete items;                  // line 367
delete rootItem;               // line 372
```

For Qt-parented widgets this is acceptable (Qt's parent-child ownership
cleans them up), but for non-widget objects like `QgModel`, raw-pointer
ownership is fragile and exception-unsafe.

Non-Qt objects that should use `std::unique_ptr` or similar:
- `QgEdApp::mdl` (`QgModel *`) — `QgEdApp` is the sole owner
- `QgModel::rootItem` (`QgItem *`) — model owns the tree
- `QgModel::items` (`std::unordered_set<QgItem *> *`) — raw pointer to heap set

**Severity:** Medium — not currently causing bugs but creates exception-safety
and ownership-clarity problems.

### 3.6 Lambdas

**Finding:** Only one lambda exists in the entire `src/qged/` and `src/libqtcad/`
codebase (in `QgConsoleListener.cpp:63`).  All other callback-style patterns use
function pointers, `std::bind`-free explicit member function pointers, or separate
named slot functions.

Lambdas would be appropriate for:
- Short Qt signal connections that don't warrant a named slot (e.g., menu action
  connections in `QgEdMainWindow.cpp`)
- Notification callbacks registered with BRL-CAD C APIs
- Predicate arguments to `std::find_if`, `std::sort`, etc.

**Severity:** Low (style), not causing bugs.

---

## 4. Qt Best Practices Assessment

### 4.1 Global `qApp` casts

**Finding:** 44 occurrences of `((QgEdApp *)qApp)` or similar casts in
`src/qged/`.  This pattern appears in `QgEdMainWindow.cpp`, `QgEdApp.cpp`,
`QgEdFilter.cpp`, `QgEdPalette.cpp`, and `fbserv.cpp`.

```cpp
// QgEdMainWindow.cpp
QgEdApp *ap = (QgEdApp *)qApp;

// fbserv.cpp
QgEdApp *c = (QgEdApp *)qApp;
```

This is the Qt-approved way to access a custom `QApplication` subclass from
anywhere in the program, but it creates tight coupling that:
- Makes the widgets and filters non-reusable outside `qged`
- Makes unit testing harder (the app context must be present)
- Breaks the layering (`libqtcad` should have no dependency on `QgEdApp`)

Within `src/qged/` the pattern is acceptable; it is a problem where it appears
in `libqtcad` code (it does not currently, but care must be taken as the code
evolves).

**Severity:** Low within qged, would be Medium if it spread to libqtcad.

### 4.2 `bv.h` includes — should be `bsg.h`

**Finding:** The BSG modernization work (sessions 19–27) migrated all consumers
of `bv.h` to `bsg.h`, but `src/qged/` and `src/libqtcad/` were not updated:

```
src/qged/QgEdApp.h                          #include "bv.h"
src/qged/plugins/polygon/QPolySettings.h   #include "bv.h"
src/qged/plugins/view/measure/CADViewMeasure.h  #include "bv.h"
src/qged/plugins/view/info/CADViewModel.h  #include "bv.h"
src/libqtcad/QgSelectFilter.cpp            #include "bv.h"
src/libqtcad/QgView.cpp                    #include "bv.h"
src/libqtcad/QgPolyFilter.cpp              #include "bv.h"
src/libqtcad/QgMeasureFilter.cpp           #include "bv.h"
src/libqtcad/QgQuadView.cpp                #include "bv.h"
```

`bv.h` is now a compatibility shim that redirects to `bsg.h`; the code will
continue to build.  However, since the intent is to eventually remove `bv.h`,
these 9 files should be updated to include `bsg.h` directly.

**Severity:** Low (build still works), but creates a maintenance obligation.

### 4.3 `raytrace.h` pulled into `libqtcad`

**Finding:** Five files in `src/libqtcad/` include `raytrace.h`:

```
src/libqtcad/QgModel.cpp
src/libqtcad/QgView.cpp           // comment says "TODO - need to move..."
src/libqtcad/QgPolyFilter.cpp     // comment says "TODO - need to move..."
src/libqtcad/QgSelectFilter.cpp
src/libqtcad/QgMeasureFilter.cpp
```

`raytrace.h` is a heavyweight header that pulls in large portions of librt.
`libqtcad` is a *display* library; it should not need to know about ray tracing
internals.  The polygon sketch export functionality that requires `raytrace.h`
in `QgView.cpp` and `QgPolyFilter.cpp` should be moved to `qged` or to a thin
adapter layer.

**Severity:** Medium — inflated compile times and incorrect dependency direction.

### 4.4 Qt6 compatibility gaps

Two confirmed `Qt6`-compatibility TODOs remain:

| File | Line | Description |
|---|---|---|
| `src/libqtcad/QgConsole.cpp` | 543 | `// TODO - figure out what to do for Qt6 here...` — affects mid-string completion insertion |
| `src/libqtcad/QgFlowLayout.cpp` | 154 | `// TODO - figure out the right Qt6 logic here...` — affects flow layout geometry calculation |

`QgFlowLayout.cpp` already has `#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)`
guards, but the Qt6 code path is not yet implemented.

**Severity:** Medium for Qt6 targets.

### 4.5 fbserv.cpp: TCP socket instead of QLocalSocket

**Finding:** `fbserv.cpp` uses `QTcpServer` / `QTcpSocket` for the embedded
framebuffer server.  The file header comment says:

```cpp
// TODO - Look into QLocalSocket, and whether we might be able to
// use it here...
```

For intra-process or same-host communication between `qged` and the rt
subprocess, `QLocalSocket` / `QLocalServer` is the correct abstraction: it is
faster, does not require a bound TCP port (which may conflict on busy machines),
and does not traverse the network stack.

**Severity:** Medium — correctness (port conflicts) and security (TCP socket
is accessible from any process on the host).

### 4.6 Signal/slot syntax consistency

**Finding:** The vast majority of `connect()` calls in `QgEdMainWindow.cpp` and
`QgEdApp.cpp` use the modern type-safe pointer-to-member syntax:
```cpp
QObject::connect(m, &QgModel::view_change, ap, &QgEdApp::do_view_changed);
```

However, five bare `connect()` calls (without the `QObject::` prefix) appear in
`QgEdMainWindow.cpp` (lines 241-244, 266).  These are still the modern syntax
form (not SIGNAL/SLOT macros), so there is no type-safety concern, but the
inconsistency is a minor readability issue.

`QTCAD_SLOT` is a BRL-CAD-specific macro (defined in `include/qtcad/defines.h`)
that expands to debug tracing code in debug builds and is empty in release builds.
It is used correctly throughout and is not a Qt4-style SIGNAL/SLOT macro.

**Severity:** Very low.

---

## 5. Threading

### 5.1 QgConsoleListener — unsynchronised access to `ged_result_str`

`QgConsoleListener::QConsoleListener()` moves a `QSocketNotifier` to a
background thread.  The activated lambda (line 63) reads
`process->gedp->ged_result_str` from the background thread:

```cpp
[this]() {
    if (callback) {
        size_t s1 = bu_vls_strlen(process->gedp->ged_result_str);
        (*callback)(data, (int)type);
        size_t s2 = bu_vls_strlen(process->gedp->ged_result_str);
        ...
    }
}
```

`ged_result_str` is a `struct bu_vls *` owned by the `struct ged`.  The main
thread (in `QgEdApp::run_cmd`) also reads and resets `ged_result_str` after
command execution.  If the subprocess produces output at the moment the main
thread is processing a command, a data race exists on `ged_result_str`.

The `(*callback)` call itself may write to `ged_result_str` as well
(depending on the callback implementation), compounding the hazard.

The design intent (QueuedConnection between the background thread and the main
thread) is correct for signaling the UI.  However, the access to `ged_result_str`
must be protected.  Options:
- Use `bu_semaphore_acquire`/`release` around `ged_result_str` access, OR
- Have the callback write to a thread-local buffer and pass the result to the
  main thread via the `finishedGetLine` queued signal, OR
- Restructure so that `ged_result_str` is only read/written on the main thread.

**Severity:** High — data race; undefined behaviour in the C++ memory model.

### 5.2 DbiState / DrawList / SelectionSet — main-thread-only (documented)

As of Phase 9, `include/ged/dbi.h` carries "MAIN THREAD ONLY" annotations on
`DbiState`, `BViewState`, `DrawList`, and `SelectionSet`.  There are no locks
or synchronisation primitives, and none of these classes are used from background
threads today.

The L1 (background geometry loading) work item requires this constraint to be
relaxed in a controlled way.  The approved approach (Section 9, Q4 decision in
`DBI_OLD_ANALYSIS.md`) is a single background worker thread posting to a result
queue.  This work has not started.

**Severity:** Low currently (no concurrent access today); design must be respected
when L1 is implemented.

---

## 6. Memory Management

### 6.1 QgModel tree (raw pointer forest)

`QgItem` nodes are allocated with `new QgItem(...)` and stored in:
- `QgModel::rootItem` — root of the tree
- `QgItem::children` (`std::vector<QgItem *>`)
- `QgModel::items` (`std::unordered_set<QgItem *> *`)

The model's destructor and the `full_model_reset()` path delete these manually.
This is correct today, but the combination of three separate ownership containers
for the same objects is fragile: if any of the three gets out of sync with the
others (e.g., an item is removed from `children` but not from `items`), a
double-free or use-after-free results.

`QgItem` nodes should be owned by a single container (e.g., `rootItem` with
`std::unique_ptr<QgItem>` children), with `items` being a non-owning index (raw
pointers or hash → pointer map) into the owned tree.

**Severity:** Medium.

### 6.2 `QgModel::items` — raw pointer set

`QgModel::items` is itself heap-allocated (`items = new std::unordered_set<QgItem *>()`).
There is no reason for the set itself to be on the heap; it should be a direct
member (`std::unordered_set<QgItem *> items_`).

**Severity:** Low.

### 6.3 `QgEdApp::tmp_av` — manual `char *` management

```cpp
// QgEdApp.cpp:517
for (size_t i = 0; i < tmp_av.size(); i++) {
    delete tmp_av[i];   // BUG: should be bu_free() — they were allocated with bu_strdup (malloc)
}
```

`tmp_av` stores `char *` strings allocated via `bu_strdup` (which internally
uses `malloc`/`strdup`).  These should be freed with `bu_free`, not `delete`.
Using `delete` on a `malloc`-allocated block is undefined behaviour in C++.
The correct approach is `std::vector<std::string>` (no manual management) or
`bu_free` to match the allocator.

**Severity:** High — undefined behaviour (mixing `malloc` and `delete`).

---

## 7. Error Handling

### 7.1 Silent failures in `load_g_file`

`QgEdApp::load_g_file` returns `BRLCAD_ERROR` with a message in the status bar
if the file cannot be opened.  The status bar is a small text area at the bottom
of the window that the user may not notice, and the error message is not
preserved after the next operation.  Important errors (file not found, format
conversion failure, permission denied) should use a modal `QMessageBox`.

**Severity:** Medium — poor user experience.

### 7.2 No error feedback for failed geometry import

`QgGeomImport` shows import dialogs but handles import failures silently via
a comment `// TODO - at least do a basic validity check...`.

**Severity:** Low.

### 7.3 Unchecked return values

Several `ged_exec()` and related call sites in `QgEdApp.cpp` check the return
code, but do not propagate or display the error consistently.  The `[[nodiscard]]`
attribute should be applied to BRL-CAD functions whose return values indicate
errors.

**Severity:** Low.

---

## 8. Technical Debt — Commented-Out Code and TODOs

### 8.1 In-code TODOs (37 confirmed)

| Category | Count | Representative items |
|---|---|---|
| Qt API / version | 4 | QgConsole Qt6 completion; QgFlowLayout Qt6 geometry; QShortcut for key bindings (×2) |
| Feature missing | 11 | fbserv QLocalSocket; rt_vlfree cleanup; plugin type expansion; CADViewSelector signal wiring; QgTreeView filtering; QgQuadView default views; QPolyMod visual-only update |
| Design / refactoring | 9 | raytrace.h move (×2); QgEdMainWindow view-update consolidation; QgConsole prompt/input unification; QgModel QActions note; fbserv copy-test for framebuffer |
| Architecture | 7 | key bindings model (bindings.cpp ×2); view mode trigger (QgTreeView); QgGL/QgSW camera (bv_mat_aet comment ×2); command-line bindings callback; polygon shape visual props |
| Minor / low priority | 6 | QPolyMod copy operation; QEll default values and UP/DOWN labels; QgMeasureFilter completeness; QgSelectFilter rectangle test |

### 8.2 Unanswered qged/TODO items

The `src/qged/TODO` file contains 25 numbered items.  The ones marked `x` or
`X` are done.  Remaining open items:

| Number | Description | Priority |
|---|---|---|
| 6 | Primitive editing (sed/oed equivalent) | High |
| 9 | Key binding configuration (QShortcut / dm bindings command) | High |
| 12 | Qt dialogs for color and font selection | Medium |
| 13 | `.mgedrc` parsing (possibly via embedded jimtcl) | Medium |
| 17 | Command-line supported key binding configuration | Medium |
| 18 | Raytrace control panel | Medium |
| 19 | Dedication dialog | Low |
| 20 | Editor widgets (Comb Editor, Bot Edit, Build Pattern, Overlap tool) | High |
| 22 | Dock widget title → menu when floating | Low |
| 23 | Archer help browser | Low |
| 24 | Sketch editor (polygon work is a foundation) | Medium |

---

## 9. Plugin System

The plugin infrastructure (`src/qged/plugins/plugin.h`) is minimal:

- `qged_plugin` structs hold arrays of `qged_tool *` pointers
- `qged_tool_impl` holds a single `tool_create` function pointer
- Three API version constants exist (`QGED_CMD_PLUGIN`, `QGED_VC_TOOL_PLUGIN`,
  `QGED_OC_TOOL_PLUGIN`) suggesting multiple panel targets

However, `plugin.h` itself notes the limitation:
```
TODO - need to support more than just QgToolPaletteElement types being
created by plugins.  The four logical candidates so far:
1. GED-style command line commands
2. QgToolPaletteElement (currently what we're using)
3. QDockWidget items
4. Full-fledged dialogs
```

Current plugins all create `QgToolPaletteElement` objects.  A dock-widget plugin
(e.g., a dedicated Combination Editor or Overlap Tool) cannot be expressed with
the current API.

The plugin dispatch in `QgEdApp` does not expose the command-registration path,
so plugins cannot add new `libged`-style commands.

**Severity:** Medium — limits what plugins can express.

---

## 10. Open Architecture Items

### L1 — Background geometry loading (not started)

**Decision** (from `DBI_OLD_ANALYSIS.md` Section 9 Q4): single background worker
thread posting to a result queue; `concurrentqueue.h` is the candidate queue
implementation.

Required design work:
1. Define which `DbiState` methods may be called from the background thread
   (likely: read-only path lookups only).
2. Define the result queue schema: what data the background thread produces
   and how it is handed off to the main thread.
3. Add `lock()`/`unlock()` RAII guards to `DbiState` for the mutation paths
   that the background thread might trigger indirectly.
4. Connect the background thread's completion signal to `BViewState::redraw()`
   on the main thread via `Qt::QueuedConnection`.

This is the single highest-impact remaining architecture item.

### Primitive editing (qged/TODO #6)

The `qged/TODO` file describes the correct approach:
- A "view object" is created from the database solid (a transient `DrawList`
  entry pointing to a modified copy).
- Edit operations mutate the view object (not the database).
- On accept: `DbiState::sync()` is called with the edited object written back.
- On reject: the transient entry is removed.

This maps cleanly to the current `DrawList` + `DbiState::sync()` API.  What
is needed is:
- A `BViewState::begin_edit_session(DbiPath)` / `end_edit_session(bool accept)`
  pair that manages the transient `DrawList` entry.
- Per-primitive edit UI panels (the `QEll` plugin is a prototype of this pattern).
- A `libged` refactoring so that edit-command logic can operate on the view object
  without requiring the database round-trip.

### View update consolidation (`QgEdMainWindow.cpp:252`)

The TODO at line 252 notes that the view update logic should be rolled into a
cleaner single cycle.  The current `do_view_changed` → `view_update` signal →
`do_view_update` slot → repaint chain has multiple entry points and the comment
warns about potential infinite loops.

A single `ViewUpdateRequest` event (posted with `QMetaObject::invokeMethod(...,
Qt::QueuedConnection)`) that coalesces multiple update requests within one event
loop iteration would eliminate the reentrancy risk and simplify the signal graph.

### BViewState::link_to — full DrawList sharing (A2 partial)

`BViewState::link_to(BViewState *primary)` stores the pointer but does not yet
route `redraw()` through the primary's `DrawList`.  The quad view should share
draw state between views.  This is the remaining half of A2/A3.

---

## 11. Positive Findings

The following aspects of the codebase are well-implemented:

1. **DBI layer** — `DbiState`, `DrawList`, `SelectionSet`, `IDbiObserver`,
   and `GObj`/`CombInst` form a clean, testable foundation.  The `DbiPath`
   value type eliminates a class of hash-confusion bugs.

2. **Qt model/view separation** — `QgModel` correctly implements
   `QAbstractItemModel` with `QAbstractItemModelTester` in Fatal mode passing.
   Incremental `beginInsertRows`/`endInsertRows`/`dataChanged` calls preserve
   the user's expanded/collapsed tree state across most edits.

3. **Signal/slot modernity** — approximately 80% of `connect()` calls use the
   type-safe pointer-to-member syntax.  SIGNAL/SLOT string macros are not used.

4. **Thread boundary** — `QgConsoleListener` correctly uses `moveToThread()`
   and `Qt::QueuedConnection` to hand I/O events back to the main thread.  The
   data-race on `ged_result_str` (Section 5.1) is an implementation gap, not an
   architectural flaw.

5. **Test coverage for DBI** — `test_dbi_c.c` (35 checks), `test_dbi_cpp.cpp`
   (49 checks), and `qgmodel.cpp` (15 checks including `QAbstractItemModelTester`
   in Fatal mode) provide a solid regression base.

6. **Plugin extensibility** — the view tool plugins (select, settings, measure,
   info) and the polygon and ellipsoid editing plugins show that the plugin
   architecture can deliver meaningful functionality.

7. **draw/erase pipeline** — `DrawList` correctly drives `BViewState::redraw()`,
   with `add_hpath()` writing to the draw list and `erase_hpath()` removing
   immediately.  The `staged` queue anti-pattern has been removed.

---

## 12. Prioritised Recommendations

### Tier 1 — Correctness issues (fix first)

| Item | File(s) | Effort |
|---|---|---|
| Fix `QgEdApp::tmp_av` — use `bu_free` not `delete`, or switch to `std::vector<std::string>` | `QgEdApp.cpp:517` | 30 min |
| Fix `QgConsoleListener` data race on `ged_result_str` (Section 5.1) | `QgConsoleListener.cpp` | 2 h |

### Tier 2 — Safety and maintenance (address soon)

| Item | File(s) | Effort |
|---|---|---|
| Add `override` to all virtual overrides in `qged/` and `libqtcad/` | Many | 2 h |
| Replace `NULL` with `nullptr` throughout | Many | 1 h |
| Migrate 9 `#include "bv.h"` → `#include "bsg.h"` | 9 files | 30 min |
| Convert `QgModel::rootItem` / `items` to `std::unique_ptr` ownership | `QgModel.cpp/.h` | 4 h |
| Move `raytrace.h`-dependent sketch export out of `libqtcad` into `qged` | `QgView.cpp`, `QgPolyFilter.cpp` | 4 h |
| Migrate `fbserv.cpp` from `QTcpServer` to `QLocalServer` / `QLocalSocket` | `fbserv.cpp/h` | 4 h |

### Tier 3 — C++17 and Qt modernisation

| Item | File(s) | Effort |
|---|---|---|
| Add `[[nodiscard]]` to `run_cmd()` and other error-returning functions | Several | 1 h |
| Replace hand-written iterator loops with range-based `for` + `auto` | Several | 2 h |
| Implement `QgFlowLayout` Qt6 geometry path | `QgFlowLayout.cpp` | 2 h |
| Implement `QgConsole` mid-string completion for Qt6 | `QgConsole.cpp` | 3 h |
| Apply `[[maybe_unused]]` to replace `UNUSED()` macro calls | Several | 1 h |
| Use `std::optional<T>` for functions that may return "not found" | `QgModel.cpp` | 4 h |

### Tier 4 — Feature completeness

| Item | Effort |
|---|---|
| Primitive editing (sed/oed, `BViewState::begin_edit_session`) | 3-4 weeks |
| Background geometry loading (L1, documented design above) | 2-3 weeks |
| Key binding configuration (QShortcut integration, dm bindings command) | 1 week |
| Raytrace control panel Qt widget | 2 weeks |
| Expand plugin API (dock widgets, full-window dialogs, GED commands) | 1-2 weeks |
| Qt dialogs for color/font settings | 3 days |
| BViewState::link_to full DrawList sharing (A2 completion) | 3 days |

---

## 13. Comparison with Previous Analysis

The original analysis in `DBI_OLD_ANALYSIS.md` identified ten structural problems
(P1–P10) in the DBI layer and proposed a nine-phase redesign.  All nine phases
are now complete.  The problems diagnosed in that document have been resolved:

| Original problem | Resolution |
|---|---|
| P1 — Flat hash-table soup | ✅ `GObj`/`CombInst` object model |
| P2 — No type safety on hashes | ✅ `GHash`, `InstHash`, `PathHash`, `DbiPath` |
| P3 — Ownership unclear | ✅ `unique_ptr` in `DbiState`; "MAIN THREAD ONLY" docs |
| P4 — Conflated responsibilities | ✅ `DbiState`/`DrawList`/`SelectionSet` separated |
| P5 — Fragile two-flag change notification | ✅ `IDbiObserver` / `DbiChangeEvent` |
| P6 — Redundant path bookkeeping | ✅ `DrawList` is the single draw-intent source |
| P7 — Qt model not decoupled | ✅ `on_dbi_changed()` with incremental row updates |
| P8 — Lazy load not robust to edits | ✅ `rebuild_item_children()` + precise insert/remove |
| P9 — `redraw()` too coarse | ✅ `DrawList` pending-entry loop; partial path support |
| P10 — `BSelectState` O(N×M) | ✅ `SelectionSet` BFS via `GObj`/`CombInst` graph |

**What was NOT covered by the previous analysis** (newly identified here):
- The `tmp_av` `bu_free`/`delete` mismatch (Section 6.3) — correctness bug
- The `QgConsoleListener` data race on `ged_result_str` (Section 5.1) — race condition
- The `bv.h` → `bsg.h` migration gap in qged/libqtcad (Section 4.2)
- The `raytrace.h` dependency in libqtcad (Section 4.3)
- The pervasive absence of `override`, `nullptr`, and `auto` (Sections 3.1–3.4)
- The `QgFlowLayout` and `QgConsole` Qt6 gaps (Section 4.4)
- The `fbserv` TCP security concern (Section 4.5)
- The `QgModel` dual ownership container fragility (Section 6.1)

---

## 14. Summary

The qged technology stack has been significantly improved by the nine-phase DBI
redesign.  The *libged* layer is now architecturally sound: it has a proper
object model, type-safe paths, a clean observer interface, a correct draw-list
pipeline, and a solid test base.

The *application layer* (`qged` and `libqtcad`) has not been modernised to the
same degree.  It functions correctly for its current feature set, but it carries
substantial C++17/Qt style debt (NULL, missing override, absent smart pointers,
obsolete includes) and has two correctness issues that should be fixed promptly:
the `tmp_av` allocator mismatch and the `QgConsoleListener` data race.

The highest-impact next steps in order are:
1. Fix the two correctness bugs (Tier 1 above)
2. Add `override`, replace `NULL`, migrate `bv.h` (Tier 2, mechanical)
3. Move `raytrace.h` out of `libqtcad`; migrate `fbserv` to `QLocalSocket`
4. Implement background geometry loading (L1)
5. Implement primitive editing

After completing Tiers 1–3, the codebase will meet C++17 and Qt best practices
for its current scope, and the remaining feature gaps (Tier 4) can be addressed
incrementally.
