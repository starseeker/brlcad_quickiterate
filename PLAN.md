# NURBS Boolean Evaluation – Development Plan

## Overview

BRL-CAD has infrastructure for evaluating Boolean operations on NURBS B-Rep
(Boundary REPresentation) geometry, but the implementation is incomplete and
error prone.  The goal of this work is to achieve robust, reliable Boolean
evaluation of NURBS-based CSG trees, with success defined as converting all
`share/db/*.g` CSG example models to evaluated NURBS Boolean outputs
successfully.  A bonus stage is wiring this ability into the `g-step` exporter
for high-quality STEP AP203 export.

Reference documentation: `brlcad/doc/docbook/devguides/bool_eval_development.xml`

---

## 1. Current State Assessment

### 1.1 Core Files

| File | Lines | Purpose |
|------|-------|---------|
| `src/libbrep/boolean.cpp` | 4,093 | Main Boolean algorithm (ON_Boolean entry point) |
| `src/libbrep/intersect.cpp` | 4,572 | Surface–surface and curve–curve intersection (SSI) |
| `src/libbrep/PullbackCurve.cpp` | 4,656 | Surface parameterization / pullback |
| `src/libbrep/opennurbs_ext.cpp` | 2,317 | OpenNURBS extensions |
| `src/libbrep/Subcurve.cpp` | – | Curve subdivision helper |
| `src/libbrep/Subsurface.cpp` | – | Surface subdivision / bounding-box tree |

### 1.2 Algorithm Overview

The implementation follows the *BOOLE* approach (Krishnan et al., 1995) with
surface–surface intersection (SSI) from Krishnamurthy et al., 2008:

1. **Trivial case check** – if bounding boxes don't overlap, result is trivially
   one or both inputs.
2. **SSI** – for every pair of faces from the two BREPs whose bounding boxes
   overlap, compute intersection curves via Newton iteration on a subdivision
   tree (`Subsurface` / `MAX_SSI_DEPTH = 8`).
3. **Face splitting** – split each face along intersection curves to produce
   `TrimmedFace` objects.
4. **Classification** – call `face_brep_location()` on each trimmed face to
   determine inside/outside/on-surface status via ray casting.
5. **Assembly** – collect faces marked `BELONG`, copy surfaces/loops/trims
   into the output `ON_Brep`, call `standardize_loop_orientations()`, and
   validate with `IsValid()`.

Entry points:
- `ON_Boolean()` in `src/libbrep/boolean.cpp`
- `rt_brep_boolean()` in `src/librt/primitives/brep/brep.cpp` (wraps ON_Boolean for librt)
- `brep` GED command in `src/libged/brep/brep.cpp` (user-facing)

### 1.3 Primitive BREP Conversion Coverage

Most CSG primitives have dedicated `*_brep.cpp` files that convert the implicit
solid into an `ON_Brep`:

| Primitive | File | Status |
|-----------|------|--------|
| arb8 | arb8/arb8_brep.cpp | OK |
| arbn | arbn/arbn_brep.cpp | OK |
| ars  | ars/ars_brep.cpp | OK |
| bot  | bot/bot_brep.cpp | OK |
| bspline | bspline/bspline_brep.cpp | OK |
| dsp  | dsp/dsp_brep.cpp | Review needed |
| ebm  | ebm/ebm_brep.cpp | Review needed |
| ehy  | ehy/ehy_brep.cpp | OK |
| ell  | ell/ell_brep.cpp | Incomplete (see §2.2) |
| epa  | epa/epa_brep.cpp | OK |
| eto  | eto/eto_brep.cpp | OK |
| extrude | extrude/extrude_brep.cpp | OK |
| **half** | half/half_brep.cpp | **Returns NULL – broken** |
| hyp  | hyp/hyp_brep.cpp | OK |
| part | part/part_brep.cpp | OK |
| pipe | pipe/pipe_brep.cpp | Review needed |
| revolve | revolve/revolve_brep.cpp | OK |
| rhc  | rhc/rhc_brep.cpp | OK |
| rpc  | rpc/rpc_brep.cpp | OK |
| sph  | sph/sph_brep.cpp | OK |
| superell | superell/superell_brep.cpp | OK |
| tgc  | tgc/tgc_brep.cpp | Minor issues (see §2.3) |
| tor  | tor/tor_brep.cpp | OK (delegates to OpenNURBS) |
| vol  | vol/vol_brep.cpp | Review needed |
| **hrt** | *missing* | **No brep file** |
| **metaball** | *missing* | **No brep file** |
| **rec** | *missing* | **No brep file** (use tgc?) |

### 1.4 Known Bugs and Limitations

#### Critical / Documented FIXMEs

1. **Memory leaks in `link_curves()`** (`boolean.cpp` lines 1418, 1429)
   `ON_LineCurve` objects created to bridge gaps between linked SSI curves are
   never freed.  Root cause: `SSICurve` is stored in `ON_SimpleArray`, which
   does not call destructors; therefore `~SSICurve()` → `delete m_curve` is
   never executed.  Fix: change `m_ssi_curves` in `LinkedCurve` to
   `ON_ClassArray<SSICurve>` and implement proper copy semantics for
   `SSICurve`.

2. **Half-space primitive returns NULL** (`half/half_brep.cpp` line 43)
   The `rt_hlf_brep()` function explicitly returns a NULL brep pointer. Any
   CSG tree containing a halfspace (e.g., `boolean-ops.g` models) will fail
   entirely.  Fix: implement a large finite box/trimmed-plane approximation
   bounded by the model bounding box.

3. **Coplanar face deduplication** (`boolean.cpp` line 3810 TODO)
   When two faces lie on the same surface (overlapping BREPs), both are
   included in the output.  This produces redundant overlapping faces.

4. **Degenerate face point-in-face test is inefficient** (`boolean.cpp`
   line 3343 TODO)
   Grid-based sampling for heavily-trimmed faces scales poorly.  Switch to
   sampling around inner loop points when the initial grid fails.

#### Medium Priority

5. **Overlapping detection heuristic** (`intersect.cpp` lines 1339, 1994
   FIXME) — current method of checking if isocurves intersect both surfaces is
   unreliable.

6. **Curve sampling may miss intersections** (`intersect.cpp` lines 2956,
   3170 TODO) — more sample points needed for complex surfaces.

7. **Even/odd test for inside-loop check** (`intersect.cpp` line 3986 TODO)

8. **Curves inside loop interior** (`intersect.cpp` line 4012 FIXME) — line
   detection of curves inside loop not comprehensive.

9. **Curve fitting threshold uncertain** (`intersect.cpp` lines 4214–4219
   TODO) — no systematic validation of the fitting threshold.

10. **Exception silencing** (`boolean.cpp` lines 3733–3737) — when
    `face_brep_location()` throws, faces are silently marked NOT_BELONG.

#### Lower Priority

11. **Torus parameterization** (`boolean.cpp` lines 3460–3544) —
    arc-length parameterization path is commented-out in some places.

12. **Hardcoded tolerances** — `INTERSECTION_TOL = 1e-4` and TGC domain
    `[-100, 100]` are not scale-aware.

13. **XOR operation** — implemented but untested.

---

## 2. Phased Implementation Plan

### Phase 0: Build Infrastructure & Baseline (1–2 days)

- [ ] Build BRL-CAD with current code (cmake flags per instructions).
- [ ] Identify which `share/db/*.g` models can currently be converted end-to-end
      using the `brep` command (manual or scripted test).
- [ ] Document the pass/fail matrix (model × conversion step) in a file
      `brep_baseline.txt`.
- [ ] Verify the existing regression tests pass:
      `test_brep_ppx`, NURBS ray tests.

### Phase 1: Fix Critical Bugs (1 week)

**1a. Memory leak fix — `link_curves()` in `boolean.cpp`**
- Change `ON_SimpleArray<SSICurve>` to `ON_ClassArray<SSICurve>` in the
  `LinkedCurve` struct (or add explicit freeing in `LinkedCurve::Empty()`
  that iterates and deletes `m_curve` pointers before calling
  `m_ssi_curves.Empty()`).
- Add a copy constructor to `SSICurve` that deep-copies `m_curve` to ensure
  safe ownership semantics when `LinkedCurve` is copied.
- Remove the FIXME comments at lines 1418 and 1429.

**1b. Half-space BREP conversion**
- Implement `rt_hlf_brep()` to produce a large finite box (e.g., 10× the
  model bounding box diagonal) trimmed to the half-space.  The trimmed plane
  can be modeled as an `ON_PlaneSurface` with a very large domain, trimmed by
  a rectangular outer loop.
- This makes all models containing `half` primitives (including `boolean-ops`,
  `operators`) eligible for conversion.

**1c. Coplanar face deduplication**
- In `categorize_trimmed_faces()`, when two faces have `ON_BREP_SURFACE`
  status, retain only one (the brep1 face for UNION/INTERSECT, the brep2 face
  for DIFF when appropriate).  Address the TODO at line 3810.

### Phase 2: Primitive Conversion Audit (1 week)

For each primitive listed in §1.3:

- **ell**: Audit whether the implicit outer loop (`NewOuterLoop`) produces a
  valid BREP with proper edges and trims.  Compare against `sph` which is
  known-good.  Add explicit seam edge if needed.
- **tgc** (and **rec** which should reuse tgc): Replace hardcoded domain
  `[-100, 100]` with a value derived from the ellipse radii.  Validate edge
  continuity at junctions between ruled and planar faces.
- **dsp / ebm / vol**: These produce height-field or voxel geometry; verify
  the BREP is a valid closed solid.
- **pipe**: Verify watertight seam handling for pipe segments.
- **hrt, metaball, rec**: Implement missing `*_brep.cpp` stubs (or note that
  they are genuinely deferred per README).

Priority: work through primitives actually appearing in `share/db/*.g`.

### Phase 3: SSI Robustness Improvements (2–3 weeks)

**3a. Sampling density** (intersect.cpp lines 2956, 3170)
- Increase the default number of sample points or make it adaptive based on
  surface curvature.

**3b. Overlap detection** (intersect.cpp lines 1339, 1994)
- Replace the isocurve-crossing heuristic with a more reliable approach
  (e.g., point-on-surface test at the center of candidate overlap regions).

**3c. Even/odd inside-loop test** (intersect.cpp line 3986)
- Implement a proper winding-number or even/odd crossing count.

**3d. Curve fitting** (intersect.cpp lines 4214–4219)
- Derive the fitting threshold from `INTERSECTION_TOL` and surface curvature
  rather than a magic constant.

**3e. Degenerate face sampling** (boolean.cpp line 3343)
- After grid sampling fails, fall back to sampling around inner-loop
  midpoints to find a guaranteed-interior point.

### Phase 4: Validation Against `share/db/*.g` (ongoing)

For each converted model from Phase 0:

- [ ] Run `brep <combination>` and check for success.
- [ ] Check `IsValid()` output; failures indicate topology errors.
- [ ] For complex failures, enable `DEBUG_BREP_BOOLEAN` and `dplot`
      visualization (DebugPlot) to identify the problematic face pair.
- [ ] Fix any primitive-specific conversion issues found, then re-run.

Priority order (simplest to hardest):
1. `moss.g` (simple ellipsoids, spheres)
2. `cube.g` (ARB8s)
3. `prim.g` / `primitives.g` (all standard primitives)
4. `boolean-ops.g` (exercises many operation types, requires half-space fix)
5. `ktank.g`, `havoc.g`, `m35.g` (complex assemblies)
6. `terra.g`, `wave.g` (DSP/VOL — may be deferred per README)

### Phase 5: Regression Test Suite (1 week)

- [ ] Create a `regress/brep_bool/` directory with a CMake-driven regression
      script.
- [ ] For each successfully converted model, store the expected BREP property
      (volume, surface area, face/edge count) as a reference.
- [ ] Automate `brep <comb>` → validate output BREP → compare reference.
- [ ] Add to CI.

### Phase 6: Bonus — g-step Integration

The `g-step` exporter (`src/conv/step/`) converts BRL-CAD geometry to STEP
AP203.  Currently it emits faceted geometry rather than true NURBS surfaces.
Once the Boolean evaluator is robust:

- [ ] Identify the conversion path in `src/conv/step/` where NURBS output
      could be inserted.
- [ ] Wire `rt_brep_boolean()` into the STEP export pipeline so that
      combinations are evaluated to a single NURBS solid before export.
- [ ] Validate output with a STEP viewer / AP203 validator.

---

## 3. Key Algorithms and References

| Algorithm | Reference | Implementation |
|-----------|-----------|----------------|
| BREP Boolean evaluation | Krishnan et al., "BOOLE: A System to Compute Boolean Combinations of Sculptured Solids," 1995 | `boolean.cpp` |
| Surface–surface intersection | Krishnamurthy et al., "Performing Efficient NURBS Modeling Operations on the GPU," 2008 | `intersect.cpp` |
| 2D polygon Boolean | Margalit & Knott, "An Algorithm for Computing the Union, Intersection or Difference of Two Polygons," 1989 | `boolean.cpp` (loop_boolean) |
| Curve pullback | — | `PullbackCurve.cpp` |
| NURBS representation | OpenNURBS by Robert McNeel & Associates | `opennurbs_ext.cpp`, external library |

---

## 4. Debugging Tools

- **`DEBUG_BREP_BOOLEAN`** flag in `boolean.cpp` – enables verbose logging of
  face categorization decisions.
- **`DebugPlot`** (`src/libbrep/debug_plot.cpp`) – writes intermediate geometry
  to `.vl` files viewable in BRL-CAD's display tools.  Enable by uncommenting
  `dplot = new DebugPlot(...)` in `ON_Boolean()`.
- **`brep` command** with info subcommands – inspect individual face / edge /
  trim structures of a stored BREP object in MGED.
- **`ON_Brep::IsValid(&log)`** – built-in validation that identifies topology
  errors; already called at end of `ON_Boolean()`.

---

## 5. File Change Summary

The following files are expected to be modified in the course of this work:

| File | Changes |
|------|---------|
| `src/libbrep/boolean.cpp` | Fix memory leaks; fix coplanar dedup; fix degenerate sampling; fix exception handling |
| `src/libbrep/intersect.cpp` | Fix overlapping detection; improve sampling; fix curve fitting |
| `src/librt/primitives/half/half_brep.cpp` | Implement half-space BREP |
| `src/librt/primitives/ell/ell_brep.cpp` | Add explicit seam edge if needed |
| `src/librt/primitives/tgc/tgc_brep.cpp` | Fix domain sizing |
| `src/librt/primitives/*/` | Miscellaneous primitive fixes as discovered |
| `regress/brep_bool/` | New regression test suite |
| `brlcad/doc/docbook/devguides/bool_eval_development.xml` | Update as code changes |
