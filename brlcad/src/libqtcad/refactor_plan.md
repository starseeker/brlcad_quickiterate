Phase 1 — Style and surface conventions (mechanical, low-risk)

Current tree notes:
- Pointer-to-member connect syntax is already in use across libqtcad; Q_DISABLE_COPY_MOVE is already present on the public Q_OBJECT widgets/models touched so far.
- The naming cleanup has started: QgConsoleListener, QgPoly*Filter, and QgAsc/QgRhino/QgStepImportDialog are the exported names now, with compatibility aliases still present.
- QgTypes.h already centralizes the QgView_*/quadrant/edit-mode/comb-type compatibility constants behind enum class definitions plus Q_DECLARE_FLAGS for view-update flags.
- Remaining work from this phase is mostly a residual style sweep (for example, lingering NULL usage in tests/docs and any remaining missing override/delete cleanups).

- Adopt a uniform style: replace NULL with nullptr in libqtcad headers and .cpp files; switch all signal/slot connections to the pointer-to-member-function form; apply override everywhere appropriate; add Q_DISABLE_COPY_MOVE (or = delete'd copy/move) to all Q_OBJECT classes.
- Normalize naming: rename the un-prefixed filter classes (QPolyCreateFilter → QgPolyCreateFilter, etc.), QConsoleListener → QgConsoleListener (already the filename), ASCImportDialog/RhinoImportDialog/STEPImportDialog → QgAsc/Rhino/StepImportDialog, give them QTCAD_EXPORT, and provide back-compat typedefs for one release if needed.
- Run clang-format/astyle over src/libqtcad and include/qtcad with the BRL-CAD style.
- Replace #define-style enums (QgView_*, UPPER_RIGHT_QUADRANT, Qg*EditMode, QG_VIEW_*, G_STANDARD_OBJ/…) with enum class (or Q_FLAGS for the bitwise view flags) declared inside the relevant class or a small QgTypes.h.

Phase 2 — Encapsulation pass

Current tree notes:
- This phase is started but not finished: QgGL/QgSW already expose defaultMouseMode as Q_PROPERTY and already have basic view()/displayManager()/frameBuffer() getters.
- QgView now keeps its active event-filter pointer private and manages it through add/remove helpers rather than exposing raw external writes.
- Public data still leaks heavily from QgItem/QgModel and the canvas widgets, and the headers still expose libged/libdm/bv/raytrace details directly.
- The next useful slice is to keep moving QgSW/QgGL state behind accessors so QgView/tests stop reaching into raw widget members.

- Promote raw public data to accessors where they're called from outside (compile, follow errors, add minimal getters/setters). Focus on the high-traffic classes first: QgItem/QgModel, QgSW/QgGL, QgQuadView, QgViewCtrl, QgToolPalette*.
- Introduce Q_PROPERTY for state visible to QML/Designer or for state that genuinely is user-configurable (icon size, default mouse mode, default key bindings, etc.).
- Apply the pimpl idiom to QgModel, QgGL, QgSW, QgQuadView, QgTreeView, QgAttributesModel, and QgToolPalette so the public headers no longer leak STL containers, libged structs, or per-implementation state. Keep the existing QgConsole::pqImplementation model.
- Move heavy C-header includes (raytrace.h, ged.h, dm.h, bv.h) out of the public headers wherever pimpl makes that possible. Public headers should declare opaque struct types and pull the implementations in only in the .cpp.

Phase 3 — Canvas widget unification

- Introduce a QgCanvasBase abstract widget interface (signals changed() / init_done(); slots need_update() / queued_update() / set_lmouse_move_default(int); accessors view(), dmp(), ifp(), stash_hashes(), diff_hashes(), aet(), save_image(), render_to_file(), get_viewport_image(), enable/disable*Bindings(), set_draw_custom()) implemented by QgGL (over QOpenGLWidget) and QgSW (over QWidget).
- Consolidate the duplicated event-handler and hash logic into a non-Qt helper (QgCanvasState) used by both subclasses; this eliminates the textual duplication noted in §2.4.
- Replace QgView's #ifdef BRLCAD_OPENGL member layout with a single QgCanvasBase *canvas and a factory function chosen in the .cpp.
- Move bindings.{h,cpp} (currently private to libqtcad) into a real QgCanvasInput class behind the canvas base; its current free-function API is awkward to test.

Phase 4 — Filter hierarchy unification

- Define a single QgViewFilter base in a new QgViewFilter.h: a QObject with a bview *v (pimpl-hidden), a common view_sync() helper, a view_updated(int) signal, and a documented contract for installing/removing from a QgView.
- Refactor QgPolyFilter, QgSelectFilter, QgMeasureFilter, QgSketchFilter (and all their derived classes) onto that base; delete the duplicated view_sync() copies.
- Move filter installation into QgView::installFilter(QgViewFilter*) rather than today's lower-level add_event_filter(QObject*) (which is too generic) — clearer ownership and lifetime.

Phase 5 — Signal/slot conventions

- Tighten QgSignalFlags.h into a Q_FLAG-decorated enum class and make every signal carrying view flags use it (no more unsigned long long).
- Audit and unify the signal names: pick one of view_changed/view_change/changed and remove the others (currently QgModel has both view_change(ull) and view_changed(ull)).
- Centralize the role enum: move QgModel::CADDataRoles into a shared QgRoles header and document the role IDs in one place.

Phase 6 — Model/data abstraction

- Introduce a thin QgSession (or QgDbiSource) class that owns the struct ged * / struct db_i * references currently scattered across QgModel, QgAttributesModel, QgTreeView, QgViewCtrl, QgGeomImport. Models receive a QgSession* and stop talking to ged_exec directly.
- Use the new session to implement signal-driven invalidation (one QgSession::changed(...) signal that fans out to models that subscribe) instead of the current "every widget knows everyone" pattern.
- Move QgCombType/QgIcon (free QgUtil functions) and the icon cache implied by QgItem::icon to a shared icon provider on the session, so icons aren't duplicated in every tree node.

Phase 7 — Namespacing and packaging

- Wrap all public symbols in namespace qtcad { … }; provide a deprecated using block in a compatibility header for one release.
- Switch the CMakeLists to target_include_directories/target_link_libraries with PUBLIC/PRIVATE/INTERFACE, enable CMAKE_AUTOMOC for this target only (set_target_properties(libqtcad PROPERTIES AUTOMOC ON)), and drop the hand-maintained qth_names list.
- Drop the Qt5 code paths once Qt6 is mandatory; if Qt5 must remain, hide the difference behind a single helper module.
- Add a Doxygen group @defgroup libqtcad and ensure every public class has at least a one-paragraph class-level comment.

Phase 8 — Test and CI coverage

Current tree notes:
- Headless QApplication coverage has already started: src/libqtcad/tests contains qgmodel/qgview test programs plus the offscreen ged_test_qged_swrast integration test, and the latter is wired into CTest with QT_QPA_PLATFORM=offscreen.
- The QAbstractItemModelTester item is still open; QgModel.h still carries the Model_Test TODO.

- Add headless QApplication-based unit tests (using QSignalSpy and QTest) for the most logic-heavy classes: QgModel (tree fetch/hierarchy), QgKeyValModel, QgAttributesModel, each filter family (synthetic mouse events), QgFlowLayout, QgToolPalette selection logic, QgConsole command echo / completion, QgSignalFlags flag round-tripping.
- Add a QAbstractItemModelTester instance over QgModel (the TODO in QgModel.h referencing wiki.qt.io/Model_Test becomes obsolete).
- Ensure the existing tests/ programs are run under xvfb in CI so canvas widgets actually instantiate.

Phase 9 — Threading and ownership cleanup

Current tree notes:
- The QgConsoleListener rename landed under Phase 1, but the ownership/threading cleanup itself does not appear to have started yet.

- Replace QgConsoleListener's inherited QThread m_thread with the QObject::moveToThread pattern, document the consumer/producer lifetime.
- Give QgConsole::listeners a private API and clear ownership semantics (transfer to std::unique_ptr or rely on QObject parent ownership).
- Document who owns the bview*/dm*/fb* for each canvas widget; remove the duplicated local_v versus v distinction where possible.

Sequencing and risk

- Phases 0–2 and 5 are low-risk (mechanical, no behavior change) and unblock everything else.
- Phase 3 (canvas unification) is the highest-value change for the drawing stack work currently in flight, but it must land while qged, archer, and the dm-qtgl/dm-swrast plugins are buildable at each step.
- Phase 4 (filter unification) has a localized blast radius (mostly src/qged/plugins/{polygon,view/measure,view/select,…}); doable independently.
- Phase 6 (session abstraction) is the most invasive design change and should follow Phase 2, after the headers no longer leak C structs.
- Phase 7 (namespacing) is best done last so it only renames once, with shim headers for one release.
- Phase 8 (tests) can and should be interleaved with the earlier phases — each refactored class lands with new tests.

At every step the gating check is:

cmake -DBRLCAD_ENABLE_QT=ON ...;
cmake --build … --target libqtcad qged archer dm-qtgl dm-swrast

and the existing tests under src/libqtcad/tests must continue to pass.
