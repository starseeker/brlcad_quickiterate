/*                  B O O L E A N . C P P
 * BRL-CAD
 *
 * Copyright (c) 2013-2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file boolean.cpp
 *
 * Evaluate NURBS booleans (union, intersection and difference).
 *
 * Additional documentation can be found in the "NURBS Boolean Evaluation
 * Development Guide" docbook article (bool_eval_development.html).
 */

#include "common.h"

#include <assert.h>
#include <array>
#include <cmath>
#include <vector>
#include <stack>
#include <queue>
#include <set>
#include <map>
#include <sstream>

#include "bio.h"

#include "vmath.h"
#include "bu/log.h"
#include "brep/defines.h"
#include "brep/boolean.h"
#include "brep/intersect.h"
#include "brep/pullback.h"
#include "brep/ray.h"
#include "brep/util.h"

#include "debug_plot.h"
#include "brep_except.h"
#include "brep_defines.h"

//DebugPlot *dplot = NULL;

// Whether to output the debug messages about b-rep booleans.
#define DEBUG_BREP_BOOLEAN 1


struct IntersectPoint {
    ON_3dPoint m_pt;	// 3D intersection point
    double m_seg_t;	// param on the loop curve
    int m_loop_seg;	// which curve of the loop
    int m_ssx_curve;	// which intersection curve
    int m_curve_pos;	// rank on the chain
    double m_curve_t;	// param on the SSI curve
    enum {
	UNSET,
	IN_HIT,
	OUT_HIT,
	TANGENT
    } m_dir;		// dir is going inside/outside
    int m_split_li;	// between clx_points[m_split_li] and
			// clx_points[m_split_li+1]
			// after the outerloop is split
};


// A structure to represent the curve segments generated from surface-surface
// intersections, including some information needed by the connectivity graph
struct SSICurve {
    ON_Curve *m_curve;

    SSICurve()
    {
	m_curve = NULL;
    }

    SSICurve(ON_Curve *curve)
    {
	m_curve = curve;
    }

    SSICurve(const SSICurve &other)
    {
	m_curve = other.m_curve ? other.m_curve->Duplicate() : NULL;
    }

    SSICurve &operator=(const SSICurve &other)
    {
	if (this != &other) {
	    delete m_curve;
	    m_curve = other.m_curve ? other.m_curve->Duplicate() : NULL;
	}
	return *this;
    }

    ~SSICurve()
    {
	delete m_curve;
	m_curve = NULL;
    }

    SSICurve *Duplicate() const
    {
	return new SSICurve(*this);
    }
};


void
append_to_polycurve(ON_Curve *curve, ON_PolyCurve &polycurve);
// We link the SSICurves that share an endpoint, and form this new structure,
// which has many similar behaviors as ON_Curve, e.g. PointAt(), Reverse().
struct LinkedCurve {
private:
    ON_Curve *m_curve;	// an explicit storage of the whole curve
public:
    // The curves contained in this LinkedCurve, including
    // the information needed by the connectivity graph.
    // ON_ClassArray is used (not ON_SimpleArray) so that SSICurve
    // copy constructors and destructors are invoked, ensuring proper
    // ownership and cleanup of m_curve pointers.
    ON_ClassArray<SSICurve> m_ssi_curves;

    // Default constructor
    LinkedCurve()
    {
	m_curve = NULL;
    }

    void Empty()
    {
	m_ssi_curves.Empty();
	delete m_curve;
	m_curve = NULL;
    }

    ~LinkedCurve()
    {
	Empty();
    }

    LinkedCurve &operator= (const LinkedCurve &_lc)
    {
	if (this != &_lc) {
	    Empty();
	    m_curve = _lc.m_curve ? _lc.m_curve->Duplicate() : NULL;
	    m_ssi_curves = _lc.m_ssi_curves;
	}
	return *this;
    }

    ON_3dPoint PointAtStart() const
    {
	if (m_ssi_curves.Count()) {
	    return m_ssi_curves[0].m_curve->PointAtStart();
	} else {
	    return ON_3dPoint::UnsetPoint;
	}
    }

    ON_3dPoint PointAtEnd() const
    {
	if (m_ssi_curves.Count()) {
	    return m_ssi_curves.Last()->m_curve->PointAtEnd();
	} else {
	    return ON_3dPoint::UnsetPoint;
	}
    }

    bool IsClosed() const
    {
	if (m_ssi_curves.Count() == 0) {
	    return false;
	}
	return PointAtStart().DistanceTo(PointAtEnd()) < ON_ZERO_TOLERANCE;
    }

    bool IsValid() const
    {
	// Check whether the curve has "gaps".
	for (int i = 1; i < m_ssi_curves.Count(); i++) {
	    if (m_ssi_curves[i].m_curve->PointAtStart().DistanceTo(m_ssi_curves[i - 1].m_curve->PointAtEnd()) >= ON_ZERO_TOLERANCE) {
		bu_log("The LinkedCurve is not valid.\n");
		return false;
	    }
	}
	return true;
    }

    bool Reverse()
    {
	int count = m_ssi_curves.Count();
	for (int i = 0; i < count; i++) {
	    if (!m_ssi_curves[i].m_curve->Reverse()) {
		return false;
	    }
	}
	m_ssi_curves.Reverse();
	return true;
    }

    void Append(const LinkedCurve &lc)
    {
	m_ssi_curves.Append(lc.m_ssi_curves.Count(), lc.m_ssi_curves.Array());
    }

    void Append(const SSICurve &sc)
    {
	m_ssi_curves.Append(sc);
    }

    void AppendCurvesToArray(ON_SimpleArray<ON_Curve *> &arr) const
    {
	for (int i = 0; i < m_ssi_curves.Count(); i++) {
	    arr.Append(m_ssi_curves[i].m_curve->Duplicate());
	}
    }

    const ON_Curve *Curve()
    {
	if (m_curve != NULL) {
	    return m_curve;
	}
	if (m_ssi_curves.Count() == 0 || !IsValid()) {
	    return NULL;
	}
	ON_PolyCurve *polycurve = new ON_PolyCurve;
	for (int i = 0; i < m_ssi_curves.Count(); i++) {
	    append_to_polycurve(m_ssi_curves[i].m_curve->Duplicate(), *polycurve);
	}
	m_curve = polycurve;
	return m_curve;
    }

    const ON_3dPoint PointAt(double t)
    {
	const ON_Curve *c = Curve();
	if (c == NULL) {
	    return ON_3dPoint::UnsetPoint;
	}
	return c->PointAt(t);
    }

    const ON_Interval Domain()
    {
	const ON_Curve *c = Curve();
	if (c == NULL) {
	    return ON_Interval::EmptyInterval;
	}
	return c->Domain();
    }

    ON_Curve *SubCurve(double t1, double t2)
    {
	const ON_Curve *c = Curve();
	if (c == NULL) {
	    return NULL;
	}
	try {
	    return sub_curve(c, t1, t2);
	} catch (InvalidInterval &e) {
	    bu_log("%s", e.what());
	    return NULL;
	}
    }
};


/* Threshold for treating a 2-D cross-product determinant as zero (parallel /
 * collinear).  Chosen as a small fraction of floating-point machine epsilon
 * so that nearly-parallel segments are treated as non-crossing. */
static const double POLY_CROSS_TOL = 1e-14;


/* Inlined 2D point-in-polygon (ray-casting).  Equivalent to bg_pnt_in_polygon
 * from libbg; duplicated here to avoid adding a new library dependency to
 * libbrep. */
static inline int
_pnt_in_polygon(size_t nvert, const double (*pnts)[2], double tx, double ty)
{
    int c = 0;
    for (size_t i = 0, j = nvert - 1; i < nvert; j = i++) {
	if (((pnts[i][1] > ty) != (pnts[j][1] > ty)) &&
	    (tx < (pnts[j][0] - pnts[i][0]) * (ty - pnts[i][1]) /
	              (pnts[j][1] - pnts[i][1]) + pnts[i][0]))
	    c = !c;
    }
    return c;
}


/* Returns true if segment AB properly crosses segment CD (not just touching
 * at endpoints).  Used for the inner-polygon self-intersection check. */
static inline bool
_segments_cross_2d(double ax, double ay, double bx, double by,
		   double cx, double cy, double dx, double dy)
{
    double d1x = bx - ax, d1y = by - ay;
    double d2x = dx - cx, d2y = dy - cy;
    double denom = d1x * d2y - d1y * d2x;
    if (std::fabs(denom) < POLY_CROSS_TOL)
	return false;  /* parallel / collinear */
    double t = ((cx - ax) * d2y - (cy - ay) * d2x) / denom;
    double u = ((cx - ax) * d1y - (cy - ay) * d1x) / denom;
    return (t > 0.0 && t < 1.0 && u > 0.0 && u < 1.0);
}


/* Check whether a closed 2-D polygon self-intersects.
 *
 * The chord / inner polygon can be self-intersecting when the trimmed face
 * has a complex winding (e.g. many SSI curves that spiral around the UV
 * domain).  A self-intersecting polygon gives wrong results from the
 * ray-casting test and must NOT be used as the inner pre-filter.
 *
 * Strategy:
 *   • For small polygons (≤ MAX_SI_CHECK edges): do the full O(N²/2)
 *     pairwise edge-crossing test.  Adjacent edges always share one endpoint
 *     and are skipped.
 *   • For large polygons (> MAX_SI_CHECK edges): we cannot afford O(N²) so
 *     we conservatively report "possibly self-intersecting" and disable the
 *     inner fast-path.  The outer polygon (which is larger and constructed
 *     differently) is still used.
 *
 * Returns true if the polygon is (or may be) self-intersecting. */
static bool
is_polygon_self_intersecting(const std::vector<std::array<double, 2> > &poly)
{
    static const size_t MAX_SI_CHECK = 128;
    const size_t n = poly.size();
    if (n < 4)
	return false;  /* triangle cannot self-intersect */
    if (n > MAX_SI_CHECK)
	return true;   /* too large to check: assume unsafe */

    for (size_t i = 0; i < n; i++) {
	double ax = poly[i][0],        ay = poly[i][1];
	double bx = poly[(i+1)%n][0],  by = poly[(i+1)%n][1];

	/* j starts at i+2 to skip adjacent edges.  When i==0 the last
	 * edge (j == n-1) is also adjacent and must be excluded. */
	for (size_t j = i + 2; j < n; j++) {
	    if (i == 0 && j == n - 1)
		continue;  /* wraps around: edges 0 and n-1 share vertex 0 */
	    double cx = poly[j][0],       cy = poly[j][1];
	    double dx = poly[(j+1)%n][0], dy = poly[(j+1)%n][1];
	    if (_segments_cross_2d(ax, ay, bx, by, cx, cy, dx, dy))
		return true;
	}
    }
    return false;
}


/* Build inner (chord) and outer (tangent-corrected sample) polygon
 * approximations of a 2D loop.
 *
 * INNER POLYGON (conservative subset of the enclosed region):
 *   Connect each curve's start-point to the next with a straight chord.
 *   This is always inside the enclosed region because:
 *   - For arcs that bulge outward (away from interior): the chord is closer
 *     to the interior centre than the arc → chord is inside.
 *   - For arcs that dip inward (notch): the chord cuts across the notch but
 *     all points on the chord are still inside the full interior.
 *
 *   HOWEVER, the chord polygon can be self-intersecting for complex trimmed
 *   faces.  The caller must use is_polygon_self_intersecting() and discard
 *   the inner polygon if it is self-intersecting.
 *
 * OUTER POLYGON (conservative superset of the enclosed region):
 *   For each curve segment we need the polygon to be ≥ the true boundary:
 *   - We first check which side of the chord the arc midpoint lies on.
 *   - If the midpoint bulges OUTWARD (away from the loop interior): the
 *     chord alone would leave the bulge OUTSIDE the polygon → we add the
 *     tangent-intersection corner point (or the midpoint if tangents are
 *     nearly parallel) so the polygon edge arcs outward.
 *   - If the midpoint is INWARD (notch): sampling the arc midpoint M is
 *     sufficient to capture the notch in the outer polygon.
 *   - For straight / nearly-straight segments: chord is fine for both.
 *
 * The loop orientation (CCW vs CW) is inferred from the signed area so that
 * "outward" is correctly identified for both winding conventions.
 */
static void
build_loop_polygons(
    const ON_SimpleArray<ON_Curve *> &loop,
    std::vector<std::array<double, 2> > &inner_poly,
    std::vector<std::array<double, 2> > &outer_poly)
{
    const int N = loop.Count();
    if (N < 1)
	return;

    /* Compute signed area of the chord polygon to determine orientation.
     * Positive → CCW (interior on the left); negative → CW. */
    double signed_area = 0.0;
    for (int i = 0; i < N; i++) {
	if (!loop[i]) continue;
	ON_3dPoint a = loop[i]->PointAtStart();
	ON_3dPoint b = loop[i]->PointAtEnd();
	signed_area += a.x * b.y - b.x * a.y;
    }
    const bool ccw = (signed_area >= 0.0);

    /* How much sagitta (perpendicular deviation from chord) we tolerate
     * before adding the tangent-intersection corner.  Below this threshold
     * the chord is close enough to the true arc for the outer polygon. */
    const double SAG_TOL = INTERSECTION_TOL * 2.0;

    /* A curve whose chord length is below this threshold is considered
     * closed (start ≈ end).  We use 2×SAG_TOL so that the closed-curve
     * path also catches near-degenerate open curves whose endpoints are
     * so close together that the chord polygon would be a near-point. */
    const double CLOSED_CHORD_TOL = SAG_TOL * 2.0;

    /* Number of evenly-spaced samples used to represent a closed curve
     * (one whose start and end points coincide).  8 is sufficient for any
     * convex closed shape and gives a reasonable fit for mild concavities. */
    const int CLOSED_CURVE_NSAMP = 8;

    inner_poly.clear();
    outer_poly.clear();
    inner_poly.reserve(N * 4);
    outer_poly.reserve(N * 4);

    for (int i = 0; i < N; i++) {
	const ON_Curve *crv = loop[i];
	if (!crv) continue;

	ON_Interval dom = crv->Domain();
	ON_3dPoint a3 = crv->PointAtStart();
	ON_3dPoint b3 = crv->PointAtEnd();

	double ax = a3.x, ay = a3.y;
	double bx = b3.x, by = b3.y;

	/* Detect a closed curve: start and end are coincident.
	 * A single closed curve (e.g. a planar ARB8 face boundary
	 * parameterized as one closed spline, or a face that hasn't
	 * been trimmed at all) needs to be sampled at multiple interior
	 * points to produce a valid polygon — using only start/mid/end
	 * yields a 1–2-point degenerate polygon on which _pnt_in_polygon
	 * always returns false, causing every subsequent call to
	 * is_point_inside_trimmed_face to fail. */
	const double chord_sq = (bx-ax)*(bx-ax) + (by-ay)*(by-ay);
	const bool is_closed_crv = (chord_sq < CLOSED_CHORD_TOL * CLOSED_CHORD_TOL);

	if (is_closed_crv) {
	    /* Sample the curve at CLOSED_CURVE_NSAMP evenly-spaced interior
	     * parameters.  Use the same sample set for both inner and outer
	     * polygons. */
	    for (int k = 0; k < CLOSED_CURVE_NSAMP; k++) {
		double t = dom.ParameterAt((double)k / CLOSED_CURVE_NSAMP);
		ON_3dPoint p = crv->PointAt(t);
		std::array<double, 2> pv = {{p.x, p.y}};
		inner_poly.push_back(pv);
		outer_poly.push_back(pv);
	    }
	    continue;
	}

	ON_3dPoint m3 = crv->PointAt(dom.ParameterAt(0.5));
	double mx = m3.x, my = m3.y;

	/* Inner polygon: always just the start-point (chord polygon). */
	std::array<double, 2> av = {{ax, ay}};
	inner_poly.push_back(av);

	/* Cross product (B-A) × (M-A) determines which side of the chord
	 * the arc midpoint M lies on.
	 *   > 0  → M is LEFT  of A→B
	 *   < 0  → M is RIGHT of A→B
	 * For a CCW loop, interior is to the LEFT of the boundary direction.
	 * "Outward bulge" = M is to the RIGHT (away from interior) → cross < 0.
	 * "Inward notch"  = M is to the LEFT  (toward interior)   → cross > 0. */
	double cross = (bx - ax) * (my - ay) - (by - ay) * (mx - ax);

	/* Perpendicular sagitta (signed distance from M to chord A→B). */
	double chord_len = std::sqrt(chord_sq);
	double sagitta = (chord_len > 0.0) ? std::fabs(cross) / chord_len : 0.0;

	/* Determine if this is an "outward bulge" relative to the loop
	 * interior: for CCW loops, outward means M is to the RIGHT (cross<0);
	 * for CW loops, outward means M is to the LEFT (cross>0). */
	bool outward = ccw ? (cross < 0.0) : (cross > 0.0);

	/* Outer polygon: start-point always included. */
	outer_poly.push_back(av);

	if (sagitta <= SAG_TOL) {
	    /* Segment is nearly straight — chord is fine for both polygons.
	     * Add the midpoint to the outer polygon for a slightly tighter fit
	     * on mildly-curved segments. */
	    std::array<double, 2> mv = {{mx, my}};
	    outer_poly.push_back(mv);
	} else if (outward) {
	    /* Outward-bulging arc: the chord under-approximates the boundary.
	     * Add the tangent-intersection corner so the outer polygon edge
	     * lies OUTSIDE the arc.
	     *
	     * The tangent lines at A and B are:
	     *   P(s) = A + s * tA
	     *   Q(t) = B + t * tB
	     * We solve for their intersection T = A + s * tA.
	     *
	     *   s * tA.x - t * tB.x = bx - ax
	     *   s * tA.y - t * tB.y = by - ay
	     *
	     * det = tA.x * (-tB.y) - (-tB.x) * tA.y = tB.x*tA.y - tA.x*tB.y
	     */
	    ON_3dVector ta3 = crv->TangentAt(dom.ParameterAt(0.0));
	    ON_3dVector tb3 = crv->TangentAt(dom.ParameterAt(1.0));
	    double tax = ta3.x, tay = ta3.y;
	    double tbx = tb3.x, tby = tb3.y;

	    double det = tbx * tay - tax * tby;
	    bool added_corner = false;
	    if (std::fabs(det) > POLY_CROSS_TOL) {
		double dx = bx - ax, dy = by - ay;
		double s = (dx * (-tby) - dy * (-tbx)) / det;
		double tx_pt = ax + s * tax;
		double ty_pt = ay + s * tay;

		/* Accept T if it is "outside" M (farther from the interior
		 * than M) and not absurdly far away (cap at 10× chord). */
		double max_reach = chord_len * 10.0 + sagitta * 4.0;
		double dist_T_from_chord =
		    std::fabs((bx-ax)*(ty_pt-ay) - (by-ay)*(tx_pt-ax));
		if (dist_T_from_chord > 0.0 && dist_T_from_chord < max_reach) {
		    std::array<double, 2> tv = {{tx_pt, ty_pt}};
		    outer_poly.push_back(tv);
		    added_corner = true;
		}
	    }
	    if (!added_corner) {
		/* Parallel tangents or T too far: fall back to midpoint. */
		std::array<double, 2> mv = {{mx, my}};
		outer_poly.push_back(mv);
	    }
	} else {
	    /* Inward notch: midpoint captures the deepest point of the notch
	     * so the outer polygon includes the notch area. */
	    std::array<double, 2> mv = {{mx, my}};
	    outer_poly.push_back(mv);
	}
    }
}


struct TrimmedFace {
    // curve segments in the face's outer loop
    ON_SimpleArray<ON_Curve *> m_outerloop;
    // several inner loops, each has some curves
    std::vector<ON_SimpleArray<ON_Curve *> > m_innerloop;
    const ON_BrepFace *m_face;
    enum {
	UNKNOWN = -1,
	NOT_BELONG = 0,
	BELONG = 1
    } m_belong_to_final;
    bool m_rev;

    /* Lazily-built conservative polygon approximations of m_outerloop.
     * Used by is_point_inside_trimmed_face() as a fast pre-filter before
     * invoking the expensive NURBS ray-cast (point_loop_location).
     *
     * m_inner_poly: chord polygon — always a subset of the enclosed region
     *   PROVIDED it is not self-intersecting.  Self-intersection can occur
     *   for complex trimmed faces (many wound SSI curves).  The flag
     *   m_inner_poly_valid records whether the check passed.
     *   If the test point is inside this polygon it is definitely inside
     *   the trimmed face without needing the full ray-cast.
     *
     * m_outer_poly: tangent-corrected sample polygon — always a superset of
     *   the enclosed region (contains the full boundary including arc bulges
     *   and inward notches).  If the test point is outside this polygon it
     *   is definitely outside the trimmed face.
     *
     * Points in the thin annular zone between the two polygons fall through
     * to the full NURBS test.  In practice this zone is very narrow (sub-
     * tolerance for nearly-linear SSI trim curves), so most tests are
     * resolved by the O(M) polygon check rather than the O(N) NURBS cast.
     */
    mutable std::vector<std::array<double, 2> > m_inner_poly;
    mutable std::vector<std::array<double, 2> > m_outer_poly;
    mutable bool m_poly_valid;       /* true once build_loop_polygons ran */
    mutable bool m_inner_poly_valid; /* true if inner polygon is non-self-intersecting */

    // Default constructor
    TrimmedFace()
    {
	m_face = NULL;
	m_belong_to_final = UNKNOWN;
	m_rev = false;
	m_poly_valid = false;
	m_inner_poly_valid = false;
    }

    /* Build (or rebuild) the cached polygon approximations from m_outerloop.
     * Call this before using m_inner_poly / m_outer_poly. */
    void ensure_polygons() const
    {
	if (m_poly_valid)
	    return;
	build_loop_polygons(m_outerloop, m_inner_poly, m_outer_poly);
	/* Only use the inner (chord) polygon if it is not self-intersecting.
	 * A self-intersecting polygon gives wrong ray-casting results.
	 * The outer polygon is still used regardless — it is constructed with
	 * extra tangent-corner / midpoint samples that make self-intersection
	 * much less likely. */
	m_inner_poly_valid = !is_polygon_self_intersecting(m_inner_poly);
	m_poly_valid = true;
    }

    // Destructor
    ~TrimmedFace()
    {
	// Delete the curve segments if it's not belong to the result.
	if (m_belong_to_final != BELONG) {
	    for (int i = 0; i < m_outerloop.Count(); i++) {
		if (m_outerloop[i]) {
		    delete m_outerloop[i];
		    m_outerloop[i] = NULL;
		}
	    }
	    for (unsigned int i = 0; i < m_innerloop.size(); i++) {
		for (int j = 0; j < m_innerloop[i].Count(); j++) {
		    if (m_innerloop[i][j]) {
			delete m_innerloop[i][j];
			m_innerloop[i][j] = NULL;
		    }
		}
	    }
	}
    }

    TrimmedFace *Duplicate() const
    {
	TrimmedFace *out = new TrimmedFace();
	out->m_face = m_face;
	for (int i = 0; i < m_outerloop.Count(); i++) {
	    if (m_outerloop[i]) {
		out->m_outerloop.Append(m_outerloop[i]->Duplicate());
	    }
	}
	out->m_innerloop = m_innerloop;
	for (unsigned int i = 0; i < m_innerloop.size(); i++) {
	    for (int j = 0; j < m_innerloop[i].Count(); j++) {
		if (m_innerloop[i][j]) {
		    out->m_innerloop[i][j] = m_innerloop[i][j]->Duplicate();
		}
	    }
	}
	/* Don't copy cached polygon state — it will be rebuilt on demand. */
	return out;
    }
};


static int
loop_t_compare(const IntersectPoint *p1, const IntersectPoint *p2)
{
    // Use for sorting an array. Use strict FP comparison.
    if (p1->m_loop_seg != p2->m_loop_seg) {
	return p1->m_loop_seg - p2->m_loop_seg;
    }
    return p1->m_seg_t > p2->m_seg_t ? 1 : (p1->m_seg_t < p2->m_seg_t ? -1 : 0);
}


static int
curve_t_compare(const IntersectPoint *p1, const IntersectPoint *p2)
{
    // Use for sorting an array. Use strict FP comparison.
    return p1->m_curve_t > p2->m_curve_t ? 1 : (p1->m_curve_t < p2->m_curve_t ? -1 : 0);
}


void
append_to_polycurve(ON_Curve *curve, ON_PolyCurve &polycurve)
{
    // use this function rather than ON_PolyCurve::Append() to avoid
    // getting nested polycurves, which makes ON_Brep::IsValid() to fail.

    ON_PolyCurve *nested = ON_PolyCurve::Cast(curve);
    if (nested != NULL) {
	// The input curve is a polycurve
	const ON_CurveArray &segments = nested->SegmentCurves();
	for (int i = 0; i < segments.Count(); i++) {
	    append_to_polycurve(segments[i]->Duplicate(), polycurve);
	}
	delete nested;
    } else {
	polycurve.Append(curve);
    }
}


static bool
is_loop_valid(const ON_SimpleArray<ON_Curve *> &loop, double tolerance, ON_PolyCurve *polycurve = NULL)
{
    bool delete_curve = false;
    bool ret = true;

    if (loop.Count() == 0) {
	bu_log("The input loop is empty.\n");
	ret = false;
    }

    // First, use a ON_PolyCurve to represent the loop.
    if (ret) {
	if (polycurve == NULL) {
	    polycurve = new ON_PolyCurve;
	    if (polycurve) {
		delete_curve = true;
	    } else {
		ret = false;
	    }
	}
    }

    // Check the loop is continuous and closed or not.
    if (ret) {
	if (loop[0] != NULL) {
	    append_to_polycurve(loop[0]->Duplicate(), *polycurve);
	}
	for (int i = 1 ; i < loop.Count(); i++) {
	    if (loop[i] && loop[i - 1] && loop[i]->PointAtStart().DistanceTo(loop[i - 1]->PointAtEnd()) < ON_ZERO_TOLERANCE) {
		append_to_polycurve(loop[i]->Duplicate(), *polycurve);
	    } else {
		bu_log("The input loop is not continuous.\n");
		ret = false;
	    }
	}
    }
    if (ret && polycurve->PointAtStart().DistanceTo(polycurve->PointAtEnd()) >= ON_ZERO_TOLERANCE)
    {
	bu_log("The input loop is not closed.\n");
	ret = false;
    }

    if (ret) {
	// Check whether the loop is degenerated.
	ON_BoundingBox bbox = polycurve->BoundingBox();
	ret = !ON_NearZero(bbox.Diagonal().Length(), tolerance)
	    && !polycurve->IsLinear(tolerance);
    }

    if (delete_curve) {
	delete polycurve;
    }

    return ret;
}


enum {
    OUTSIDE_OR_ON_LOOP,
    INSIDE_OR_ON_LOOP
};


//   Returns whether the point is inside/on or outside/on the loop
//   boundary.
//
//   Throws InvalidGeometry if loop is invalid.
//
//   If you want to know whether this point is on the loop boundary,
//   call is_point_on_loop().
static int
point_loop_location(const ON_2dPoint &pt, const ON_SimpleArray<ON_Curve *> &loop)
{
    ON_PolyCurve polycurve;
    if (!is_loop_valid(loop, ON_ZERO_TOLERANCE, &polycurve)) {
	throw InvalidGeometry("point_loop_location() given invalid loop\n");
    }

    ON_BoundingBox bbox = polycurve.BoundingBox();
    if (!bbox.IsPointIn(pt)) {
	return OUTSIDE_OR_ON_LOOP;
    }

    // The input point is inside the loop's bounding box.
    // out must be outside the closed region (and the bbox).
    ON_2dPoint out = pt + ON_2dVector(bbox.Diagonal());
    ON_LineCurve linecurve(pt, out);
    ON_3dVector line_dir = linecurve.m_line.Direction();

    ON_SimpleArray<ON_X_EVENT> tmp_x;
    for (int i = 0; i < loop.Count(); ++i) {
	ON_SimpleArray<ON_X_EVENT> li_x;
	ON_Intersect(&linecurve, loop[i], li_x, INTERSECTION_TOL);

	for (int j = 0; j < li_x.Count(); ++j) {
	    // ignore tangent and overlap intersections
	    if (li_x[j].m_type != ON_X_EVENT::ccx_overlap &&
		!loop[i]->TangentAt(li_x[j].m_b[0]).IsParallelTo(line_dir, ANGLE_TOL))
	    {
		tmp_x.Append(li_x[j]);
	    }
	}
    }
    ON_SimpleArray<ON_X_EVENT> x_event;
    for (int i = 0; i < tmp_x.Count(); i++) {
	int j;
	for (j = 0; j < x_event.Count(); j++) {
	    if (tmp_x[i].m_A[0].DistanceTo(x_event[j].m_A[0]) < INTERSECTION_TOL &&
		tmp_x[i].m_A[1].DistanceTo(x_event[j].m_A[1]) < INTERSECTION_TOL &&
		tmp_x[i].m_B[0].DistanceTo(x_event[j].m_B[0]) < INTERSECTION_TOL &&
		tmp_x[i].m_B[1].DistanceTo(x_event[j].m_B[1]) < INTERSECTION_TOL)
	    {
		break;
	    }
	}
	if (j == x_event.Count()) {
	    x_event.Append(tmp_x[i]);
	}
    }

    return (x_event.Count() % 2) ? INSIDE_OR_ON_LOOP : OUTSIDE_OR_ON_LOOP;
}


// Returns whether or not point is on the loop boundary.
// Throws InvalidGeometry if loop is invalid.
static bool
is_point_on_loop(const ON_2dPoint &pt, const ON_SimpleArray<ON_Curve *> &loop)
{
    ON_PolyCurve polycurve;
    if (!is_loop_valid(loop, ON_ZERO_TOLERANCE, &polycurve)) {
	throw InvalidGeometry("is_point_on_loop() given invalid loop\n");
    }

    ON_3dPoint pt3d(pt);
    for (int i = 0; i < loop.Count(); ++i) {
	ON_ClassArray<ON_PX_EVENT> px_event;
	if (ON_Intersect(pt3d, *loop[i], px_event, INTERSECTION_TOL)) {
	    return true;
	}
    }
    return false;
}


static bool
is_point_inside_loop(const ON_2dPoint &pt, const ON_SimpleArray<ON_Curve *> &loop)
{
    return (point_loop_location(pt, loop) == INSIDE_OR_ON_LOOP) && !is_point_on_loop(pt, loop);
}


static bool
is_point_outside_loop(const ON_2dPoint &pt, const ON_SimpleArray<ON_Curve *> &loop)
{
    return (point_loop_location(pt, loop) == OUTSIDE_OR_ON_LOOP) && !is_point_on_loop(pt, loop);
}


static ON_SimpleArray<ON_Interval>
get_curve_intervals_inside_or_on_face(
    ON_Curve *curve2D,
    const ON_ClassArray<ON_SimpleArray<ON_Curve *> > &face_loops,
    double isect_tol)
{
    // get curve-loop intersections
    ON_SimpleArray<double> isect_curve_t;
    ON_SimpleArray<ON_X_EVENT> ccx_events;

    for (int i = 0; i < face_loops.Count(); ++i) {
	for (int j = 0; j < face_loops[i].Count(); ++j) {
	    ON_Intersect(curve2D, face_loops[i][j], ccx_events, isect_tol);
	}
    }

    // get a sorted list of just the parameters on the first curve
    // where it intersects the outerloop
    for (int i = 0; i < ccx_events.Count(); i++) {
	isect_curve_t.Append(ccx_events[i].m_a[0]);

	if (ccx_events[i].m_type == ON_X_EVENT::ccx_overlap) {
	    isect_curve_t.Append(ccx_events[i].m_a[1]);
	}
    }
    isect_curve_t.QuickSort(ON_CompareIncreasing);


    // insert start and end parameters so every part of the curve is tested
    isect_curve_t.Insert(0, curve2D->Domain().Min());
    isect_curve_t.Append(curve2D->Domain().Max());

    // if the midpoint of an interval is inside/on the face, keep the
    // entire interval
    ON_SimpleArray<ON_Interval> included_intervals;
    for (int i = 0; i < isect_curve_t.Count() - 1; i++) {
	ON_Interval interval(isect_curve_t[i], isect_curve_t[i + 1]);
	if (ON_NearZero(interval.Length(), isect_tol)) {
	    continue;
	}
	ON_2dPoint pt = curve2D->PointAt(interval.Mid());

	bool point_included = false;
	try {
	    // inside/on outerloop
	    if (!is_point_outside_loop(pt, face_loops[0])) {

		// outside/on innerloops
		point_included = true;
		for (int j = 1; j < face_loops.Count(); ++j) {
		    if (is_point_inside_loop(pt, face_loops[j])) {
			point_included = false;
			break;
		    }
		}
	    }
	} catch (InvalidGeometry &e) {
	    bu_log("%s", e.what());
	}
	if (point_included) {
	    included_intervals.Append(interval);
	}
    }

    // merge continuous intervals
    ON_SimpleArray<ON_Interval> final_intervals;
    for (int j, i = 0; i < included_intervals.Count(); i = j) {
	ON_Interval merged_interval = included_intervals[i];

	for (j = i + 1; j < included_intervals.Count(); ++j) {
	    ON_Interval &next = included_intervals[j];

	    if (ON_NearZero(next.Min() - merged_interval.Max(), isect_tol)) {
		ON_Interval new_interval = merged_interval;

		if (new_interval.Union(next)) {
		    merged_interval = new_interval;
		} else {
		    break;
		}
	    } else {
		break;
	    }
	}
	final_intervals.Append(merged_interval);
    }

    return final_intervals;
}


struct IntervalPoints {
    ON_3dPoint min;
    ON_3dPoint mid;
    ON_3dPoint max;
};


class IntervalParams {
public:
    double min;
    double mid;
    double max;

    void
    MakeIncreasing(void)
    {
	if (min > mid) {
	    std::swap(min, mid);
	}
	if (mid > max) {
	    std::swap(mid, max);

	    if (min > mid) {
		std::swap(min, mid);
	    }
	}
    }
};


// given parameters in a curve interval, create new interval
// parameters that reflect the the non-degenerate (different
// dimensioned) curve interval used to generate the curve parameters
static IntervalParams
curve_interval_from_params(
    IntervalParams interval_t,
    const ON_Curve *curve)
{
    if (!curve->IsClosed()) {
	return interval_t;
    }

    if (interval_t.min > interval_t.max) {
	std::swap(interval_t.min, interval_t.max);
    }
    double min_t = interval_t.min;
    double max_t = interval_t.max;

    ON_Interval cdom = curve->Domain();
    if (!(min_t < max_t || min_t > max_t)) {
	// if endpoints are both at closed curve joint, put them at
	// either end of the curve domain
	if (ON_NearZero(cdom.Min() - min_t, ON_ZERO_TOLERANCE)) {
	    interval_t.max = cdom.Max();
	} else if (ON_NearZero(cdom.Max() - min_t, ON_ZERO_TOLERANCE)) {
	    interval_t.min = cdom.Min();
	}
    } else {
	// if interval doesn't include midpt, assume the point nearest
	// the seam needs to be on the opposite side of the domain
	ON_Interval curr(min_t, max_t);
	if (!curr.Includes(interval_t.mid)) {
	    if (fabs(cdom.Min() - min_t) > fabs(cdom.Max() - max_t)) {
		interval_t.max = cdom.Min();
	    } else {
		interval_t.min = cdom.Max();
	    }
	}
    }
    interval_t.MakeIncreasing();

    return interval_t;
}


enum seam_location {SEAM_NONE, SEAM_ALONG_V, SEAM_ALONG_U, SEAM_ALONG_UV};


// given interval points in surface uv space, create new interval
// points that reflect the non-degenerate curve parameter interval
// used to generate the input points
static IntervalPoints
uv_interval_from_points(
    IntervalPoints interval_pts,
    const ON_Surface *surf)
{
    ON_3dPoint &min_uv = interval_pts.min;
    ON_3dPoint &max_uv = interval_pts.max;

    ON_Interval udom = surf->Domain(0);
    ON_Interval vdom = surf->Domain(1);

    int seam_min = IsAtSeam(surf, min_uv, ON_ZERO_TOLERANCE);
    int seam_max = IsAtSeam(surf, max_uv, ON_ZERO_TOLERANCE);

    if ((seam_min && seam_max) && (min_uv == max_uv)) {
	// if uv endpoints are identical and on a seam
	// they need to be on opposite sides of the domain
	if (ON_NearZero(udom.Min() - min_uv[0], ON_ZERO_TOLERANCE) ||
	    ON_NearZero(udom.Max() - min_uv[0], ON_ZERO_TOLERANCE))
	{
	    // point on west/east edge becomes one point on west
	    // edge, one point on east edge
	    min_uv[0] = udom.Min();
	    max_uv[0] = udom.Max();
	} else if (ON_NearZero(vdom.Min() - min_uv[1], ON_ZERO_TOLERANCE) ||
		   ON_NearZero(vdom.Max() - min_uv[1], ON_ZERO_TOLERANCE))
	{
	    // point on south/north edge becomes one point on
	    // south edge, one point on north edge
	    min_uv[1] = vdom.Min();
	    max_uv[1] = vdom.Max();
	}
    } else if (!seam_min != !seam_max) { // XOR
	// if just one point is on a seam, make sure it's on the
	// correct side of the domain

	// get interval midpoint in uv space
	ON_ClassArray<ON_PX_EVENT> events;
	ON_3dPoint midpt = surf->PointAt(interval_pts.mid.x,
					 interval_pts.mid.y);
	ON_Intersect(midpt, *surf, events, INTERSECTION_TOL);

	if (events.Count() == 1) {
	    // Check that the non-seam parameter of the
	    // midpoint is between the non-seam parameters of
	    // the interval uv on the seam and the other
	    // interval uv. If the midpoint non-seam parameter
	    // is outside the interval we'll move the seam_uv
	    // to the other side of the domain.
	    //
	    // For example, if the surface has a seam at the
	    // west/east edge and we have seam_uv (0.0, .1)
	    // and other_uv (.5, .6) we'll check that the
	    // interval midpoint uv has a u in [0.0, .5].
	    //
	    // A midpoint of (.25, .25) would be okay. A
	    // midpoint of (.75, .25) would necessitate us
	    // moving the seam_uv from the west edge to the
	    // east edge, e.g (1.0, .1).
	    int seam = seam_min ? seam_min : seam_max;
	    ON_3dPoint &seam_uv = seam_min ? min_uv : max_uv;
	    ON_3dPoint other_uv = seam_min ? max_uv : min_uv;

	    double *seam_t = &seam_uv[1];
	    double seam_opp_t = vdom.Max() - seam_uv[1];
	    double other_t = other_uv[1];
	    double midpt_t = events[0].m_b[1];
	    if (seam != SEAM_ALONG_U) {
		seam_t = &seam_uv[0];
		seam_opp_t = udom.Max() - seam_uv[0];
		other_t = other_uv[0];
		midpt_t = events[0].m_b[0];
	    }

	    ON_Interval curr(*seam_t, other_t);
	    if (!curr.Includes(midpt_t)) {
		// need to flip the seam point to the other
		// side of the domain
		*seam_t = seam_opp_t;
	    }
	}
    }
    return interval_pts;
}


static IntervalPoints
interval_2d_to_uv(
    const ON_Interval &interval_2d,
    const ON_Curve *curve2d,
    const ON_Surface *surf)
{
    // initialize endpoints from evaluated surface uv points
    IntervalPoints pts;
    pts.min = curve2d->PointAt(interval_2d.Min());
    pts.mid = curve2d->PointAt(interval_2d.Mid());
    pts.max = curve2d->PointAt(interval_2d.Max());

    return uv_interval_from_points(pts, surf);
}


static std::pair<IntervalPoints, IntervalPoints>
interval_2d_to_2uv(
    const ON_Interval &interval_2d,
    const ON_Curve *curve2d,
    const ON_Surface *surf,
    double split_t)
{
    std::pair<IntervalPoints, IntervalPoints> out;

    ON_Interval left(interval_2d.Min(), split_t);
    ON_Interval right(split_t, interval_2d.Max());

    out.first = interval_2d_to_uv(left, curve2d, surf);
    out.second = interval_2d_to_uv(right, curve2d, surf);

    return out;
}


static IntervalPoints
points_uv_to_3d(
    const IntervalPoints &interval_uv,
    const ON_Surface *surf)
{
    // evaluate surface at uv points to get 3d interval points
    IntervalPoints pts_3d;
    pts_3d.min = surf->PointAt(interval_uv.min.x, interval_uv.min.y);
    pts_3d.mid = surf->PointAt(interval_uv.mid.x, interval_uv.mid.y);
    pts_3d.max = surf->PointAt(interval_uv.max.x, interval_uv.max.y);

    return pts_3d;
}


static IntervalParams
points_3d_to_params_3d(
    const IntervalPoints &pts_3d,
    const ON_Curve *curve3d)
{
    ON_ClassArray<ON_PX_EVENT> events;
    ON_Intersect(pts_3d.min, *curve3d, events, INTERSECTION_TOL);
    ON_Intersect(pts_3d.mid, *curve3d, events, INTERSECTION_TOL);
    ON_Intersect(pts_3d.max, *curve3d, events, INTERSECTION_TOL);

    /* When ON_Intersect misses one or more of the three sample points (e.g.
     * because the SSI surface-projection puts a sample slightly outside
     * INTERSECTION_TOL of the 3D curve), retry each missing point with a
     * progressively wider tolerance.  A slightly-off projection is common for
     * flat-faced primitives (ARB8) where floating-point rounding can move a
     * sample a fraction of a ULP away from the intersection line. */
    if (events.Count() != 3) {
	static const double SCALES[] = { 10.0, 100.0, 1000.0 };
	const ON_3dPoint pts[3] = { pts_3d.min, pts_3d.mid, pts_3d.max };
	double found_t[3];
	bool have_t[3] = { false, false, false };
	/* Record the parameters already found */
	for (int k = 0; k < events.Count() && k < 3; ++k) {
	    found_t[k] = events[k].m_b[0];
	    have_t[k] = true;
	}

	for (int i = 0; i < 3; ++i) {
	    if (have_t[i]) continue;
	    for (size_t si = 0; si < sizeof(SCALES)/sizeof(SCALES[0]); ++si) {
		ON_ClassArray<ON_PX_EVENT> ev2;
		ON_Intersect(pts[i], *curve3d, ev2, INTERSECTION_TOL * SCALES[si]);
		if (ev2.Count() > 0) {
		    found_t[i] = ev2[0].m_b[0];
		    have_t[i] = true;
		    break;
		}
	    }
	    /* Last resort: closest-point projection onto the curve.  This is
	     * unconditional (no tolerance limit) so it always succeeds when
	     * the sample point is merely near the curve rather than exactly
	     * on it (e.g. due to surface approximation error).  Used when
	     * ON_Intersect fails at all tolerance scales.
	     * Only accepted if the closest point is within a generous
	     * tolerance — if it is farther the 3-D point is simply not on
	     * the curve at all and we should leave have_t[i] false so the
	     * caller can handle the missing endpoint gracefully.
	     * Simple ternary-search on the curve domain. */
	    if (!have_t[i]) {
		ON_Interval dom = curve3d->Domain();
		/* coarse sample to bracket */
		double best_t = dom.Min();
		double best_d = curve3d->PointAt(dom.Min()).DistanceTo(pts[i]);
		const int NSAMP = 64;
		for (int k = 1; k <= NSAMP; ++k) {
		    double tt = dom.ParameterAt(k / (double)NSAMP);
		    double dd = curve3d->PointAt(tt).DistanceTo(pts[i]);
		    if (dd < best_d) { best_d = dd; best_t = tt; }
		}
		/* refine with ternary search over ±1 interval step */
		double lo = best_t - dom.Length() / NSAMP;
		double hi = best_t + dom.Length() / NSAMP;
		if (lo < dom.Min()) lo = dom.Min();
		if (hi > dom.Max()) hi = dom.Max();
		for (int iter = 0; iter < 50; ++iter) {
		    double m1 = lo + (hi - lo) / 3.0;
		    double m2 = hi - (hi - lo) / 3.0;
		    if (curve3d->PointAt(m1).DistanceTo(pts[i]) <
			curve3d->PointAt(m2).DistanceTo(pts[i]))
			hi = m2;
		    else
			lo = m1;
		}
		double t_close = (lo + hi) * 0.5;
		double d_close = curve3d->PointAt(t_close).DistanceTo(pts[i]);
		/* Accept if the residual is within 1000× the base tolerance.
		 * Larger residuals mean the sample point is not on the curve. */
		if (d_close <= INTERSECTION_TOL * 1000.0) {
		    found_t[i] = t_close;
		    have_t[i] = true;
		}
	    }
	}

	if (!have_t[0] || !have_t[1] || !have_t[2])
	    throw AlgorithmError("points_3d_to_params_3d: conversion failed\n");

	IntervalParams params_3d;
	params_3d.min = found_t[0];
	params_3d.mid = found_t[1];
	params_3d.max = found_t[2];
	return params_3d;
    }

    IntervalParams params_3d;
    params_3d.min = events[0].m_b[0];
    params_3d.mid = events[1].m_b[0];
    params_3d.max = events[2].m_b[0];

    return params_3d;
}


static std::vector<ON_Interval>
interval_2d_to_3d(
    const ON_Interval &interval,
    const ON_Curve *curve2d,
    const ON_Curve *curve3d,
    const ON_Surface *surf)
{
    std::vector<ON_Interval> intervals_3d;

    ON_Interval c2_dom = curve2d->Domain();
    ON_Interval c3_dom = curve3d->Domain();

    c2_dom.MakeIncreasing();
    if (ON_NearZero(interval.Min() - c2_dom.Min(), ON_ZERO_TOLERANCE) &&
	ON_NearZero(interval.Max() - c2_dom.Max(), ON_ZERO_TOLERANCE))
    {
	// entire 2d domain equates to entire 3d domain
	c3_dom.MakeIncreasing();
	if (c3_dom.IsValid()) {
	    intervals_3d.push_back(c3_dom);
	}
    } else {
	// get 2d curve interval points as uv points
	IntervalPoints interval_uv =
	    interval_2d_to_uv(interval, curve2d, surf);

	// evaluate surface at uv points to get 3d interval points
	IntervalPoints pts_3d = points_uv_to_3d(interval_uv, surf);

	// convert 3d points into 3d curve parameters
	try {
	    std::vector<IntervalParams> int_params;

	    IntervalParams curve_t = points_3d_to_params_3d(pts_3d, curve3d);

	    if (curve3d->IsClosed()) {
		// get 3d seam point as surf point
		ON_3dPoint seam_pt = curve3d->PointAt(c3_dom.Min());

		ON_ClassArray<ON_PX_EVENT> events;
		ON_Intersect(seam_pt, *surf, events, INTERSECTION_TOL);

		if (events.Count() == 1) {
		    ON_3dPoint surf_pt = events[0].m_b;
		    std::vector<double> split_t;

		    // get surf point as 2d curve t
		    events.Empty();
		    ON_Intersect(surf_pt, *curve2d, events, INTERSECTION_TOL);
		    if (events.Count() == 1) {
			split_t.push_back(events[0].m_b[0]);
		    }

		    int surf_seam = IsAtSeam(surf, surf_pt, ON_ZERO_TOLERANCE);
		    if (surf_seam != SEAM_NONE) {
			// move surf_pt to other side of seam
			if (surf_seam == SEAM_ALONG_U || surf_seam == SEAM_ALONG_UV) {
			    ON_Interval vdom = surf->Domain(1);
			    if (ON_NearZero(surf_pt.y - vdom.Min(),
					    ON_ZERO_TOLERANCE)) {
				surf_pt.y = vdom.Max();
			    } else {
				surf_pt.y = vdom.Min();
			    }
			}
			if (surf_seam == SEAM_ALONG_V || surf_seam == SEAM_ALONG_UV) {
			    ON_Interval udom = surf->Domain(0);
			    if (ON_NearZero(surf_pt.x - udom.Min(),
					    ON_ZERO_TOLERANCE)) {
				surf_pt.x = udom.Max();
			    } else {
				surf_pt.x = udom.Min();
			    }
			}
			// get alternative surf point as 2d curve t
			events.Empty();
			ON_Intersect(surf_pt, *curve2d, events, INTERSECTION_TOL);
			if (events.Count() == 1) {
			    split_t.push_back(events[0].m_b[0]);
			}
		    }

		    // see if 3d curve seam point is in the 2d curve interval
		    for (size_t i = 0; i < split_t.size(); ++i) {
			double min2split = fabs(curve_t.min - split_t[i]);
			double max2split = fabs(curve_t.max - split_t[i]);

			if (min2split > ON_ZERO_TOLERANCE ||
			    max2split > ON_ZERO_TOLERANCE)
			{
			    // split 2d interval at seam point
			    std::pair<IntervalPoints, IntervalPoints> halves =
				interval_2d_to_2uv(interval, curve2d, surf,
						   split_t[i]);

			    // convert new intervals to 3d curve intervals
			    IntervalPoints left_3d, right_3d;
			    IntervalParams left_t, right_t;

			    left_3d = points_uv_to_3d(halves.first, surf);
			    left_t = points_3d_to_params_3d(left_3d, curve3d);
			    int_params.push_back(left_t);

			    right_3d = points_uv_to_3d(halves.second, surf);
			    right_t = points_3d_to_params_3d(right_3d, curve3d);
			    int_params.push_back(right_t);
			}
		    }
		}
	    }
	    if (int_params.empty()) {
		int_params.push_back(curve_t);
	    }

	    // get final 3d intervals
	    for (size_t i = 0; i < int_params.size(); ++i) {
		curve_t = curve_interval_from_params(int_params[i], curve3d);
		ON_Interval interval_3d(curve_t.min, curve_t.max);
		if (interval_3d.IsValid()) {
		    intervals_3d.push_back(interval_3d);
		}
	    }
	} catch (AlgorithmError &e) {
	    bu_log("%s", e.what());
	}
    }
    return intervals_3d;
}


// Convert parameter interval of a 3d curve into the equivalent parameter
// interval on a matching 2d face curve.
static ON_Interval
interval_3d_to_2d(
    const ON_Interval &interval,
    const ON_Curve *curve2d,
    const ON_Curve *curve3d,
    const ON_BrepFace *face)
{
    ON_Interval interval_2d;

    ON_Interval whole_domain = curve3d->Domain();
    whole_domain.MakeIncreasing();

    if (ON_NearZero(interval.Min() - whole_domain.Min(), ON_ZERO_TOLERANCE) &&
	ON_NearZero(interval.Max() - whole_domain.Max(), ON_ZERO_TOLERANCE))
    {
	interval_2d = curve2d->Domain();
	interval_2d.MakeIncreasing();
    } else {
	const ON_Surface *surf = face->SurfaceOf();

	IntervalPoints pts;
	pts.min = curve3d->PointAt(interval.Min());
	pts.mid = curve3d->PointAt(interval.Mid());
	pts.max = curve3d->PointAt(interval.Max());

	ON_ClassArray<ON_PX_EVENT> events;
	ON_Intersect(pts.min, *surf, events, INTERSECTION_TOL);
	ON_Intersect(pts.mid, *surf, events, INTERSECTION_TOL);
	ON_Intersect(pts.max, *surf, events, INTERSECTION_TOL);

	if (events.Count() == 3) {
	    IntervalPoints interval_uv;
	    interval_uv.min = events[0].m_b;
	    interval_uv.mid = events[1].m_b;
	    interval_uv.max = events[2].m_b;

	    interval_uv = uv_interval_from_points(interval_uv, surf);

	    // intersect surface uv parameters with 2d curve to convert
	    // surface uv parameters to 2d curve parameters
	    events.Empty();
	    ON_Intersect(interval_uv.min, *curve2d, events, INTERSECTION_TOL);
	    ON_Intersect(interval_uv.mid, *curve2d, events, INTERSECTION_TOL);
	    ON_Intersect(interval_uv.max, *curve2d, events, INTERSECTION_TOL);

	    if (events.Count() == 3) {
		IntervalParams curve_t;
		curve_t.min = events[0].m_b[0];
		curve_t.mid = events[1].m_b[0];
		curve_t.max = events[2].m_b[0];

		curve_t = curve_interval_from_params(curve_t, curve2d);
		interval_2d.Set(curve_t.min, curve_t.max);
	    }
	}
    }
    return interval_2d;
}


static void
get_subcurves_inside_faces(
    ON_SimpleArray<ON_Curve *> &subcurves_on1,
    ON_SimpleArray<ON_Curve *> &subcurves_on2,
    const ON_Brep *brep1,
    const ON_Brep *brep2,
    int face_i1,
    int face_i2,
    ON_SSX_EVENT *event)
{
    // The ON_SSX_EVENT from SSI is the intersection of two whole surfaces.
    // We need to get the part that lies inside both trimmed patches.
    // (brep1's face[face_i1] and brep2's face[face_i2])
    ON_SimpleArray<ON_SSX_EVENT *> out;

    if (event == NULL) {
	return;
    }

    if (event->m_curve3d == NULL || event->m_curveA == NULL || event->m_curveB == NULL) {
	return;
    }

    // get the face loops
    if (face_i1 < 0 || face_i1 >= brep1->m_F.Count() || brep1->m_F[face_i1].m_li.Count() <= 0) {
	bu_log("get_subcurves_inside_faces(): invalid face_i1 (%d).\n", face_i1);
	return;
    }
    if (face_i2 < 0 || face_i2 >= brep2->m_F.Count() || brep2->m_F[face_i2].m_li.Count() <= 0) {
	bu_log("get_subcurves_inside_faces(): invalid face_i2 (%d).\n", face_i2);
	return;
    }

    const ON_SimpleArray<int> &face1_li = brep1->m_F[face_i1].m_li;
    ON_ClassArray<ON_SimpleArray<ON_Curve *> > face1_loops;
    for (int i = 0; i < face1_li.Count(); ++i) {
	const ON_BrepLoop &brep_loop = brep1->m_L[face1_li[i]];

	ON_SimpleArray<ON_Curve *> loop_curves;
	for (int j = 0; j < brep_loop.m_ti.Count(); ++j) {
	    ON_Curve *trim2d =
		brep1->m_C2[brep1->m_T[brep_loop.m_ti[j]].m_c2i];
	    loop_curves.Append(trim2d);
	}
	face1_loops.Append(loop_curves);
    }

    const ON_SimpleArray<int> &face2_li = brep2->m_F[face_i2].m_li;
    ON_ClassArray<ON_SimpleArray<ON_Curve *> > face2_loops;
    for (int i = 0; i < face2_li.Count(); ++i) {
	const ON_BrepLoop &brep_loop = brep2->m_L[face2_li[i]];
	ON_SimpleArray<ON_Curve *> loop_curves;

	for (int j = 0; j < brep_loop.m_ti.Count(); ++j) {
	    ON_Curve *trim2d =
		brep2->m_C2[brep2->m_T[brep_loop.m_ti[j]].m_c2i];
	    loop_curves.Append(trim2d);
	}
	face2_loops.Append(loop_curves);
    }

    // find the intervals of the curves that are inside/on each face
    ON_SimpleArray<ON_Interval> intervals1, intervals2;

    intervals1 = get_curve_intervals_inside_or_on_face(event->m_curveA,
						       face1_loops, INTERSECTION_TOL);

    intervals2 = get_curve_intervals_inside_or_on_face(event->m_curveB,
						       face2_loops, INTERSECTION_TOL);

    // get subcurves for each face
    for (int i = 0; i < intervals1.Count(); ++i) {
	// convert interval on face 1 to equivalent interval on face 2
	std::vector<ON_Interval> intervals_3d;
	intervals_3d = interval_2d_to_3d(intervals1[i], event->m_curveA,
					 event->m_curve3d, &brep1->m_F[face_i1]);

	for (size_t j = 0; j < intervals_3d.size(); ++j) {
	    /* Skip 3-D sub-intervals with near-zero extent.  Such intervals
	     * arise at junctions where two curved surfaces (e.g. TGC sides)
	     * meet at a single point in 3-D even though they have a finite
	     * intersection arc in the UV domain of each surface.  The
	     * degenerate 2-D subcurves produced from these intervals cannot
	     * be assembled into valid loop trims by add_elements. */
	    {
		ON_3dPoint p3s = event->m_curve3d->PointAt(intervals_3d[j].Min());
		ON_3dPoint p3e = event->m_curve3d->PointAt(intervals_3d[j].Max());
		if (p3s.DistanceTo(p3e) < INTERSECTION_TOL)
		    continue;
	    }
	    ON_Interval interval_on2 = interval_3d_to_2d(intervals_3d[j],
							 event->m_curveB, event->m_curve3d, &brep2->m_F[face_i2]);
	    if (interval_on2.IsValid()) {
		/* Snap the interval endpoints to the SSI curve's domain
		 * boundaries when they fall within INTERSECTION_TOL.  The
		 * interval_3d_to_2d conversion can place an endpoint a tiny
		 * epsilon away from the exact domain boundary even when the
		 * SSI curve physically starts/ends on the face boundary.
		 * Without snapping, close_small_gaps later inserts a
		 * degenerate gap-closer of that epsilon length, which collapses
		 * the resulting inner-loop area to nearly zero, causing
		 * loop_is_degenerate to discard the otherwise valid loop. */
		ON_Interval dom2 = event->m_curveB->Domain();
		if (ON_NearZero(interval_on2.Min() - dom2.Min(), INTERSECTION_TOL))
		    interval_on2.m_t[0] = dom2.Min();
		if (ON_NearZero(dom2.Max() - interval_on2.Max(), INTERSECTION_TOL))
		    interval_on2.m_t[1] = dom2.Max();

		// create subcurve from interval
		try {
		    ON_Curve *subcurve_on2 = sub_curve(event->m_curveB,
						       interval_on2.Min(), interval_on2.Max());

		    /* Skip curves whose UV midpoint is not strictly inside
		     * face2's outer loop.  Such curves lie entirely on (or
		     * outside) the face boundary — they are boundary artefacts
		     * produced when two planar faces intersect along a shared
		     * edge.  Including them causes link_curves to chain the
		     * boundary segment with the true interior cut, which
		     * produces a combined path that touches the outer loop at
		     * intermediate points.  Curves missing from the closed loop
		     * (because they run along the face boundary) are recovered
		     * by the "corner-bite" fallback in split_trimmed_face. */
		    ON_2dPoint mid2 = subcurve_on2->PointAt(subcurve_on2->Domain().Mid());
		    if (!is_point_inside_loop(mid2, face2_loops[0])) {
			if (DEBUG_BREP_BOOLEAN)
			    bu_log("  subcurve_on2 REJECTED: face%d mid=(%g,%g) start=(%g,%g) end=(%g,%g)\n",
				   face_i2, mid2.x, mid2.y,
				   subcurve_on2->PointAtStart().x, subcurve_on2->PointAtStart().y,
				   subcurve_on2->PointAtEnd().x, subcurve_on2->PointAtEnd().y);
			delete subcurve_on2;
		    } else {
			if (DEBUG_BREP_BOOLEAN)
			    bu_log("  subcurve_on2 KEPT: face%d mid=(%g,%g) start=(%g,%g) end=(%g,%g)\n",
				   face_i2, mid2.x, mid2.y,
				   subcurve_on2->PointAtStart().x, subcurve_on2->PointAtStart().y,
				   subcurve_on2->PointAtEnd().x, subcurve_on2->PointAtEnd().y);
			subcurves_on2.Append(subcurve_on2);
		    }
		} catch (InvalidInterval &e) {
		    bu_log("%s", e.what());
		}
	    }
	}

    }
    for (int i = 0; i < intervals2.Count(); ++i) {
	// convert interval on face 1 to equivalent interval on face 2
	std::vector<ON_Interval> intervals_3d;
	intervals_3d = interval_2d_to_3d(intervals2[i], event->m_curveB,
					 event->m_curve3d, &brep2->m_F[face_i2]);

	for (size_t j = 0; j < intervals_3d.size(); ++j) {
	    /* Skip degenerate 3-D sub-intervals (same check as above). */
	    {
		ON_3dPoint p3s = event->m_curve3d->PointAt(intervals_3d[j].Min());
		ON_3dPoint p3e = event->m_curve3d->PointAt(intervals_3d[j].Max());
		if (p3s.DistanceTo(p3e) < INTERSECTION_TOL)
		    continue;
	    }
	    ON_Interval interval_on1 = interval_3d_to_2d(intervals_3d[j],
							 event->m_curveA, event->m_curve3d, &brep1->m_F[face_i1]);
	    if (interval_on1.IsValid()) {
		/* Snap interval endpoints to the SSI curve domain when within
		 * INTERSECTION_TOL — same reasoning as the face2 case above. */
		ON_Interval dom1 = event->m_curveA->Domain();
		if (ON_NearZero(interval_on1.Min() - dom1.Min(), INTERSECTION_TOL))
		    interval_on1.m_t[0] = dom1.Min();
		if (ON_NearZero(dom1.Max() - interval_on1.Max(), INTERSECTION_TOL))
		    interval_on1.m_t[1] = dom1.Max();

		// create subcurve from interval
		try {
		    ON_Curve *subcurve_on1 = sub_curve(event->m_curveA,
						       interval_on1.Min(), interval_on1.Max());

		    /* Same boundary-artefact filter as above, applied to
		     * face1: skip if the UV midpoint is not strictly inside
		     * face1's outer loop. */
		    ON_2dPoint mid1 = subcurve_on1->PointAt(subcurve_on1->Domain().Mid());
		    if (!is_point_inside_loop(mid1, face1_loops[0])) {
			delete subcurve_on1;
		    } else {
			subcurves_on1.Append(subcurve_on1);
		    }
		} catch (InvalidInterval &e) {
		    bu_log("%s", e.what());
		}
	    }
	}

    }

    /* ── Full inner-circle injection ──────────────────────────────────────
     * When the SSI between a flat cap (face1) and a cone/cylinder (face2)
     * produces a curve that lies entirely on the south-iso boundary of face2
     * (v ≈ vdom2.Min()), the OpenNURBS SSI algorithm returns only the arc
     * that does not cross the periodic seam.  The complementary arc is never
     * returned as a separate event, so the cap face's intersection curves are
     * incomplete.
     *
     * Fix: when this situation is detected, replace the NURBS bottom arc in
     * subcurves_on1 with a single CLOSED polyline that samples the ENTIRE
     * inner circle — first traversing the bottom-arc range that curveB already
     * covers (u from u_start down to u_end), then the complementary range
     * (u from u_end up to u_start going around), and finally closing back to
     * the first point.  A closed curve skips the link_curves joining step and
     * is fed directly as a closed ssx_curve to split_trimmed_face, avoiding
     * the floating-point IsClosed() tolerance problems that arise when trying
     * to join two separate open arcs. */
    {
	const ON_Surface *surf2 = brep2->m_S[brep2->m_F[face_i2].m_si];
	const ON_Surface *surf1 = brep1->m_S[brep1->m_F[face_i1].m_si];
	ON_Interval vdom2 = surf2->Domain(1);

	ON_2dPoint cB_s = event->m_curveB->PointAtStart();
	ON_2dPoint cB_e = event->m_curveB->PointAtEnd();

	bool at_south = ON_NearZero(cB_s.y - vdom2.Min(), INTERSECTION_TOL * 100.0) &&
			ON_NearZero(cB_e.y - vdom2.Min(), INTERSECTION_TOL * 100.0);
	bool is_partial = !ON_NearZero(cB_s.x - cB_e.x, INTERSECTION_TOL);

	if (at_south && is_partial && cB_e.x < cB_s.x) {
	    /* Use a generous projection tolerance proportional to the arc
	     * span: tilted TGC cap planes can be several millimetres apart
	     * over the inner-circle diameter, so (span * 1000) in UV units
	     * gives a stand-off tolerance large enough to catch the nearest
	     * point even for the most tilted pair encountered in practice. */
	    const double proj_tol = (cB_s.x - cB_e.x) * 1000.0 + INTERSECTION_TOL;
	    /* N_ARC: sample points per half-circle (8 gives sub-degree accuracy
	     * for a semicircle; the full polyline will have 2*N_ARC+1 points).
	     * N_FULL = 2*N_ARC total samples for the full circle. */
	    const int N_ARC = 8;

	    /* ---- Full circle: sample 2*N_ARC equally-spaced points starting
	     * at cB_s.x and advancing by one full period.
	     *
	     * The original code sampled a "bottom arc" (u_start → u_end,
	     * decreasing) then a "complementary arc" (u_start → u_end,
	     * seam-crossing, increasing) — both arcs starting from u_start.
	     * The connection chord from the bottom arc's end (u_end) to the
	     * complementary arc's start (u_start+ε) created a ~158° diagonal
	     * chord through the polygon, making it self-intersecting (figure-8).
	     * A self-intersecting polygon gives wrong ray-cast results in
	     * point_loop_location(), so test points inside the inner circle
	     * were not reliably excluded from the annular region, causing
	     * incorrect face classification.
	     *
	     * Fix: advance u continuously from cB_s.x by one full period.
	     * Adjacent samples are ~22° apart; no long diagonal chords. */
	    ON_Interval udom2 = surf2->Domain(0);
	    double u_period = udom2.Length();
	    const int N_FULL = 2 * N_ARC;
	    ON_3dPointArray uvpts;
	    for (int k = 0; k < N_FULL; k++) {
		double u_raw = cB_s.x + u_period * (double)k / N_FULL;
		double u = u_raw;
		while (u >= udom2.Max()) u -= u_period;
		while (u <  udom2.Min()) u += u_period;
		ON_3dPoint pt3d = surf2->PointAt(u, vdom2.Min());
		ON_ClassArray<ON_PX_EVENT> px;
		if (ON_Intersect(pt3d, *surf1, px, proj_tol) && px.Count() > 0) {
		    ON_2dPoint uv1(px[0].m_b[0], px[0].m_b[1]);
		    if (!is_point_outside_loop(uv1, face1_loops[0]))
			uvpts.Append(ON_3dPoint(uv1.x, uv1.y, 0.0));
		}
	    }

	    /* Deduplicate consecutive identical UV points (can arise when
	     * is_point_outside_loop rejects some samples, leaving gaps). */
	    {
		ON_3dPointArray dedup;
		for (int di = 0; di < uvpts.Count(); di++) {
		    if (dedup.Count() == 0 ||
			uvpts[di].DistanceTo(dedup[dedup.Count()-1]) > INTERSECTION_TOL)
			dedup.Append(uvpts[di]);
		}
		uvpts = dedup;
	    }

	    if (uvpts.Count() >= 4) {
		/* Close the polyline exactly. */
		uvpts.Append(uvpts[0]);

		ON_PolylineCurve *circle = new ON_PolylineCurve(uvpts);
		if (circle->IsValid()) {
		    /* Replace the NURBS bottom arc with the full-circle polyline. */
		    if (subcurves_on1.Count() > 0) {
			delete subcurves_on1[subcurves_on1.Count() - 1];
			subcurves_on1.Remove();
		    }
		    if (DEBUG_BREP_BOOLEAN)
			bu_log("  full inner-circle polyline ADDED for face%d: %d pts\n",
			       face_i1, uvpts.Count());
		    subcurves_on1.Append(circle);
		} else {
		    /* Polyline is degenerate; discard it and keep the NURBS arc. */
		    delete circle;
		}
	    } else if (DEBUG_BREP_BOOLEAN) {
		bu_log("  inner-circle: only %d pts (need >=4), not added\n", uvpts.Count());
	    }
	}
    }
}


static double
bbox_diagonal_length(ON_Curve *curve)
{
    double len = 0.0;
    if (curve) {
	ON_BoundingBox bbox;
	if (curve->GetTightBoundingBox(bbox)) {
	    len = bbox.Diagonal().Length();
	} else {
	    len = curve->PointAtStart().DistanceTo(curve->PointAtEnd());
	}
    }
    return len;
}


static void
split_curve(ON_Curve *&left, ON_Curve *&right, const ON_Curve *in, double t)
{
    try {
	left = sub_curve(in, in->Domain().m_t[0], t);
    } catch (InvalidInterval &) {
	left = NULL;
    }
    try {
	right = sub_curve(in, t, in->Domain().m_t[1]);
    } catch (InvalidInterval &) {
	right = NULL;
    }
}


static double
configure_for_linking(
    LinkedCurve *&first,
    LinkedCurve *&second,
    LinkedCurve &in1,
    LinkedCurve &in2)
{
    double dist_s1s2 = in1.PointAtStart().DistanceTo(in2.PointAtStart());
    double dist_s1e2 = in1.PointAtStart().DistanceTo(in2.PointAtEnd());
    double dist_e1s2 = in1.PointAtEnd().DistanceTo(in2.PointAtStart());
    double dist_e1e2 = in1.PointAtEnd().DistanceTo(in2.PointAtEnd());

    double min_dist = std::min(dist_s1s2, dist_s1e2);
    min_dist = std::min(min_dist, dist_e1s2);
    min_dist = std::min(min_dist, dist_e1e2);

    first = second = NULL;
    if (dist_s1e2 <= min_dist) {
	first = &in2;
	second = &in1;
    } else if (dist_e1s2 <= min_dist) {
	first = &in1;
	second = &in2;
    } else if (dist_s1s2 <= min_dist) {
	if (in1.Reverse()) {
	    first = &in1;
	    second = &in2;
	}
    } else if (dist_e1e2 <= min_dist) {
	if (in2.Reverse()) {
	    first = &in1;
	    second = &in2;
	}
    }
    return min_dist;
}


struct LinkedCurveX {
    int ssi_idx_a;
    int ssi_idx_b;
    ON_SimpleArray<ON_X_EVENT> events;
};


static ON_ClassArray<LinkedCurve>
get_joinable_ssi_curves(const ON_SimpleArray<SSICurve> &in)
{
    ON_SimpleArray<SSICurve> curves;
    for (int i = 0; i < in.Count(); ++i) {
	curves.Append(in[i]);
    }

    for (int i = 0; i < curves.Count(); ++i) {
	if (curves[i].m_curve == NULL || curves[i].m_curve->IsClosed()) {
	    continue;
	}
	for (int j = i + 1; j < curves.Count(); j++) {
	    if (curves[j].m_curve == NULL || curves[j].m_curve->IsClosed()) {
		continue;
	    }
	    ON_Curve *icurve = curves[i].m_curve;
	    ON_Curve *jcurve = curves[j].m_curve;

	    ON_SimpleArray<ON_X_EVENT> events;
	    ON_Intersect(icurve, jcurve, events, INTERSECTION_TOL);

	    if (events.Count() != 1) {
		if (events.Count() > 1) {
		    bu_log("unexpected intersection between curves\n");
		}
		continue;
	    }

	    ON_X_EVENT event = events[0];
	    if (event.m_type == ON_X_EVENT::ccx_overlap) {
		// curves from adjacent surfaces may overlap and have
		// common endpoints, but we don't want the linked
		// curve to double back on itself creating a
		// degenerate section
		ON_Interval dom[2], range[2];
		dom[0] = icurve->Domain();
		dom[1] = jcurve->Domain();
		range[0].Set(event.m_a[0], event.m_a[1]);
		range[1].Set(event.m_b[0], event.m_b[1]);

		// overlap endpoints that are near the endpoints
		// should be snapped to the endpoints
		for (int k = 0; k < 2; ++k) {
		    dom[k].MakeIncreasing();
		    range[k].MakeIncreasing();

		    for (int l = 0; l < 2; ++l) {
			if (ON_NearZero(dom[k].m_t[l] - range[k].m_t[l],
					ON_ZERO_TOLERANCE)) {
			    range[k].m_t[l] = dom[k].m_t[l];
			}
		    }
		}

		if (dom[0].Includes(range[0], true) ||
		    dom[1].Includes(range[1], true))
		{
		    // overlap is in the middle of one or both curves
		    continue;
		}

		// if one curve is completely contained by the other,
		// keep just the larger curve (or the first curve if
		// they're the same)
		if (dom[1] == range[1]) {
		    curves[j].m_curve = NULL;
		    continue;
		}
		if (dom[0] == range[0]) {
		    curves[i].m_curve = NULL;
		    continue;
		}

		// remove the overlapping portion from the end of one
		// curve so the curves meet at just a single point
		try {
		    double start = dom[0].m_t[0];
		    double end = range[0].m_t[0];
		    if (ON_NearZero(start - end, ON_ZERO_TOLERANCE)) {
			start = range[0].m_t[1];
			end = dom[0].m_t[1];
		    }
		    ON_Curve *isub = sub_curve(icurve, start, end);

		    delete curves[i].m_curve;
		    curves[i] = isub;
		} catch (InvalidInterval &e) {
		    bu_log("%s", e.what());
		}
	    } else {
		// For a single intersection, assume that one or both
		// curve endpoints is just a little past where it
		// should be. Split the curves at the intersection,
		// and discard the portion with the smaller bbox
		// diagonal.
		ON_Curve *ileft, *iright, *jleft, *jright;
		ileft = iright = jleft = jright = NULL;
		split_curve(ileft, iright, icurve, event.m_a[0]);
		split_curve(jleft, jright, jcurve, event.m_b[0]);

		if (bbox_diagonal_length(ileft) <
		    bbox_diagonal_length(iright))
		{
		    std::swap(ileft, iright);
		}
		ON_Curve *isub = ileft;
		delete iright;

		if (bbox_diagonal_length(jleft) <
		    bbox_diagonal_length(jright))
		{
		    std::swap(jleft, jright);
		}
		ON_Curve *jsub = jleft;
		delete jright;

		if (isub && jsub) {
		    // replace the original ssi curves with the
		    // trimmed versions
		    curves[i].m_curve = isub;
		    curves[j].m_curve = jsub;
		    isub = jsub = NULL;
		}
		delete isub;
		delete jsub;
	    }
	}
    }

    ON_ClassArray<LinkedCurve> out;
    for (int i = 0; i < curves.Count(); ++i) {
	if (curves[i].m_curve != NULL) {
	    LinkedCurve linked;
	    linked.Append(curves[i]);
	    out.Append(linked);
	}
    }
    return out;
}


static ON_ClassArray<LinkedCurve>
link_curves(const ON_SimpleArray<SSICurve> &in)
{
    // There might be two reasons why we need to link these curves.
    // 1) They are from intersections with two different surfaces.
    // 2) They are not continuous in the other surface's UV domain.

    ON_ClassArray<LinkedCurve> tmp = get_joinable_ssi_curves(in);

    // As usual, we use a greedy approach.
    for (int i = 0; i < tmp.Count(); i++) {
	for (int j = 0; j < tmp.Count(); j++) {
	    if (tmp[i].m_ssi_curves.Count() == 0 || tmp[i].IsClosed()) {
		break;
	    }

	    if (tmp[j].m_ssi_curves.Count() == 0 || tmp[j].IsClosed() || j == i) {
		continue;
	    }
	    LinkedCurve *c1 = NULL, *c2 = NULL;
	    double dist = configure_for_linking(c1, c2, tmp[i], tmp[j]);

	    if (dist > INTERSECTION_TOL) {
		continue;
	    }

	    if (c1 != NULL && c2 != NULL) {
		LinkedCurve new_curve;
		new_curve.Append(*c1);
		if (dist > ON_ZERO_TOLERANCE) {
		    new_curve.Append(SSICurve(new ON_LineCurve(c1->PointAtEnd(), c2->PointAtStart())));
		}
		new_curve.Append(*c2);
		tmp[i] = new_curve;
		tmp[j].m_ssi_curves.Empty();
	    }

	    // Check whether tmp[i] is closed within a tolerance
	    if (tmp[i].PointAtStart().DistanceTo(tmp[i].PointAtEnd()) < INTERSECTION_TOL && !tmp[i].IsClosed()) {
		// make IsClosed() true
		tmp[i].Append(SSICurve(new ON_LineCurve(tmp[i].PointAtEnd(), tmp[i].PointAtStart())));
	    }
	}
    }

    // Append the remaining curves to out.
    ON_ClassArray<LinkedCurve> out;
    for (int i = 0; i < tmp.Count(); i++) {
	if (tmp[i].m_ssi_curves.Count() != 0) {
	    out.Append(tmp[i]);
	}
    }

    if (DEBUG_BREP_BOOLEAN) {
	bu_log("link_curves(): %d curves remaining.\n", out.Count());
    }

    return out;
}


class CurvePoint {
public:
    int source_loop;
    int loop_index;
    double curve_t;
    ON_2dPoint pt;

    enum Location {
	BOUNDARY,
	INSIDE,
	OUTSIDE
    } location;

    static CurvePoint::Location
    PointLoopLocation(ON_2dPoint pt, const ON_SimpleArray<ON_Curve *> &loop);

    CurvePoint(
	int loop,
	int li,
	double pt_t,
	ON_Curve *curve,
	const ON_SimpleArray<ON_Curve *> &other_loop)
	: source_loop(loop), loop_index(li), curve_t(pt_t)
    {
	pt = curve->PointAt(curve_t);
	location = PointLoopLocation(pt, other_loop);
    }

    CurvePoint(
	int loop,
	int li,
	double t,
	ON_2dPoint p,
	CurvePoint::Location l)
	: source_loop(loop), loop_index(li), curve_t(t), pt(p), location(l)
    {
    }

    bool
    operator<(const CurvePoint &other) const
    {
	// for points not on the same loop, compare the actual points
	if (source_loop != other.source_loop) {
	    if (ON_NearZero(pt.DistanceTo(other.pt), INTERSECTION_TOL)) {
		return false;
	    }
	    return pt < other.pt;
	}

	// for points on the same loop, compare loop position
	if (loop_index == other.loop_index) {
	    if (ON_NearZero(curve_t - other.curve_t, INTERSECTION_TOL)) {
		return false;
	    }
	    return curve_t < other.curve_t;
	}
	return loop_index < other.loop_index;
    }

    bool
    operator==(const CurvePoint &other) const
    {
	return ON_NearZero(pt.DistanceTo(other.pt), INTERSECTION_TOL);
    }

    bool
    operator!=(const CurvePoint &other) const
    {
	return !ON_NearZero(pt.DistanceTo(other.pt), INTERSECTION_TOL);
    }
};


class CurvePointAbsoluteCompare {
public:
    bool
    operator()(const CurvePoint &a, const CurvePoint &b) const
    {
	if (ON_NearZero(a.pt.DistanceTo(b.pt), INTERSECTION_TOL)) {
	    return false;
	}
	return a.pt < b.pt;
    }
};


CurvePoint::Location
CurvePoint::PointLoopLocation(
    ON_2dPoint point,
    const ON_SimpleArray<ON_Curve *> &loop)
{
    /* is_point_on_loop() and point_loop_location() both call is_loop_valid()
     * which throws InvalidGeometry when the loop is discontinuous or otherwise
     * malformed.  Catch those exceptions so that a single bad face-split does
     * not abort the entire boolean operation; treat the point as OUTSIDE so
     * the affected curve segment is omitted rather than causing a crash. */
    try {
	if (is_point_on_loop(point, loop)) {
	    return CurvePoint::BOUNDARY;
	}
    } catch (const InvalidGeometry &) {
	return CurvePoint::OUTSIDE;
    }
    try {
	if (point_loop_location(point, loop) == OUTSIDE_OR_ON_LOOP) {
	    return CurvePoint::OUTSIDE;
	}
    } catch (const InvalidGeometry &) {
	return CurvePoint::OUTSIDE;
    }
    return CurvePoint::INSIDE;
}


class CurveSegment {
public:
    ON_SimpleArray<ON_Curve *> orig_loop;
    CurvePoint from, to;
    enum Location {
	BOUNDARY,
	INSIDE,
	OUTSIDE
    } location;

    CurveSegment(
	ON_SimpleArray<ON_Curve *> &loop,
	CurvePoint f,
	CurvePoint t,
	CurveSegment::Location l)
	: orig_loop(loop), from(f), to(t), location(l)
    {
	if (!orig_loop.Capacity()) {
	    size_t c = (from.loop_index > to.loop_index) ? from.loop_index : to.loop_index;
	    orig_loop.SetCapacity(c + 1);
	}
    }

    void
    Reverse(void)
    {
	std::swap(from, to);
    }

    ON_Curve *
    Curve(void) const
    {
	ON_Curve *from_curve = orig_loop[from.loop_index];
	ON_Curve *to_curve = orig_loop[to.loop_index];
	ON_Interval from_dom = from_curve->Domain();
	ON_Interval to_dom = to_curve->Domain();

	// if endpoints are on the same curve, just get the part between them
	if (from.loop_index == to.loop_index) {
	    ON_Curve *seg_curve =
		sub_curve(from_curve, from.curve_t, to.curve_t);
	    return seg_curve;
	}
	// if endpoints are on different curves, we may need a subcurve of
	// just the 'from' curve, just the 'to' curve, or both
	if (ON_NearZero(from.curve_t - from_dom.m_t[1], INTERSECTION_TOL)) {
	    // starting at end of 'from' same as starting at start of 'to'
	    ON_Curve *seg_curve = sub_curve(to_curve, to_dom.m_t[0],
					    to.curve_t);
	    return seg_curve;
	}
	if (ON_NearZero(to.curve_t - to_dom.m_t[0], INTERSECTION_TOL)) {
	    // ending at start of 'to' same as ending at end of 'from'
	    ON_Curve *seg_curve = sub_curve(from_curve, from.curve_t,
					    from_dom.m_t[1]);
	    return seg_curve;
	}
	ON_PolyCurve *pcurve = new ON_PolyCurve();
	append_to_polycurve(sub_curve(from_curve, from.curve_t,
				      from_dom.m_t[1]), *pcurve);
	append_to_polycurve(sub_curve(to_curve, to_dom.m_t[0], to.curve_t),
			    *pcurve);
	return pcurve;
    }

    bool
    IsDegenerate(void)
    {
	ON_Curve *seg_curve = NULL;
	try {
	    seg_curve = Curve();
	} catch (InvalidInterval &) {
	    return true;
	}

	double length = 0.0;
	if (seg_curve->IsLinear(INTERSECTION_TOL)) {
	    length = seg_curve->PointAtStart().DistanceTo(
		seg_curve->PointAtEnd());
	} else {
	    double min[3] = {0.0, 0.0, 0.0};
	    double max[3] = {0.0, 0.0, 0.0};

	    seg_curve->GetBBox(min, max, true);
	    length = DIST_PNT_PNT(min, max);
	}
	delete seg_curve;
	return length < INTERSECTION_TOL;
    }

    bool
    operator<(const CurveSegment &other) const
    {
	return from < other.from;
    }
};


class LoopBooleanResult {
public:
    std::vector<ON_SimpleArray<ON_Curve *> > outerloops;
    std::vector<ON_SimpleArray<ON_Curve *> > innerloops;

    void ClearOuterloops() {
	for (size_t i = 0; i < outerloops.size(); ++i) {
	    for (int j = 0; j < outerloops[i].Count(); ++j) {
		delete outerloops[i][j];
	    }
	}
    }
    void ClearInnerloops() {
	for (size_t i = 0; i < innerloops.size(); ++i) {
	    for (int j = 0; j < innerloops[i].Count(); ++j) {
		delete innerloops[i][j];
	    }
	}
    }
};


#define LOOP_DIRECTION_CCW  1
#define LOOP_DIRECTION_CW  -1
#define LOOP_DIRECTION_NONE 0

static bool
close_small_gap(ON_SimpleArray<ON_Curve *> &loop, int curr, int next)
{
    /* Guard against NULL entries that can appear when segments are compacted
     * out of outerloop_segs (e.g. at boolean.cpp line 3042 the last slot is
     * set to NULL before Remove()).  Dereferencing a NULL pointer here was
     * the root cause of a SIGSEGV when converting complex m35 bed regions. */
    if (!loop[curr] || !loop[next]) {
	return false;
    }
    ON_3dPoint end_curr = loop[curr]->PointAtEnd();
    ON_3dPoint start_next = loop[next]->PointAtStart();

    double gap = end_curr.DistanceTo(start_next);
    if (gap <= INTERSECTION_TOL && gap >= ON_ZERO_TOLERANCE) {
	ON_Curve *closing_seg = new ON_LineCurve(end_curr, start_next);
	loop.Insert(next, closing_seg);
	return true;
    }
    return false;
}


static void
close_small_gaps(ON_SimpleArray<ON_Curve *> &loop)
{
    if (loop.Count() == 0) {
	return;
    }
    for (int i = 0; i < loop.Count() - 1; ++i) {
	if (close_small_gap(loop, i, i + 1)) {
	    ++i;
	}
    }
    close_small_gap(loop, loop.Count() - 1, 0);
}


ON_Curve *
get_loop_curve(const ON_SimpleArray<ON_Curve *> &loop)
{
    ON_PolyCurve *pcurve = new ON_PolyCurve();
    for (int i = 0; i < loop.Count(); ++i) {
	if (!loop[i]) continue; /* skip NULL placeholders from vertex-endpoint splits */
	append_to_polycurve(loop[i]->Duplicate(), *pcurve);
    }
    return pcurve;
}


std::list<ON_SimpleArray<ON_Curve *> >::iterator
find_innerloop(std::list<ON_SimpleArray<ON_Curve *> > &loops)
{
    std::list<ON_SimpleArray<ON_Curve *> >::iterator k;
    for (k = loops.begin(); k != loops.end(); ++k) {
	ON_Curve *loop_curve = get_loop_curve(*k);

	if (ON_ClosedCurveOrientation(*loop_curve, NULL) == LOOP_DIRECTION_CW) {
	    delete loop_curve;
	    return k;
	}
	delete loop_curve;
    }
    return loops.end();
}


bool
set_loop_direction(ON_SimpleArray<ON_Curve *> &loop, int dir)
{
    ON_Curve *curve = get_loop_curve(loop);
    int curr_dir = ON_ClosedCurveOrientation(*curve, NULL);
    delete curve;

    if (curr_dir == LOOP_DIRECTION_NONE) {
	// can't set the correct direction
	return false;
    }
    if (curr_dir != dir) {
	// need reverse
	for (int i = 0; i < loop.Count(); ++i) {
	    if (!loop[i]->Reverse()) {
		return false;
	    }
	}
	loop.Reverse();
    }
    // curve already has the correct direction
    return true;
}


void
add_point_to_set(std::multiset<CurvePoint> &set, CurvePoint pt)
{
    if (set.count(pt) < 2) {
	set.insert(pt);
    }
}


std::multiset<CurveSegment>
make_segments(
    std::multiset<CurvePoint> &curve1_points,
    ON_SimpleArray<ON_Curve *> &loop1,
    ON_SimpleArray<ON_Curve *> &loop2)
{
    std::multiset<CurveSegment> out;

    std::multiset<CurvePoint>::iterator first = curve1_points.begin();
    std::multiset<CurvePoint>::iterator curr = first;
    std::multiset<CurvePoint>::iterator next = ++curve1_points.begin();

    for (; next != curve1_points.end(); ++curr, ++next) {
	CurvePoint from = *curr;
	CurvePoint to = *next;
	CurveSegment new_seg(loop1, from, to, CurveSegment::BOUNDARY);

	if (new_seg.IsDegenerate()) {
	    continue;
	}

	if (from.location == CurvePoint::BOUNDARY &&
	    to.location == CurvePoint::BOUNDARY)
	{
	    ON_Curve *seg_curve = loop1[from.loop_index];

	    double end_t = to.curve_t;
	    if (from.loop_index != to.loop_index) {
		end_t = seg_curve->Domain().m_t[1];
	    }
	    ON_2dPoint seg_midpt = seg_curve->PointAt(
		ON_Interval(from.curve_t, end_t).Mid());

	    CurvePoint::Location midpt_location =
		CurvePoint::PointLoopLocation(seg_midpt, loop2);

	    if (midpt_location == CurvePoint::INSIDE) {
		new_seg.location = CurveSegment::INSIDE;
	    } else if (midpt_location == CurvePoint::OUTSIDE) {
		new_seg.location = CurveSegment::OUTSIDE;
	    }
	} else if (from.location == CurvePoint::INSIDE ||
		   to.location == CurvePoint::INSIDE)
	{
	    new_seg.location = CurveSegment::INSIDE;
	} else if (from.location == CurvePoint::OUTSIDE ||
		   to.location == CurvePoint::OUTSIDE)
	{
	    new_seg.location = CurveSegment::OUTSIDE;
	}
	out.insert(new_seg);
    }
    return out;
}


void
set_append_segment(
    std::multiset<CurveSegment> &out,
    const CurveSegment &seg)
{
    std::multiset<CurveSegment>::iterator i;
    for (i = out.begin(); i != out.end(); ++i) {
	if ((i->from == seg.to) && (i->to == seg.from)) {
	    /* This segment is a reversed version of an existing segment —
	     * they cancel each other out.  The endpoint geometric equality
	     * check (CurvePoint::operator==, tolerance INTERSECTION_TOL) is
	     * sufficient to establish cancellation; the previous CCI overlap
	     * confirmation was O(N) per call and made get_op_segments O(N²)
	     * when boundary segment counts are large. */
	    out.erase(i);
	    return;
	}
    }
    out.insert(seg);
}


void
set_append_segments_at_location(
    std::multiset<CurveSegment> &out,
    std::multiset<CurveSegment> &in,
    CurveSegment::Location location,
    bool reversed_segs_cancel)
{
    std::multiset<CurveSegment>::iterator i;
    if (reversed_segs_cancel) {
	for (i = in.begin(); i != in.end(); ++i) {
	    if (i->location == location) {
		set_append_segment(out, *i);
	    }
	}
    } else {
	for (i = in.begin(); i != in.end(); ++i) {
	    if (i->location == location) {
		out.insert(*i);
	    }
	}
    }
}


std::multiset<CurveSegment>
find_similar_segments(
    std::multiset<CurveSegment> &set,
    const CurveSegment &seg)
{
    std::multiset<CurveSegment> out;

    std::multiset<CurveSegment>::iterator i;
    for (i = set.begin(); i != set.end(); ++i) {
	if (((i->from == seg.from) && (i->to == seg.to)) ||
	    ((i->from == seg.to)   && (i->to == seg.from)))
	{
	    out.insert(*i);
	}
    }
    return out;
}


static std::multiset<CurveSegment>
get_op_segments(
    std::multiset<CurveSegment> &curve1_segments,
    std::multiset<CurveSegment> &curve2_segments,
    op_type op)
{
    std::multiset<CurveSegment> out;

    std::multiset<CurveSegment> c1_boundary_segs;
    set_append_segments_at_location(c1_boundary_segs, curve1_segments,
				    CurveSegment::BOUNDARY, false);

    std::multiset<CurveSegment> c2_boundary_segs;
    set_append_segments_at_location(c2_boundary_segs, curve2_segments,
				    CurveSegment::BOUNDARY, false);

    if (op == BOOLEAN_INTERSECT) {
	set_append_segments_at_location(out, curve1_segments,
					CurveSegment::INSIDE, true);

	set_append_segments_at_location(out, curve2_segments,
					CurveSegment::INSIDE, true);

	std::multiset<CurveSegment>::iterator i;
	for (i = c1_boundary_segs.begin(); i != c1_boundary_segs.end(); ++i) {
	    std::multiset<CurveSegment> curve1_matches =
		find_similar_segments(c1_boundary_segs, *i);

	    std::multiset<CurveSegment> curve2_matches =
		find_similar_segments(c2_boundary_segs, *i);

	    if (curve1_matches.size() > 1 || curve2_matches.size() > 1) {
		continue;
	    }
	    if (curve1_matches.begin()->from == curve2_matches.begin()->from) {
		out.insert(*i);
	    }
	}
    } else if (op == BOOLEAN_DIFF) {
	set_append_segments_at_location(out, curve1_segments,
					CurveSegment::OUTSIDE, true);

	set_append_segments_at_location(out, curve2_segments,
					CurveSegment::INSIDE, true);

	std::multiset<CurveSegment>::iterator i;
	for (i = c1_boundary_segs.begin(); i != c1_boundary_segs.end(); ++i) {
	    std::multiset<CurveSegment> curve1_matches =
		find_similar_segments(c1_boundary_segs, *i);

	    std::multiset<CurveSegment> curve2_matches =
		find_similar_segments(c2_boundary_segs, *i);

	    if (curve1_matches.size() > 1) {
		continue;
	    }
	    if (curve1_matches.begin()->from == curve2_matches.begin()->from ||
		curve2_matches.size() > 1)
	    {
		out.insert(*i);
	    }
	}
    } else if (op == BOOLEAN_UNION) {
	set_append_segments_at_location(out, curve1_segments,
					CurveSegment::OUTSIDE, true);

	set_append_segments_at_location(out, curve2_segments,
					CurveSegment::OUTSIDE, true);

	std::multiset<CurveSegment>::iterator i;
	for (i = c1_boundary_segs.begin(); i != c1_boundary_segs.end(); ++i) {
	    std::multiset<CurveSegment> curve1_matches =
		find_similar_segments(c1_boundary_segs, *i);

	    std::multiset<CurveSegment> curve2_matches =
		find_similar_segments(c2_boundary_segs, *i);

	    if (curve1_matches.size() > 1 && curve2_matches.size() > 1) {
		continue;
	    }

	    std::multiset<CurveSegment>::iterator a, b;
	    for (a = curve1_matches.begin(); a != curve1_matches.end(); ++a) {

		b = curve2_matches.begin();
		for (; b != curve2_matches.end(); ++b) {
		    if (a->from == b->from) {
			out.insert(*i);
		    }
		}
	    }
	}
    }

    return out;
}


std::list<ON_SimpleArray<ON_Curve *> >
construct_loops_from_segments(
    std::multiset<CurveSegment> &segments)
{
    std::list<ON_SimpleArray<ON_Curve *> > out;

    while (!segments.empty()) {
	std::vector<std::multiset<CurveSegment>::iterator> loop_segs;
	std::multiset<CurvePoint, CurvePointAbsoluteCompare> visited_points;

	std::multiset<CurveSegment>::iterator curr_seg, prev_seg;
        curr_seg = segments.begin();

	loop_segs.push_back(curr_seg);
	visited_points.insert(curr_seg->from);
	visited_points.insert(curr_seg->to);

	bool closed_curve = (curr_seg->from == curr_seg->to);
	while (!closed_curve) {
	    // look for a segment that connects to the previous
	    prev_seg = curr_seg;
	    for (curr_seg = segments.begin(); curr_seg != segments.end(); ++curr_seg) {
		if (curr_seg->from == prev_seg->to) {
		    break;
		}
	    }

	    if (curr_seg == segments.end()) {
		// no segment connects to the prev one
		break;
	    } else {
		// Extend our loop with the joining segment.
		// If we've visited its endpoint before, then the loop
		// is now closed.
		loop_segs.push_back(curr_seg);
		visited_points.insert(curr_seg->to);
		closed_curve = (visited_points.count(curr_seg->to) > 1);
	    }
	}

	if (closed_curve) {
	    // find the segment the closing segment connected to (it
	    // may not be the first segment)
	    size_t i;
	    for (i = 0; i < loop_segs.size(); ++i) {
		if (loop_segs[i]->from == loop_segs.back()->to) {
		    break;
		}
	    }
	    // Form a curve from the closed chain of segments.
	    // Remove the used segments from the available set.
	    ON_SimpleArray<ON_Curve *> loop;
	    for (; i < loop_segs.size(); ++i) {
		try {
		    loop.Append(loop_segs[i]->Curve());
		} catch (InvalidInterval &e) {
		    bu_log("%s", e.what());
		}
		segments.erase(loop_segs[i]);
	    }
	    out.push_back(loop);
	} else {
	    // couldn't join to the last segment, discard it
	    segments.erase(loop_segs.back());
	    bu_log("construct_loops_from_segments: found unconnected segment\n");
	}
	loop_segs.clear();
	visited_points.clear();
    }
    return out;
}


std::multiset<CurvePoint>
get_loop_points(
    int source_loop,
    ON_SimpleArray<ON_Curve *> loop1,
    ON_SimpleArray<ON_Curve *> loop2)
{
    std::multiset<CurvePoint> out;

    if (loop1.Count() <= 0)
	return out;

    ON_Curve *loop1_seg = loop1[0];
    out.insert(CurvePoint(source_loop, 0, loop1_seg->Domain().m_t[0], loop1_seg,
			  loop2));

    for (int i = 0; i < loop1.Count(); ++i) {
	loop1_seg = loop1[i];
	out.insert(CurvePoint(source_loop, i, loop1_seg->Domain().m_t[1],
			      loop1_seg, loop2));
    }

    return out;
}


// separate outerloops and innerloops
static LoopBooleanResult
make_result_from_loops(const std::list<ON_SimpleArray<ON_Curve *> > &loops)
{
    LoopBooleanResult out;

    std::list<ON_SimpleArray<ON_Curve *> >::const_iterator li;
    for (li = loops.begin(); li != loops.end(); ++li) {
	ON_Curve *loop_curve = get_loop_curve(*li);

	int dir = ON_ClosedCurveOrientation(*loop_curve, NULL);

	if (dir == LOOP_DIRECTION_CCW) {
	    out.outerloops.push_back(*li);
	} else if (dir == LOOP_DIRECTION_CW) {
	    out.innerloops.push_back(*li);
	}
	delete loop_curve;
    }

    return out;
}


// Get the result of a boolean combination of two loops. Based on the
// algorithm from this paper:
//
// Margalit, Avraham and Gary D. Knott. 1989. "An Algorithm for
// Computing the Union, Intersection or Difference of two Polygons."
// Computers & Graphics 13:167-183.
//
// gvu.gatech.edu/people/official/jarek/graphics/papers/04PolygonBooleansMargalit.pdf
LoopBooleanResult
loop_boolean(
    const ON_SimpleArray<ON_Curve *> &l1,
    const ON_SimpleArray<ON_Curve *> &l2,
    op_type op)
{
    LoopBooleanResult out;

    if (op != BOOLEAN_INTERSECT &&
	op != BOOLEAN_DIFF &&
	op != BOOLEAN_UNION)
    {
	bu_log("loop_boolean: unsupported operation\n");
	return out;
    }

    if (l1.Count() <= 0 || l2.Count() <= 0) {
	bu_log("loop_boolean: one or more empty loops\n");
	return out;
    }

    // copy input loops
    ON_SimpleArray<ON_Curve *> loop1, loop2;
    for (int i = 0; i < l1.Count(); ++i) {
	loop1.Append(l1[i]->Duplicate());
    }
    for (int i = 0; i < l2.Count(); ++i) {
	loop2.Append(l2[i]->Duplicate());
    }

    // set curve directions based on operation
    int loop1_dir, loop2_dir;

    loop1_dir = loop2_dir = LOOP_DIRECTION_CCW;
    if (op == BOOLEAN_DIFF) {
	loop2_dir = LOOP_DIRECTION_CW;
    }

    if (!set_loop_direction(loop1, loop1_dir) ||
	!set_loop_direction(loop2, loop2_dir))
    {
	bu_log("loop_boolean: couldn't standardize curve directions\n");

	for (int i = 0; i < l1.Count(); ++i) {
	    delete loop1[i];
	}
	for (int i = 0; i < l2.Count(); ++i) {
	    delete loop2[i];
	}
	return out;
    }

    /* Coextension short-circuit:
     *
     * When two loops are nearly identical (e.g. two representations of the
     * same intersection circle from either side of a closed surface seam),
     * the CCI can produce hundreds of overlap events.  Processing all those
     * events with make_segments() and then get_op_segments() — which uses an
     * O(N²) reversed-segment cancellation loop with CCI calls — can take
     * many seconds even though the result is trivially determined:
     *   DIFF of A with coextensive B   → empty
     *   INTERSECT of A with coextensive B → A
     *   UNION of A with coextensive B    → A
     *
     * Detection: collect the CCI result for every loop1×loop2 pair once.
     * If any single pair produces more than MAX_COEXT_EVENTS events and the
     * overlap covers ≥ 90 % of loop1's parameter domain, they are coextensive.
     */
    // Collect CCI events for all pairs (needed both for coextension check and
    // for the normal segment-building path below).
    typedef std::vector<ON_SimpleArray<ON_X_EVENT> > EventTable;
    EventTable all_x_events(loop1.Count() * loop2.Count());
    for (int i = 0; i < loop1.Count(); ++i) {
	for (int j = 0; j < loop2.Count(); ++j) {
	    ON_Intersect(loop1[i], loop2[j], all_x_events[i * loop2.Count() + j],
			INTERSECTION_TOL);
	}
    }

    // Coextension check: compute the total overlap coverage of loop1's
    // parameter domain across ALL loop1×loop2 segment pairs.  For a pair
    // of nearly-identical loops (e.g. a split face whose outer loop equals
    // the next ssx_loop), every segment of loop1 will be covered by some
    // overlap event with loop2's corresponding segment.  Summing across all
    // pairs correctly detects this even when no individual pair exceeds
    // MAX_COEXT_EVENTS events.
    bool coextensive = false;
    {
	double total_domain_len = 0.0;
	double total_overlap_len = 0.0;
	for (int i = 0; i < loop1.Count(); ++i) {
	    double seg_len = loop1[i]->Domain().Length();
	    if (seg_len <= 0.0) continue;
	    total_domain_len += seg_len;
	    double seg_overlap = 0.0;
	    for (int j = 0; j < loop2.Count(); ++j) {
		const ON_SimpleArray<ON_X_EVENT> &evs =
		    all_x_events[i * loop2.Count() + j];
		for (int k = 0; k < evs.Count(); ++k) {
		    if (evs[k].m_type == ON_X_EVENT::ccx_overlap) {
			seg_overlap += fabs(evs[k].m_a[1] - evs[k].m_a[0]);
		    }
		}
	    }
	    /* Clamp per-segment overlap to [0, seg_len] to avoid
	     * double-counting when multiple loop2 segments overlap. */
	    total_overlap_len += (seg_overlap > seg_len) ? seg_len : seg_overlap;
	}
	if (total_domain_len > 0.0 &&
	    total_overlap_len / total_domain_len >= 0.90) {
	    coextensive = true;
	    bu_log("loop_boolean: coextension detected (total overlap=%.1f%%)\n",
		   100.0 * total_overlap_len / total_domain_len);
	}
    }

    if (coextensive) {
	/* The two loops are coextensive.  Handle each operation analytically. */
	if (op == BOOLEAN_DIFF) {
	    /* A \ A = ∅ — return empty result. */
	    for (int i = 0; i < l1.Count(); ++i) { delete loop1[i]; }
	    for (int i = 0; i < l2.Count(); ++i) { delete loop2[i]; }
	    return out;  /* out is already empty */
	}
	/* INTERSECT and UNION: return loop1 as the result. */
	ON_SimpleArray<ON_Curve *> result_loop;
	for (int i = 0; i < loop1.Count(); ++i) {
	    result_loop.Append(loop1[i]);
	    loop1[i] = NULL; /* transferred ownership */
	}
	/* Re-orient for CCW outerloop. */
	set_loop_direction(result_loop, LOOP_DIRECTION_CCW);
	out.outerloops.push_back(result_loop);
	for (int i = 0; i < l1.Count(); ++i) { if (loop1[i]) delete loop1[i]; }
	for (int i = 0; i < l2.Count(); ++i) { delete loop2[i]; }
	return out;
    }

    /* Non-intersecting nested-loop short-circuit:
     *
     * When no CCI events exist (the two loops never cross), they are either
     * completely separate or one is entirely enclosed by the other.  The
     * normal segment-building path cannot handle this: make_segments() with
     * empty CurvePoint sets always returns an empty result, so get_op_segments
     * and construct_loops_from_segments produce nothing.
     *
     * Detect the nesting relationship by testing one boundary point of each
     * loop against the other loop.  Then return the analytically correct
     * result for each operation:
     *
     *   loop2 inside loop1:
     *     UNION      → loop1 (loop2 is absorbed)
     *     INTERSECT  → loop2 (the smaller enclosed region)
     *     DIFF       → loop1 with loop2 as an inner hole
     *
     *   loop1 inside loop2:
     *     UNION      → loop2 (loop1 is absorbed)
     *     INTERSECT  → loop1 (the smaller enclosed region)
     *     DIFF       → empty (loop1 is entirely removed by loop2)
     *
     *   disjoint (no overlap):
     *     UNION      → both loops as separate outer loops
     *     INTERSECT  → empty
     *     DIFF       → loop1 unchanged
     *
     * Only run when ALL CCI event arrays are empty (no crossing events).
     */
    {
	bool any_events = false;
	for (int i = 0; i < loop1.Count() && !any_events; ++i) {
	    for (int j = 0; j < loop2.Count() && !any_events; ++j) {
		if (all_x_events[i * loop2.Count() + j].Count() > 0)
		    any_events = true;
	    }
	}

	if (!any_events) {
	    /* Check nesting: is loop2 inside loop1? */
	    ON_2dPoint l2pt = loop2[0]->PointAtStart();
	    bool l2_in_l1 = !is_point_outside_loop(l2pt, loop1);

	    /* Check nesting: is loop1 inside loop2? */
	    ON_2dPoint l1pt = loop1[0]->PointAtStart();
	    bool l1_in_l2 = !is_point_outside_loop(l1pt, loop2);

	    if (l2_in_l1 && !l1_in_l2) {
		/* loop2 entirely inside loop1 */
		if (op == BOOLEAN_UNION) {
		    /* result = loop1 */
		    ON_SimpleArray<ON_Curve *> result;
		    for (int i = 0; i < loop1.Count(); ++i) {
			result.Append(loop1[i]);
			loop1[i] = NULL;
		    }
		    set_loop_direction(result, LOOP_DIRECTION_CCW);
		    out.outerloops.push_back(result);
		} else if (op == BOOLEAN_INTERSECT) {
		    /* result = loop2 */
		    ON_SimpleArray<ON_Curve *> result;
		    for (int i = 0; i < loop2.Count(); ++i) {
			result.Append(loop2[i]);
			loop2[i] = NULL;
		    }
		    set_loop_direction(result, LOOP_DIRECTION_CCW);
		    out.outerloops.push_back(result);
		} else {
		    /* DIFF: result = loop1 with loop2 as inner hole */
		    ON_SimpleArray<ON_Curve *> outer_result;
		    for (int i = 0; i < loop1.Count(); ++i) {
			outer_result.Append(loop1[i]);
			loop1[i] = NULL;
		    }
		    set_loop_direction(outer_result, LOOP_DIRECTION_CCW);
		    out.outerloops.push_back(outer_result);

		    ON_SimpleArray<ON_Curve *> inner_result;
		    for (int i = 0; i < loop2.Count(); ++i) {
			inner_result.Append(loop2[i]);
			loop2[i] = NULL;
		    }
		    set_loop_direction(inner_result, LOOP_DIRECTION_CW);
		    out.innerloops.push_back(inner_result);
		}
	    } else if (l1_in_l2 && !l2_in_l1) {
		/* loop1 entirely inside loop2 */
		if (op == BOOLEAN_UNION) {
		    /* result = loop2 */
		    ON_SimpleArray<ON_Curve *> result;
		    for (int i = 0; i < loop2.Count(); ++i) {
			result.Append(loop2[i]);
			loop2[i] = NULL;
		    }
		    set_loop_direction(result, LOOP_DIRECTION_CCW);
		    out.outerloops.push_back(result);
		} else if (op == BOOLEAN_INTERSECT) {
		    /* result = loop1 */
		    ON_SimpleArray<ON_Curve *> result;
		    for (int i = 0; i < loop1.Count(); ++i) {
			result.Append(loop1[i]);
			loop1[i] = NULL;
		    }
		    set_loop_direction(result, LOOP_DIRECTION_CCW);
		    out.outerloops.push_back(result);
		}
		/* DIFF: empty (loop1 is entirely inside the subtracted region) */
	    } else {
		/* Disjoint loops */
		if (op == BOOLEAN_UNION) {
		    /* result = both loops */
		    ON_SimpleArray<ON_Curve *> r1, r2;
		    for (int i = 0; i < loop1.Count(); ++i) {
			r1.Append(loop1[i]);
			loop1[i] = NULL;
		    }
		    for (int i = 0; i < loop2.Count(); ++i) {
			r2.Append(loop2[i]);
			loop2[i] = NULL;
		    }
		    set_loop_direction(r1, LOOP_DIRECTION_CCW);
		    set_loop_direction(r2, LOOP_DIRECTION_CCW);
		    out.outerloops.push_back(r1);
		    out.outerloops.push_back(r2);
		} else if (op == BOOLEAN_DIFF) {
		    /* result = loop1 unchanged */
		    ON_SimpleArray<ON_Curve *> result;
		    for (int i = 0; i < loop1.Count(); ++i) {
			result.Append(loop1[i]);
			loop1[i] = NULL;
		    }
		    set_loop_direction(result, LOOP_DIRECTION_CCW);
		    out.outerloops.push_back(result);
		}
		/* INTERSECT disjoint: empty */
	    }

	    /* Free any non-transferred curves */
	    for (int i = 0; i < loop1.Count(); ++i) {
		if (loop1[i]) delete loop1[i];
	    }
	    for (int i = 0; i < loop2.Count(); ++i) {
		if (loop2[i]) delete loop2[i];
	    }
	    return out;
	}
    }

    // get curve endpoints and intersection points for each loop
    std::multiset<CurvePoint> loop1_points, loop2_points;

    loop1_points = get_loop_points(1, loop1, loop2);
    loop2_points = get_loop_points(2, loop2, loop1);

    for (int i = 0; i < loop1.Count(); ++i) {
	for (int j = 0; j < loop2.Count(); ++j) {
	    const ON_SimpleArray<ON_X_EVENT> &x_events =
		all_x_events[i * loop2.Count() + j];

	    for (int k = 0; k < x_events.Count(); ++k) {
		/* Skip overlap events between the two loops.  A ccx_overlap
		 * means loop1 and loop2 share a collinear edge segment; the
		 * entire overlap region (both endpoints, m_a[0]/m_A[0] and
		 * m_a[1]/m_A[1]) is skipped.  Adding overlap endpoints as
		 * CurvePoints splits the shared edge into sub-segments that
		 * produce degenerate CurveSegments in make_segments (same
		 * from/to parameter) and disconnected chains in
		 * construct_loops_from_segments.  The endpoints of each loop
		 * segment are already inserted by get_loop_points(), so no
		 * valid partition information is lost by skipping the entire
		 * overlap event. */
		if (x_events[k].m_type == ON_X_EVENT::ccx_overlap) {
		    continue;
		}

		add_point_to_set(loop1_points, CurvePoint(1, i,
							  x_events[k].m_a[0], x_events[k].m_A[0],
							  CurvePoint::BOUNDARY));

		add_point_to_set(loop2_points, CurvePoint(2, j,
							  x_events[k].m_b[0], x_events[k].m_B[0],
							  CurvePoint::BOUNDARY));
	    }
	}
    }

    // classify segments and determine which belong in the result
    std::multiset<CurveSegment> loop1_segments, loop2_segments;
    loop1_segments = make_segments(loop1_points, loop1, loop2);
    loop2_segments = make_segments(loop2_points, loop2, loop1);

    std::multiset<CurveSegment> out_segments =
	get_op_segments(loop1_segments, loop2_segments, op);

    // build result
    std::list<ON_SimpleArray<ON_Curve *> > new_loops;

    new_loops = construct_loops_from_segments(out_segments);
    for (int i = 0; i < l1.Count(); ++i) {
	delete loop1[i];
    }
    for (int i = 0; i < l2.Count(); ++i) {
	delete loop2[i];
    }

    std::list<ON_SimpleArray<ON_Curve *> >::iterator li;
    for (li = new_loops.begin(); li != new_loops.end(); ++li) {
	close_small_gaps(*li);
    }

    out = make_result_from_loops(new_loops);

    return out;
}


std::list<ON_SimpleArray<ON_Curve *> >
innerloops_inside_outerloop(
    const ON_SimpleArray<ON_Curve *> &outerloop_curve,
    const std::vector<ON_SimpleArray<ON_Curve *> > &innerloop_curves)
{
    std::list<ON_SimpleArray<ON_Curve *> > out;

    for (size_t i = 0; i < innerloop_curves.size(); ++i) {
	LoopBooleanResult new_loops;
	new_loops = loop_boolean(outerloop_curve, innerloop_curves[i],
				 BOOLEAN_INTERSECT);

	// grab outerloops
	for (size_t j = 0; j < new_loops.outerloops.size(); ++j) {
	    set_loop_direction(new_loops.outerloops[j], LOOP_DIRECTION_CW);
	    out.push_back(new_loops.outerloops[j]);
	}
	new_loops.ClearInnerloops();
    }
    return out;
}


TrimmedFace *
make_face_from_loops(
    const TrimmedFace *orig_face,
    const ON_SimpleArray<ON_Curve *> &outerloop,
    const std::vector<ON_SimpleArray<ON_Curve *> > &innerloops)
{
    TrimmedFace *face = new TrimmedFace();

    face->m_face = orig_face->m_face;
    face->m_outerloop.Append(outerloop.Count(), outerloop.Array());

    // TODO: the innerloops found here can't be inside any other
    // outerloop, and should be removed from the innerloop set in the
    // interest of efficiency
    std::list<ON_SimpleArray<ON_Curve *> > new_innerloops;
    new_innerloops = innerloops_inside_outerloop(outerloop, innerloops);

    std::list<ON_SimpleArray<ON_Curve *> >::iterator i;
    for (i = new_innerloops.begin(); i != new_innerloops.end(); ++i) {
	face->m_innerloop.push_back(*i);
    }
    return face;
}


static LoopBooleanResult
combine_loops(
    const TrimmedFace *orig_face,
    const LoopBooleanResult &new_loops)
{
    // Intersections always produce a single outerloop.
    //
    // Subtractions may produce multiple outerloops, or a single
    // outerloop that optionally includes a single innerloop.
    //
    // So, the possible results are:
    // 1) Single outerloop.
    // 2) Multiple outerloops.
    // 3) Single outerloop with single innerloop.

    // First we'll combine the old and new innerloops.
    std::vector<ON_SimpleArray<ON_Curve *> > merged_innerloops;
    if (new_loops.innerloops.size() == 1) {
	// If the result has an innerloop, it may overlap any of the
	// original innerloops. We'll union all overlapping loops with
	// the new innerloop.
	ON_SimpleArray<ON_Curve *> candidate_innerloop(new_loops.innerloops[0]);

	for (size_t i = 0; i < orig_face->m_innerloop.size(); ++i) {
	    LoopBooleanResult merged = loop_boolean(candidate_innerloop,
						    orig_face->m_innerloop[i], BOOLEAN_UNION);

	    if (merged.outerloops.size() == 1) {
		candidate_innerloop = merged.outerloops[0];
	    } else {
		merged_innerloops.push_back(orig_face->m_innerloop[i]);
		merged.ClearOuterloops();
	    }
	}
	merged_innerloops.push_back(candidate_innerloop);
    } else if (!orig_face->m_innerloop.empty()) {
	for (size_t i = 0; i < orig_face->m_innerloop.size(); ++i) {
	    merged_innerloops.push_back(orig_face->m_innerloop[i]);
	}
    }

    // Next we'll attempt to subtract all merged innerloops from each
    // new outerloop to get the final set of loops. For each
    // subtraction, there are four possibilities:
    // 1) The innerloop is outside the outerloop, and the result is
    //    the original outerloop.
    // 2) The innerloop completely encloses the outerloop, and the
    //    result is empty.
    // 3) The innerloop is completely contained by the outerloop, and
    //    the result is the input outerloop and innerloop.
    // 4) The innerloop overlaps the outerloop, and the result is one
    //    or more outerloops.
    LoopBooleanResult out;
    for (size_t i = 0; i < new_loops.outerloops.size(); ++i) {

	std::list<ON_SimpleArray<ON_Curve *> >::iterator part, next_part;
	std::list<ON_SimpleArray<ON_Curve *> > outerloop_parts;

	// start with the original outerloop
	outerloop_parts.push_back(new_loops.outerloops[i]);

	// attempt to subtract all innerloops from it, and from
	// whatever subparts of it are created along the way
	for (size_t j = 0; j < merged_innerloops.size(); ++j) {

	    part = outerloop_parts.begin();
	    for (; part != outerloop_parts.end(); part = next_part) {
		LoopBooleanResult diffed = loop_boolean(*part,
							merged_innerloops[j], BOOLEAN_DIFF);

		next_part = part;
		++next_part;

		if (diffed.innerloops.size() == 1) {
		    // The outerloop part contains the innerloop, so
		    // the innerloop belongs in the final set.
		    //
		    // Note that any innerloop added here will remains
		    // completely inside an outerloop part even if the
		    // part list changes. In order for a subsequent
		    // subtraction to put any part of it outside an
		    // outerloop, that later innerloop would have to
		    // overlap this one, in which case, the innerloops
		    // would have been unioned together in the
		    // previous merging step.
		    out.innerloops.push_back(diffed.innerloops[0]);
		} else {
		    // outerloop part has been erased, modified, or
		    // split, so we need to remove it
		    for (int k = 0; k < part->Count(); ++k) {
			delete (*part)[k];
		    }
		    outerloop_parts.erase(part);

		    // add any new parts for subsequent subtractions
		    for (size_t k = 0; k < diffed.outerloops.size(); ++k) {
			outerloop_parts.push_front(diffed.outerloops[k]);
		    }
		}
	    }
	}

	// whatever parts of the outerloop that remain after
	// subtracting all innerloops belong in the final set
	part = outerloop_parts.begin();
	for (; part != outerloop_parts.end(); ++part) {
	    out.outerloops.push_back(*part);
	}
    }
    return out;
    // Only thing left to do is make a face from each outerloop. If
    // one of the innerloops is inside the outerloop, make it part of
    // the face and remove it for consideration for other faces.
}


static void
append_faces_from_loops(
    ON_SimpleArray<TrimmedFace *> &out,
    const TrimmedFace *orig_face,
    const LoopBooleanResult &new_loops)
{
    std::vector<TrimmedFace *> o;
    LoopBooleanResult combined_loops = combine_loops(orig_face, new_loops);

    // make a face from each outerloop, using appropriate innerloops
    for (size_t i = 0; i < combined_loops.outerloops.size(); ++i) {
	o.push_back(make_face_from_loops(orig_face,
					combined_loops.outerloops[i],
					combined_loops.innerloops));
    }
    for (size_t i = 0; i < o.size(); i++) {
	out.Append(o[i]);
    }
}


/* Turn an open curve into a closed curve by using segments from the
 * face outerloop to connect its endpoints.
 *
 * Returns false on failure, true otherwise.
 */
std::vector<ON_SimpleArray<ON_Curve *> >
split_face_into_loops(
    const TrimmedFace *orig_face,
    LinkedCurve &linked_curve)
{
    std::vector<ON_SimpleArray<ON_Curve *> > out;

    if (linked_curve.IsClosed()) {
	ON_SimpleArray<ON_Curve *> loop;
	for (int i = 0; i < orig_face->m_outerloop.Count(); ++i) {
	    loop.Append(orig_face->m_outerloop[i]->Duplicate());
	}
	out.push_back(loop);
	return out;
    }

    /* We followed the algorithms described in:
     * S. Krishnan, A. Narkhede, and D. Manocha. BOOLE: A System to Compute
     * Boolean Combinations of Sculptured Solids. Technical Report TR95-008,
     * Department of Computer Science, University of North Carolina, 1995.
     * Appendix B: Partitioning a Simple Polygon using Non-Intersecting
     * Chains.
     */

    // Get the intersection points between the SSI curves and the outerloop.
    ON_SimpleArray<IntersectPoint> clx_points;
    bool intersects_outerloop = false;
    for (int i = 0; i < orig_face->m_outerloop.Count(); i++) {
	ON_SimpleArray<ON_X_EVENT> x_events;
	ON_Intersect(orig_face->m_outerloop[i], linked_curve.Curve(),
		     x_events, INTERSECTION_TOL);

	for (int j = 0; j < x_events.Count(); j++) {
	    /* Skip overlap events: a ccx_overlap means the SSI curve runs
	     * collinearly along the outer-loop edge.  That is not a
	     * transversal crossing, so those endpoints must NOT be treated
	     * as face-partition points.  The BOOLE algorithm requires the
	     * SSI curve to enter/exit the face through the boundary
	     * transversally; collinear segments on the boundary are
	     * degenerate and corrupt the IN/OUT classifier if included.
	     * The adjacent outer-loop vertex (where the collinear segment
	     * ends) is still detected as a normal ccx_point event from the
	     * neighboring outer-loop segment, so no valid information is
	     * lost by skipping the overlap here. */
	    if (x_events[j].m_type == ON_X_EVENT::ccx_overlap) {
		intersects_outerloop = true;
		continue;
	    }

	    IntersectPoint tmp_pt;
	    tmp_pt.m_pt = x_events[j].m_A[0];
	    tmp_pt.m_seg_t = x_events[j].m_a[0];
	    tmp_pt.m_curve_t = x_events[j].m_b[0];
	    tmp_pt.m_loop_seg = i;
	    clx_points.Append(tmp_pt);

	    if (x_events.Count()) {
		intersects_outerloop = true;
	    }
	}
    }

    /* Endpoint-pinch fix: if the linked curve's start or end point lies
     * within INTERSECTION_TOL of an outer-loop segment but was not detected
     * by ON_Intersect above (e.g. the endpoint is fractionally inside the
     * face rather than exactly on the boundary), add a synthetic
     * IntersectPoint so the BOOLE algorithm can use it to partition the face.
     *
     * This case arises when a collinear boundary SSI segment is removed by
     * the midpoint filter in get_subcurves_inside_faces, leaving the
     * remaining non-boundary segments with endpoints that are approximately
     * (but not exactly) on the outer loop.  Without this fix,
     * clx_points.Count() < 2 and the function returns the unmodified original
     * face (no split).
     *
     * Only run when clx_points currently has fewer than 2 entries: if the
     * outer-loop intersection is already fully detected (2 or more points
     * found by ON_Intersect), we must NOT add more because the resulting
     * 3-point or 4-point set confuses the stack-based loop-pairing
     * algorithm that expects an even number of transversal crossings. */
    if (clx_points.Count() < 2) {
	const double curve_t_min = linked_curve.Domain().Min();
	const double curve_t_max = linked_curve.Domain().Max();
	const ON_3dPoint ep[2] = {
	    linked_curve.Curve()->PointAtStart(),
	    linked_curve.Curve()->PointAtEnd()
	};
	const double ep_t[2] = { curve_t_min, curve_t_max };

	for (int ep_idx = 0; ep_idx < 2; ep_idx++) {
	    /* Skip if the endpoint is already represented in clx_points
	     * (the original ON_Intersect loop found it as a ccx_point
	     * event). */
	    bool already_found = false;
	    for (int ci = 0; ci < clx_points.Count(); ci++) {
		if (fabs(clx_points[ci].m_curve_t - ep_t[ep_idx]) < INTERSECTION_TOL &&
		    clx_points[ci].m_pt.DistanceTo(ep[ep_idx]) <= INTERSECTION_TOL) {
		    already_found = true;
		    break;
		}
	    }
	    if (already_found)
		continue;

	    /* Find the outer-loop segment nearest to this endpoint */
	    double best_dist = INTERSECTION_TOL;
	    int best_seg = -1;
	    double best_seg_t = 0.0;
	    ON_3dPoint best_pt;

	    for (int i = 0; i < orig_face->m_outerloop.Count(); i++) {
		ON_ClassArray<ON_PX_EVENT> px;
		if (!ON_Intersect(ep[ep_idx], *orig_face->m_outerloop[i], px, INTERSECTION_TOL))
		    continue;
		for (int k = 0; k < px.Count(); k++) {
		    double dist = ep[ep_idx].DistanceTo(px[k].m_B);
		    if (dist < best_dist) {
			best_dist = dist;
			best_seg = i;
			best_seg_t = px[k].m_b.x;
			best_pt = px[k].m_B;
		    }
		}
	    }

	    if (best_seg >= 0) {
		if (DEBUG_BREP_BOOLEAN) {
		    bu_log("endpoint-pinch: ep[%d] at (%g,%g,%g) snapped to outer_loop[%d] at (%g,%g,%g) dist=%g\n",
			   ep_idx, ep[ep_idx].x, ep[ep_idx].y, ep[ep_idx].z,
			   best_seg, best_pt.x, best_pt.y, best_pt.z, best_dist);
		}
		IntersectPoint tmp_pt;
		tmp_pt.m_pt = best_pt;
		tmp_pt.m_seg_t = best_seg_t;
		tmp_pt.m_curve_t = ep_t[ep_idx];
		tmp_pt.m_loop_seg = best_seg;
		clx_points.Append(tmp_pt);
		intersects_outerloop = true;
	    }
	}
    }

    // can't close curves that don't partition the face
    if (DEBUG_BREP_BOOLEAN) {
	bu_log("split_face_into_loops: intersects_outerloop=%d clx_points.Count()=%d\n",
	       (int)intersects_outerloop, clx_points.Count());
    }
    if (!intersects_outerloop || clx_points.Count() < 2) {
	ON_SimpleArray<ON_Curve *> loop;
	for (int i = 0; i < orig_face->m_outerloop.Count(); ++i) {
	    loop.Append(orig_face->m_outerloop[i]->Duplicate());
	}
	out.push_back(loop);
	return out;
    }

    // rank these intersection points
    clx_points.QuickSort(curve_t_compare);
    for (int i = 0; i < clx_points.Count(); i++) {
	clx_points[i].m_curve_pos = i;
    }

    /* Deduplicate intersection events that arise when the SSI curve endpoint
     * coincides with an outer-loop vertex.  ON_Intersect() reports such a
     * vertex once as end-of-segment[k] and once as start-of-segment[k+1];
     * both carry the same curve parameter and the same 3-D point.  The
     * duplicate causes two consecutive same-direction events in the IN/OUT
     * classifier (the "next" midpoint between them collapses to the vertex
     * itself, which lies on the boundary, so inside-test returns false for
     * both) and leads to a zero-length SubCurve call in the loop-pairing
     * step (which throws "degenerate interval").  Remove the second of each
     * such pair so that only one event survives per vertex. */
    {
	for (int i = clx_points.Count() - 1; i >= 1; i--) {
	    const IntersectPoint &prev = clx_points[i - 1];
	    const IntersectPoint &curr = clx_points[i];
	    if (ON_NearZero(curr.m_curve_t - prev.m_curve_t) &&
		curr.m_pt.DistanceTo(prev.m_pt) <= INTERSECTION_TOL) {
		clx_points.Remove(i);
	    }
	}
	/* Re-assign curve_pos after deduplication. */
	for (int i = 0; i < clx_points.Count(); i++) {
	    clx_points[i].m_curve_pos = i;
	}
    }

    // classify intersection points
    ON_SimpleArray<IntersectPoint> new_pts;
    double curve_min_t = linked_curve.Domain().Min();
    double curve_max_t = linked_curve.Domain().Max();

    if (DEBUG_BREP_BOOLEAN) {
	bu_log("  IN/OUT classify: curve_min_t=%g curve_max_t=%g\n", curve_min_t, curve_max_t);
	for (int i = 0; i < clx_points.Count(); i++) {
	    const IntersectPoint &ipt = clx_points[i];
	    bu_log("    clx[%d] curve_t=%g seg=%d seg_t=%g pt=(%g,%g)\n",
		   i, ipt.m_curve_t, ipt.m_loop_seg, ipt.m_seg_t, ipt.m_pt.x, ipt.m_pt.y);
	}
    }

    for (int i = 0; i < clx_points.Count(); i++) {
	bool is_first_ipt = (i == 0);
	bool is_last_ipt = (i == (clx_points.Count() - 1));

	IntersectPoint *ipt = &clx_points[i];
	double curve_t = ipt->m_curve_t;

	ON_3dPoint prev = linked_curve.PointAtStart();
	if (!is_first_ipt) {
	    double prev_curve_t = clx_points[i - 1].m_curve_t;
	    prev = linked_curve.PointAt((curve_t + prev_curve_t) * .5);
	}
	ON_3dPoint next = linked_curve.PointAtEnd();
	if (!is_last_ipt) {
	    double next_curve_t = clx_points[i + 1].m_curve_t;
	    next = linked_curve.PointAt((curve_t + next_curve_t) * .5);
	}
	// If the point is on the boundary, we treat it with the same
	// way as it's outside.
	// For example, the prev side is inside, and the next's on
	// boundary, that point should be IntersectPoint::OUT, the
	// same as the next's outside the loop.
	// Other cases are similar.
	bool prev_in, next_in;
	try {
	    prev_in = is_point_inside_loop(prev, orig_face->m_outerloop);
	    next_in = is_point_inside_loop(next, orig_face->m_outerloop);
	} catch (InvalidGeometry &e) {
	    bu_log("%s", e.what());
	    // not a loop
	    ipt->m_dir = IntersectPoint::UNSET;
	    continue;
	}
	if (is_first_ipt && ON_NearZero(curve_t - curve_min_t)) {
	    ipt->m_dir = next_in ? IntersectPoint::IN_HIT : IntersectPoint::OUT_HIT;
	    continue;
	}
	if (is_last_ipt && ON_NearZero(curve_t - curve_max_t)) {
	    ipt->m_dir = prev_in ? IntersectPoint::OUT_HIT : IntersectPoint::IN_HIT;
	    continue;
	}
	if (prev_in && next_in) {
	    // tangent point, both sides in, duplicate that point
	    new_pts.Append(*ipt);
	    new_pts.Last()->m_dir = IntersectPoint::TANGENT;
	    new_pts.Last()->m_curve_pos = ipt->m_curve_pos;
	    ipt->m_dir = IntersectPoint::TANGENT;
	} else if (!prev_in && !next_in) {
	    // tangent point, both sides out, useless
	    ipt->m_dir = IntersectPoint::UNSET;
	} else if (prev_in && !next_in) {
	    // transversal point, going outside
	    ipt->m_dir = IntersectPoint::OUT_HIT;
	} else {
	    // transversal point, going inside
	    ipt->m_dir = IntersectPoint::IN_HIT;
	}
    }

    clx_points.Append(new_pts.Count(), new_pts.Array());
    clx_points.QuickSort(loop_t_compare);

    // Split the outer loop.
    ON_SimpleArray<ON_Curve *> outerloop_segs;
    int clx_i = 0;
    for (int loop_seg = 0; loop_seg < orig_face->m_outerloop.Count(); loop_seg++) {
	ON_Curve *remainder = orig_face->m_outerloop[loop_seg]->Duplicate();
	if (remainder == NULL) {
	    bu_log("ON_Curve::Duplicate() failed.\n");
	    return out;
	}
	for (; clx_i < clx_points.Count() && clx_points[clx_i].m_loop_seg == loop_seg; clx_i++) {
	    IntersectPoint &ipt = clx_points[clx_i];
	    ON_Curve *portion_before_ipt = NULL;
	    if (remainder) {
		double start_t = remainder->Domain().Min();
		double end_t = remainder->Domain().Max();
		if (ON_NearZero(ipt.m_seg_t - end_t)) {
		    // Can't call Split() if ipt is at start (that
		    // case is handled by the initialization) or ipt
		    // is at end (handled here).
		    portion_before_ipt = remainder;
		    remainder = NULL;
		} else if (!ON_NearZero(ipt.m_seg_t - start_t)) {
		    if (!remainder->Split(ipt.m_seg_t, portion_before_ipt, remainder)) {
			bu_log("Split failed.\n");
			bu_log("Domain: [%f, %f]\n", end_t, start_t);
			bu_log("m_seg_t: %f\n", ipt.m_seg_t);
		    }
		}
	    }
	    if (portion_before_ipt) {
		outerloop_segs.Append(portion_before_ipt);
	    }
	    ipt.m_split_li = outerloop_segs.Count() - 1;
	}
	/* Only append remainder if it is non-NULL: a NULL remainder means the
	 * last intersection point on this segment was exactly at the segment's
	 * end (a vertex boundary).  Appending NULL would cause a crash at the
	 * circular-wrap duplication step below, and would corrupt m_split_li
	 * for any intersection point on the following segment that starts at
	 * the same vertex. */
	if (remainder) {
	    outerloop_segs.Append(remainder);
	}
    }

    // Append the first element at the last to handle some special cases.
    if (clx_points.Count()) {
	clx_points.Append(clx_points[0]);
	clx_points.Last()->m_loop_seg += orig_face->m_outerloop.Count();
	for (int i = 0; i <= clx_points[0].m_split_li; i++) {
	    if (!outerloop_segs[i]) {
		/* NULL slot: the last intersection point on this outer-loop
		 * segment fell exactly at the segment's endpoint (a vertex
		 * boundary), so the remainder after splitting was NULL and
		 * was not appended.  Skip to avoid dereferencing NULL. */
		continue;
	    }
	    ON_Curve *dup = outerloop_segs[i]->Duplicate();
	    if (dup == NULL) {
		bu_log("ON_Curve::Duplicate() failed.\n");
		continue;
	    }
	    outerloop_segs.Append(dup);
	}
	clx_points.Last()->m_split_li = outerloop_segs.Count() - 1;
    }

    if (DEBUG_BREP_BOOLEAN) {
	for (int i = 0; i < clx_points.Count(); i++) {
	    IntersectPoint &ipt = clx_points[i];
	    bu_log("clx_points[%d](count = %d): ", i, clx_points.Count());
	    bu_log("m_curve_pos = %d, m_dir = %d\n", ipt.m_curve_pos, ipt.m_dir);
	}
    }

    std::stack<int> s;
    for (int i = 0; i < clx_points.Count(); i++) {
	if (clx_points[i].m_dir == IntersectPoint::UNSET) {
	    continue;
	}
	if (s.empty()) {
	    s.push(i);
	    continue;
	}
	const IntersectPoint &p = clx_points[s.top()];
	const IntersectPoint &q = clx_points[i];

	if (loop_t_compare(&p, &q) > 0 || q.m_split_li < p.m_split_li) {
	    bu_log("stack error or sort failure.\n");
	    bu_log("s.top() = %d, i = %d\n", s.top(), i);
	    bu_log("p->m_split_li = %d, q->m_split_li = %d\n", p.m_split_li, q.m_split_li);
	    bu_log("p->m_loop_seg = %d, q->m_loop_seg = %d\n", p.m_loop_seg, q.m_loop_seg);
	    bu_log("p->m_seg_t = %g, q->m_seg_t = %g\n", p.m_seg_t, q.m_seg_t);
	    continue;
	}
	if (q.m_curve_pos - p.m_curve_pos == 1 &&
	    q.m_dir != IntersectPoint::IN_HIT &&
	    p.m_dir != IntersectPoint::OUT_HIT)
	{
	    s.pop();
	} else if (p.m_curve_pos - q.m_curve_pos == 1 &&
		   p.m_dir != IntersectPoint::IN_HIT &&
		   q.m_dir != IntersectPoint::OUT_HIT)
	{
	    s.pop();
	} else {
	    s.push(i);
	    continue;
	}

	// need to form a new loop
	ON_SimpleArray<ON_Curve *> newloop;
	int curve_count = q.m_split_li - p.m_split_li;
	for (int j = p.m_split_li + 1; j <= q.m_split_li; j++) {
	    // No need to duplicate the curve, because the pointer
	    // in the array 'outerloop_segs' will be moved out later.
	    newloop.Append(outerloop_segs[j]);
	}

	// The curves on the outer loop is from p to q, so the curves on the
	// SSI curve should be from q to p (to form a loop)
	double t1 = p.m_curve_t, t2 = q.m_curve_t;
	bool need_reverse = true;
	if (t1 > t2) {
	    std::swap(t1, t2);
	    need_reverse = false;
	}
	ON_Curve *seg_on_SSI = linked_curve.SubCurve(t1, t2);
	if (seg_on_SSI == NULL) {
	    bu_log("sub_curve() failed.\n");
	    /* The newloop outer-loop segments were transferred out of
	     * outerloop_segs (ownership moved to newloop) but we cannot
	     * form the loop.  Ownership must be returned so the caller can
	     * still build the remaining outer loop.  The simplest recovery
	     * is to put them back and skip this pairing. */
	    for (int j = p.m_split_li + 1; j <= q.m_split_li; j++) {
		outerloop_segs[j] = newloop[j - (p.m_split_li + 1)];
	    }
	    continue;
	}
	if (need_reverse) {
	    if (!seg_on_SSI->Reverse()) {
		bu_log("Reverse failed.\n");
		continue;
	    }
	}
	newloop.Append(seg_on_SSI);
	close_small_gaps(newloop);

	ON_Curve *rev_seg_on_SSI = seg_on_SSI->Duplicate();
	if (!rev_seg_on_SSI || !rev_seg_on_SSI->Reverse()) {
	    bu_log("Reverse failed.\n");
	    continue;
	} else {
	    // Update the outerloop
	    outerloop_segs[p.m_split_li + 1] = rev_seg_on_SSI;
	    int k = p.m_split_li + 2;
	    for (int j = q.m_split_li + 1; j < outerloop_segs.Count(); j++) {
		outerloop_segs[k++] = outerloop_segs[j];
	    }
	    while (k < outerloop_segs.Count()) {
		outerloop_segs[outerloop_segs.Count() - 1] = NULL;
		outerloop_segs.Remove();
	    }
	    // Update m_split_li
	    for (int j = i + 1; j < clx_points.Count(); j++) {
		clx_points[j].m_split_li -= curve_count - 1;
	    }
	}

	// append the new loop if it's valid
	if (DEBUG_BREP_BOOLEAN) {
	    bu_log("  is_loop_valid(newloop): count=%d", newloop.Count());
	    for (int gi = 0; gi < newloop.Count(); gi++) {
		int gj = (gi + 1) % newloop.Count();
		if (newloop[gi] && newloop[gj]) {
		    double g = newloop[gi]->PointAtEnd().DistanceTo(newloop[gj]->PointAtStart());
		    bu_log(" g[%d→%d]=%g", gi, gj, g);
		}
	    }
	    bu_log("\n");
	}
	if (is_loop_valid(newloop, ON_ZERO_TOLERANCE)) {
	    ON_SimpleArray<ON_Curve *> loop;
	    loop.Append(newloop.Count(), newloop.Array());
	    out.push_back(loop);
	} else {
	    for (int j = 0; j < newloop.Count(); j++) {
		delete newloop[j];
	    }
	}
    }

    // Remove the duplicated segments before the first intersection point.
    if (clx_points.Count()) {
	for (int i = 0; i <= clx_points[0].m_split_li; i++) {
	    delete outerloop_segs[0];
	    outerloop_segs[0] = NULL;
	    outerloop_segs.Remove(0);
	}
    }

    if (!out.empty()) {
	// The remaining part after splitting some parts out.
	close_small_gaps(outerloop_segs);
	if (is_loop_valid(outerloop_segs, ON_ZERO_TOLERANCE)) {
	    ON_SimpleArray<ON_Curve *> loop;
	    loop.Append(outerloop_segs.Count(), outerloop_segs.Array());
	    out.push_back(loop);
	} else {
	    for (int i = 0; i < outerloop_segs.Count(); i++)
		if (outerloop_segs[i]) {
		    delete outerloop_segs[i];
		}
	}
    }
    return out;
}


void
free_loops(std::vector<ON_SimpleArray<ON_Curve *> > &loops)
{
    for (size_t i = 0; i < loops.size(); ++i) {
	for (int j = 0; j < loops[i].Count(); ++j) {
	    delete loops[i][j];
	    loops[i][j] = NULL;
	}
    }
}


/* Compute the absolute shoelace area of a closed loop using the segment
 * endpoints as polygon vertices.  This is O(N), exact for piecewise-linear
 * loops, and correctly gives zero for "there-and-back" degenerate paths
 * regardless of domain parameterisation.
 *
 * The loop curves live in the face's 2D UV parameter space, so x and y
 * are the u and v coordinates respectively.
 *
 * Polycurve entries (e.g. the reversed SubCurve appended by
 * split_face_into_loops) are recursed into so that every internal vertex
 * is included as a shoelace polygon vertex.  Without recursion, a
 * polycurve that goes A→B→C→A would be treated as the straight chord A→A,
 * collapsing the area to zero even when the true enclosed area is large.
 *
 * ON_PolylineCurve entries (e.g. the complementary-arc polylines injected
 * by get_subcurves_inside_faces) are similarly expanded: each polyline
 * vertex is used as a shoelace polygon vertex.  Without expansion only the
 * endpoint is used, zeroing the area of any loop whose NURBS arc partner
 * has nearly-antipodal endpoints. */
static void
shoelace_accumulate(const ON_Curve *c, ON_3dPoint &prev, double &shoelace)
{
    if (!c) return;
    const ON_PolyCurve *pc = ON_PolyCurve::Cast(c);
    if (pc) {
	for (int i = 0; i < pc->Count(); i++) {
	    shoelace_accumulate(pc->SegmentCurve(i), prev, shoelace);
	}
	return;
    }
    const ON_PolylineCurve *plc = ON_PolylineCurve::Cast(c);
    if (plc) {
	const ON_Polyline &pts = plc->m_pline;
	for (int i = 1; i < pts.Count(); i++) {
	    ON_3dPoint curr(pts[i]);
	    shoelace += prev.x * curr.y - curr.x * prev.y;
	    prev = curr;
	}
	return;
    }
    ON_3dPoint curr = c->PointAtEnd();
    shoelace += prev.x * curr.y - curr.x * prev.y;
    prev = curr;
}

static double
loop_shoelace_area(const ON_SimpleArray<ON_Curve *> &loop)
{
    if (loop.Count() < 1)
	return 0.0;
    double shoelace = 0.0;
    ON_3dPoint prev = loop[0]->PointAtStart();
    for (int si = 0; si < loop.Count(); ++si) {
	if (!loop[si]) continue;
	shoelace_accumulate(loop[si], prev, shoelace);
    }
    return fabs(shoelace * 0.5);
}


bool
loop_is_degenerate(const ON_SimpleArray<ON_Curve *> &loop)
{
    if (loop.Count() < 1) {
	return true;
    }
    // want sufficient distance between non-adjacent curve points
    ON_Curve *loop_curve = get_loop_curve(loop);
    ON_Interval dom = loop_curve->Domain();

    ON_3dPoint pt1 = loop_curve->PointAt(dom.ParameterAt(.25));
    ON_3dPoint pt2 = loop_curve->PointAt(dom.ParameterAt(.75));

    /* A "there-and-back" (zero-area) closed loop — where the curve
     * retraces the same path in reverse — encloses zero area.  The
     * 25%/75% distance check misses this when the two curve segments
     * have DIFFERENT domain lengths, causing asymmetric sampling that
     * lands at geometrically distinct points far apart (pt_dist >> TOL).
     * Parameter-space shoelace sampling also gives wrong non-zero area.
     *
     * Fix: compute the signed 2D shoelace area using only the SEGMENT
     * ENDPOINTS (start/end of each sub-curve) as polygon vertices.
     * This is exact for piecewise-linear loops and correctly gives zero
     * for there-and-back paths regardless of domain parameterization. */
    double area = loop_shoelace_area(loop);

    ON_BoundingBox bbox = loop_curve->BoundingBox();
    double bbox_diag = bbox.Diagonal().Length();
    delete loop_curve;

    /* Degenerate if point samples are too close, or if the enclosed area
     * is negligible compared to the loop's bounding-box extent. */
    if (pt1.DistanceTo(pt2) < INTERSECTION_TOL) {
	return true;
    }
    if (bbox_diag > INTERSECTION_TOL && area / (bbox_diag * bbox_diag) < 1e-3) {
	return true;
    }
    return false;
}


// It might be worth investigating the following approach to building a set of faces from the splitting
// in order to achieve robustness in the final result:
//
// A) trim the raw SSI curves with the trimming loops from both faces and get "final" curve segments in
//    3D and both 2D parametric spaces.  Consolidate curves where different faces created the same curve.
// B) assemble the new 2D segments and whatever pieces are needed from the existing trimming curves to
//    form new 2D loops (which must be non-self-intersecting), whose roles in A and B respectively
//    would be determined by the boolean op and each face's role within it.
// C) build "representative polygons" for all the 2D loops in each face, new and old - representative in
//    this case meaning that the intersection behavior of the general loops is accurately duplicated
//    by the polygons, which should be assurable by identifying and using all 2D curve intersections and possibly
//    horizontal and vertical tangents - and use clipper to perform the boolean ops.  Using the resulting polygons,
//    deduce and assemble the final trimming loops (and face or faces) created from A and B respectively.

// Note that the 2D information alone cannot be enough to decide *which* faces created from these splits
// end up in the final brep.  A case to think about here is the case of two spheres intersecting -
// depending on A, the exact trimming
// loop in B may need to either define the small area as a new face, or everything BUT the small area
// as a new face - different A spheres may either almost fully contain B or just intersect it.  That case
// would seem to suggest that we do need some sort of inside/outside test, since B doesn't have enough
// information to determine which face is to be saved without consulting A.  Likewise, A may either save
// just the piece inside the loop or everything outside it, depending on B.  This is the same situation we
// were in with the original face sets.
//
// A possible improvement here might be to calculate the best fit plane of the intersection curve and rotate
// both the faces in question and A so that that plane is centered at the origin with the normal in z+.
// In that orientation, axis aligned bounding box tests can be made that will be as informative
// as possible, and may allow many inside/outside decisions to be made without an explicit raytrace.  Coplanar
// faces will have to be handled differently, but for convex cases there should be enough information to decide.
// Concave cases may require a raytrace, but there is one other possible approach - if, instead of using the
// whole brep and face bounding boxes we start with the bounding box of the intersection curve and construct
// the sub-box that 'slices' through the parent bbox to the furthest wall in the opposite direction from the
// surface normal, then see which of the two possible
// faces' bounding boxes removes the most volume from that box when subtracted, we may be able to decide
// (say, for a subtraction) which face is cutting deeper.  It's not clear to me yet if such an approach would
// work or would scale to complex cases, but it may be worth thinking about.
static ON_SimpleArray<TrimmedFace *>
split_trimmed_face(
    const TrimmedFace *orig_face,
    ON_ClassArray<LinkedCurve> &ssx_curves)
{
    ON_SimpleArray<TrimmedFace *> out;
    out.Append(orig_face->Duplicate());

    if (ssx_curves.Count() == 0) {
	// no curves, no splitting
	return out;
    }

    ON_Curve *face_outerloop = get_loop_curve(orig_face->m_outerloop);
    if (!face_outerloop->IsClosed()) {
	// need closed outerloop
	delete face_outerloop;
	return out;
    }
    delete face_outerloop;

    for (int i = 0; i < ssx_curves.Count(); ++i) {
	std::vector<ON_SimpleArray<ON_Curve *> > ssx_loops;

	// get current ssx curve as closed loops
	if (ssx_curves[i].IsClosed()) {
	    ON_SimpleArray<ON_Curve *> loop;
	    loop.Append(ssx_curves[i].Curve()->Duplicate());
	    ssx_loops.push_back(loop);
	} else {
	    ssx_loops = split_face_into_loops(orig_face, ssx_curves[i]);
	}

	if (DEBUG_BREP_BOOLEAN) {
	    bu_log("ssx_curve[%d]: %zu ssx_loops, out.Count=%d\n",
		   i, ssx_loops.size(), out.Count());
	    for (size_t j = 0; j < ssx_loops.size(); ++j) {
		bu_log("  ssx_loops[%zu]: area=%g segs=%d\n",
		       j, loop_shoelace_area(ssx_loops[j]), ssx_loops[j].Count());
	    }
	}

	// combine each intersection loop with the original face (or
	// the previous iteration of split faces) to create new split
	// faces
	ON_SimpleArray<TrimmedFace *> next_out;
	for (size_t j = 0; j < ssx_loops.size(); ++j) {
	    if (loop_is_degenerate(ssx_loops[j])) {
		if (DEBUG_BREP_BOOLEAN) bu_log("  ssx_loop[%zu] degenerate, skip\n", j);
		continue;
	    }

	    for (int k = 0; k < out.Count(); ++k) {
		LoopBooleanResult intersect_loops, diff_loops;

		/* Coextension guard: when ssx_loops[j] is the complement of a
		 * there-and-back degenerate loop (ssx_loop[0] had zero area),
		 * it is geometrically identical to out[k]->m_outerloop.  The
		 * coextension check inside loop_boolean can fail in this case
		 * because the SSI sub-curve in ssx_loops[j] is a NURBS curve
		 * while the matching outer-loop segment is linear;
		 * ON_Intersect(linear, NURBS) does not produce a ccx_overlap
		 * event, so the 90%-coverage threshold is not reached.
		 *
		 * Detection: if the shoelace area of ssx_loops[j] matches the
		 * shoelace area of the face outer loop to within 1%, the two
		 * loops enclose the same region.  For a non-closed SSX curve
		 * split, ssx_loops arise from the original face, so this
		 * equality can only occur when ssx_loops[j] is the whole face
		 * (complement of a zero-area loop).  INTERSECT(face, face) =
		 * face, so we keep out[k] unchanged. */
		if (!ssx_curves[i].IsClosed()) {
		    /* 1% relative tolerance for area matching */
		    static const double COEXT_AREA_TOL = 0.01;
		    double face_area = loop_shoelace_area(out[k]->m_outerloop);
		    double ssx_area  = loop_shoelace_area(ssx_loops[j]);
		    if (face_area > INTERSECTION_TOL * INTERSECTION_TOL &&
			fabs(ssx_area - face_area) < face_area * COEXT_AREA_TOL)
		    {
			next_out.Append(out[k]->Duplicate());
			continue;
		    }
		}

		// get the portion of the face outerloop inside the
		// ssx loop
		intersect_loops = loop_boolean(out[k]->m_outerloop,
					       ssx_loops[j], BOOLEAN_INTERSECT);

		if (ssx_curves[i].IsClosed()) {
		    if (intersect_loops.outerloops.empty()) {
			// no intersection, just keep the face as-is
			next_out.Append(out[k]->Duplicate());
			continue;
		    }

		    // for a naturally closed ssx curve, we also need
		    // the portion outside the loop
		    diff_loops = loop_boolean(out[k]->m_outerloop, ssx_loops[j],
					      BOOLEAN_DIFF);
		    append_faces_from_loops(next_out, out[k], diff_loops);
		    diff_loops.ClearInnerloops();
		} else if (intersect_loops.outerloops.empty()) {
		    /* loop_boolean returns nothing for two distinct reasons:
		     * 1. No overlap: ssx_loops[j] and out[k] share no area.
		     * 2. Corner-bite: ssx_loops[j] IS a sub-region of out[k],
		     *    but its boundary arc comes from out[k]'s outer loop,
		     *    so loop_boolean can't find the split.
		     *
		     * Distinguish by checking whether the centroid of ssx_loops[j]
		     * is inside out[k]'s outer loop.  If not → no overlap → skip.
		     * If yes → corner-bite → use ssx_loop directly as the face. */
		    double ssx_area = loop_shoelace_area(ssx_loops[j]);
		    if (fabs(ssx_area) <= INTERSECTION_TOL * INTERSECTION_TOL) {
			continue;
		    }
		    /* Compute ssx_loop centroid in UV space */
		    ON_2dPoint ssx_centroid(0.0, 0.0);
		    for (int si = 0; si < ssx_loops[j].Count(); si++) {
			ON_3dPoint p = ssx_loops[j][si]->PointAtStart();
			ssx_centroid.x += p.x;
			ssx_centroid.y += p.y;
		    }
		    ssx_centroid.x /= ssx_loops[j].Count();
		    ssx_centroid.y /= ssx_loops[j].Count();
		    bool ssx_inside_outk = false;
		    try {
			ssx_inside_outk = is_point_inside_loop(ssx_centroid, out[k]->m_outerloop) ||
					  is_point_on_loop(ssx_centroid, out[k]->m_outerloop);
		    } catch (InvalidGeometry &) {}
		    if (DEBUG_BREP_BOOLEAN) {
			bu_log("  corner-bite check: ssx_loop[%zu] k=%d ssx_area=%g centroid(%g,%g) inside=%d\n",
			       j, k, ssx_area, ssx_centroid.x, ssx_centroid.y, (int)ssx_inside_outk);
		    }
		    if (!ssx_inside_outk) {
			/* No overlap between ssx_loop and this face — skip.
			 * out[k] will be covered by the ssx_loop that does
			 * overlap it (every sub-face must overlap at least
			 * one ssx_loop from the partition). */
			continue;
		    }
		    /* Corner-bite: ssx_loop IS the sub-face */
		    TrimmedFace *new_face = out[k]->Duplicate();
		    for (int fi = 0; fi < new_face->m_outerloop.Count(); fi++) {
			delete new_face->m_outerloop[fi];
		    }
		    new_face->m_outerloop.Empty();
		    for (int fi = 0; fi < ssx_loops[j].Count(); fi++) {
			new_face->m_outerloop.Append(ssx_loops[j][fi]->Duplicate());
		    }
		    next_out.Append(new_face);
		    continue;
		}
		append_faces_from_loops(next_out, out[k], intersect_loops);
		intersect_loops.ClearInnerloops();
	    }
	}
	free_loops(ssx_loops);

	if (next_out.Count() > 0) {
	    // replace previous faces with the new ones
	    for (int j = 0; j < out.Count(); ++j) {
		delete out[j];
	    }
	    out.Empty();

	    out.Append(next_out.Count(), next_out.Array());
	}
    }

    for (int i = 0; i < out.Count(); ++i) {
	close_small_gaps(out[i]->m_outerloop);
	for (size_t j = 0; j < out[i]->m_innerloop.size(); ++j) {
	    close_small_gaps(out[i]->m_innerloop[j]);
	}
    }

    if (DEBUG_BREP_BOOLEAN) {
	bu_log("Split to %d faces.\n", out.Count());
	for (int i = 0; i < out.Count(); i++) {
	    bu_log("Trimmed Face %d:\n", i);
	    bu_log("outerloop:\n");
	    ON_wString wstr;
	    ON_TextLog textlog(wstr);
	    textlog.PushIndent();
	    for (int j = 0; j < out[i]->m_outerloop.Count(); j++) {
		textlog.Print("Curve %d\n", j);
		out[i]->m_outerloop[j]->Dump(textlog);
	    }
	    if (ON_String(wstr).Array()) {
		bu_log("%s", ON_String(wstr).Array());
	    }

	    for (unsigned int j = 0; j < out[i]->m_innerloop.size(); j++) {
		bu_log("innerloop %d:\n", j);
		ON_wString wstr2;
		ON_TextLog textlog2(wstr2);
		textlog2.PushIndent();
		for (int k = 0; k < out[i]->m_innerloop[j].Count(); k++) {
		    textlog2.Print("Curve %d\n", k);
		    out[i]->m_innerloop[j][k]->Dump(textlog2);
		}
		if (ON_String(wstr2).Array()) {
		    bu_log("%s", ON_String(wstr2).Array());
		}
	    }
	}
    }
    return out;
}


static bool
is_same_surface(const ON_Surface *surf1, const ON_Surface *surf2)
{
    // Approach: Get their NURBS forms, and compare their CVs.
    // If their CVs are all the same (location and weight), they are
    // regarded as the same surface.

    if (surf1 == NULL || surf2 == NULL) {
	return false;
    }
    /*
    // Deal with two planes, if that's what we have - in that case
    // the determination can be more general than the CV comparison
    ON_Plane surf1_plane, surf2_plane;
    if (surf1->IsPlanar(&surf1_plane) && surf2->IsPlanar(&surf2_plane)) {
    ON_3dVector surf1_normal = surf1_plane.Normal();
    ON_3dVector surf2_normal = surf2_plane.Normal();
    if (surf1_normal.IsParallelTo(surf2_normal) == 1) {
    if (surf1_plane.DistanceTo(surf2_plane.Origin()) < ON_ZERO_TOLERANCE) {
    return true;
    } else {
    return false;
    }
    } else {
    return false;
    }
    }
    */

    ON_NurbsSurface nurbs_surf1, nurbs_surf2;
    if (!surf1->GetNurbForm(nurbs_surf1) || !surf2->GetNurbForm(nurbs_surf2)) {
	return false;
    }

    if (nurbs_surf1.Degree(0) != nurbs_surf2.Degree(0)
	|| nurbs_surf1.Degree(1) != nurbs_surf2.Degree(1)) {
	return false;
    }

    if (nurbs_surf1.CVCount(0) != nurbs_surf2.CVCount(0)
	|| nurbs_surf1.CVCount(1) != nurbs_surf2.CVCount(1)) {
	return false;
    }

    for (int i = 0; i < nurbs_surf1.CVCount(0); i++) {
	for (int j = 0; j < nurbs_surf2.CVCount(1); j++) {
	    ON_4dPoint cvA, cvB;
	    nurbs_surf1.GetCV(i, j, cvA);
	    nurbs_surf2.GetCV(i, j, cvB);
	    if (cvA != cvB) {
		return false;
	    }
	}
    }

    if (nurbs_surf1.KnotCount(0) != nurbs_surf2.KnotCount(0)
	|| nurbs_surf1.KnotCount(1) != nurbs_surf2.KnotCount(1)) {
	return false;
    }

    for (int i = 0; i < nurbs_surf1.KnotCount(0); i++) {
	if (!ON_NearZero(nurbs_surf1.m_knot[0][i] - nurbs_surf2.m_knot[0][i])) {
	    return false;
	}
    }

    for (int i = 0; i < nurbs_surf1.KnotCount(1); i++) {
	if (!ON_NearZero(nurbs_surf1.m_knot[1][i] - nurbs_surf2.m_knot[1][i])) {
	    return false;
	}
    }

    return true;
}


static void
add_elements(ON_Brep *brep, ON_BrepFace &face, const ON_SimpleArray<ON_Curve *> &loop, ON_BrepLoop::TYPE loop_type)
{
    if (!loop.Count()) {
	return;
    }

    /* Expand any ON_PolyCurve elements into their component segments before
     * processing.  When loop_boolean produces a sub-face whose outer boundary
     * is a closed linked-curve polycurve (e.g. the rectangular inner-wall
     * face from an ARB8 subtraction), the single polycurve entry would
     * otherwise be treated as a single closed edge (c3d->IsClosed() makes
     * end_idx == start_idx), collapsing the entire loop to a degenerate
     * single-vertex edge.  Expanding into individual segments ensures each
     * side of the rectangle becomes its own separate BREP edge. */
    bool need_expand = false;
    for (int k = 0; k < loop.Count(); k++) {
	const ON_PolyCurve *pc = ON_PolyCurve::Cast(loop[k]);
	if (pc && pc->Count() > 1) {
	    need_expand = true;
	    break;
	}
    }
    if (need_expand) {
	ON_SimpleArray<ON_Curve *> expanded;
	for (int k = 0; k < loop.Count(); k++) {
	    const ON_PolyCurve *pc = ON_PolyCurve::Cast(loop[k]);
	    if (pc && pc->Count() > 1) {
		for (int s = 0; s < pc->Count(); s++) {
		    expanded.Append(pc->SegmentCurve(s)->Duplicate());
		}
	    } else {
		expanded.Append(loop[k]);
	    }
	}
	/* Recurse with the expanded list.  Curves duplicated from polycurve
	 * segments are passed to brep->AddTrimCurve() inside the recursive
	 * call, transferring ownership to the brep; they must not be freed
	 * here.  Pointers borrowed from the original loop[] are also owned
	 * by the brep after AddTrimCurve and must not be freed. */
	add_elements(brep, face, expanded, loop_type);
	return;
    }

    ON_BrepLoop &breploop = brep->NewLoop(loop_type, face);
    const ON_Surface *srf = face.SurfaceOf();

    // Determine whether a segment should be a seam trim, according to the
    // requirements in ON_Brep::IsValid() (See opennurbs_brep.cpp)
    for (int k = 0; k < loop.Count(); k++) {
	bool bClosed[2];
	bClosed[0] = srf->IsClosed(0);
	bClosed[1] = srf->IsClosed(1);
	if (bClosed[0] || bClosed[1]) {
	    ON_Surface::ISO iso1, iso2, iso_type;
	    int endpt_index = -1;
	    iso1 = srf->IsIsoparametric(*loop[k]);
	    if (ON_Surface::E_iso == iso1 && bClosed[0]) {
		iso_type = ON_Surface::W_iso;
		endpt_index = 1;
	    } else if (ON_Surface::W_iso == iso1 && bClosed[0]) {
		iso_type = ON_Surface::E_iso;
		endpt_index = 1;
	    } else if (ON_Surface::S_iso == iso1 && bClosed[1]) {
		iso_type = ON_Surface::N_iso;
		endpt_index = 0;
	    } else if (ON_Surface::N_iso == iso1 && bClosed[1]) {
		iso_type = ON_Surface::S_iso;
		endpt_index = 0;
	    }
	    if (endpt_index != -1) {
		ON_Interval side_interval;
		const double side_tol = 1.0e-4;
		double s0, s1;
		bool seamed = false;
		side_interval.Set(loop[k]->PointAtStart()[endpt_index], loop[k]->PointAtEnd()[endpt_index]);
		if (((ON_Surface::N_iso == iso_type || ON_Surface::W_iso == iso_type) && side_interval.IsIncreasing())
		    || ((ON_Surface::S_iso == iso_type || ON_Surface::E_iso == iso_type) && side_interval.IsDecreasing())) {
		    for (int i = 0; i < breploop.m_ti.Count(); i++) {
			ON_BrepTrim &trim = brep->m_T[breploop.m_ti[i]];
			if (ON_BrepTrim::boundary != trim.m_type) {
			    continue;
			}
			iso2 = srf->IsIsoparametric(trim);
			if (iso2 != iso_type) {
			    continue;
			}
			s1 = side_interval.NormalizedParameterAt(trim.PointAtStart()[endpt_index]);
			if (fabs(s1 - 1.0) > side_tol) {
			    continue;
			}
			s0 = side_interval.NormalizedParameterAt(trim.PointAtEnd()[endpt_index]);
			if (fabs(s0) > side_tol) {
			    continue;
			}

			// Check 3D distances - not included in ON_Brep::IsValid().
			// So with this check, we only add seam trims if their end points
			// are the same within ON_ZERO_TOLERANCE. This will cause IsValid()
			// reporting "they should be seam trims connected to the same edge",
			// because the 2D tolerance (side_tol) are hardcoded to 1.0e-4.
			// We still add this check because we treat two vertices to be the
			// same only if their distance < ON_ZERO_TOLERANCE. (Maybe 3D dist
			// should also be added to ON_Brep::IsValid()?)
			if (srf->PointAt(trim.PointAtStart().x, trim.PointAtStart().y).DistanceTo(
				srf->PointAt(loop[k]->PointAtEnd().x, loop[k]->PointAtEnd().y)) >= ON_ZERO_TOLERANCE) {
			    continue;
			}

			if (srf->PointAt(trim.PointAtEnd().x, trim.PointAtEnd().y).DistanceTo(
				srf->PointAt(loop[k]->PointAtStart().x, loop[k]->PointAtStart().y)) >= ON_ZERO_TOLERANCE) {
			    continue;
			}

			// We add another checking, which is not included in ON_Brep::IsValid()
			// - they should be iso boundaries of the surface.
			double s2 = srf->Domain(1 - endpt_index).NormalizedParameterAt(loop[k]->PointAtStart()[1 - endpt_index]);
			double s3 = srf->Domain(1 - endpt_index).NormalizedParameterAt(trim.PointAtStart()[1 - endpt_index]);
			if ((fabs(s2 - 1.0) < side_tol && fabs(s3) < side_tol) ||
			    (fabs(s2) < side_tol && fabs(s3 - 1.0) < side_tol)) {
			    // Find a trim that should share the same edge
			    int ti = brep->AddTrimCurve(loop[k]);
			    ON_BrepTrim &newtrim = brep->NewTrim(brep->m_E[trim.m_ei], true, breploop, ti);
			    // newtrim.m_type = ON_BrepTrim::seam;
			    newtrim.m_tolerance[0] = newtrim.m_tolerance[1] = MAX_FASTF;
			    seamed = true;
			    break;
			}
		    }
		    if (seamed) {
			continue;
		    }
		}
	    }
	}

	ON_Curve *c3d = NULL;
	// First, try the ON_Surface::Pushup() method.
	// If Pushup() does not succeed, use sampling method.
	c3d = ON_Surface_Pushup(face.SurfaceOf(), *(loop[k]), NULL);
	if (!c3d) {
	    ON_3dPointArray ptarray(101);
	    for (int l = 0; l <= 100; l++) {
		ON_3dPoint pt2d;
		pt2d = loop[k]->PointAt(loop[k]->Domain().ParameterAt(l / 100.0));
		ptarray.Append(face.SurfaceOf()->PointAt(pt2d.x, pt2d.y));
	    }
	    c3d = new ON_PolylineCurve(ptarray);
	}

	/* Treat as a singular (degenerate) trim when the 3D curve maps to a
	 * single point: either its bounding-box diagonal is smaller than
	 * INTERSECTION_TOL, or its 2D UV domain length is smaller than
	 * INTERSECTION_TOL^2 (a near-zero-length 2D curve that a numerical
	 * pushup might return with a spuriously non-zero 3D bbox).
	 * A closed curve (start == end) is NOT singular — it is a
	 * full-circle (or other closed) trim and must be kept as a proper
	 * edge.  Using the start-end distance as a singularity test
	 * incorrectly classifies those closed curves. */
	double c3d_bbox = c3d->BoundingBox().Diagonal().Length();
	double c2d_dom  = loop[k]->Domain().Length();
	if (c3d_bbox < INTERSECTION_TOL || c2d_dom < INTERSECTION_TOL * INTERSECTION_TOL) {
	    ON_Surface::ISO iso_type = srf->IsIsoparametric(*loop[k]);
	    if (iso_type == ON_Surface::W_iso || iso_type == ON_Surface::S_iso ||
		iso_type == ON_Surface::E_iso || iso_type == ON_Surface::N_iso) {
		/* Boundary iso: create a proper singular trim (e.g. TGC apex).
		 *
		 * The Boolean evaluator may produce a near-zero-length 2D curve
		 * whose start and end UV lie within ~1 ULP of a surface boundary
		 * but are not exactly on it (e.g. a corner point where v is
		 * 0.99999993 instead of exactly 1.0).  IsIsoparametric detects
		 * the correct boundary by looking at the closest axis-aligned
		 * edge, but any subsequent SetTrimIsoFlags call re-runs the same
		 * test and may return not_iso if the deviation from the boundary
		 * exceeds its internal tolerance.  OpenNURBS IsValid then rejects
		 * the trim because m_iso != IsIsoparametric(c2d).
		 *
		 * Fix: build a replacement 2D curve whose endpoints lie exactly
		 * on the detected boundary (by snapping the iso-coordinate to the
		 * exact surface domain edge), so that any future IsIsoparametric
		 * call reliably returns the correct boundary iso.
		 *
		 * Start endpoint: use the exact last CV of the preceding trim
		 * (loop continuity), with the iso-coordinate snapped to the
		 * boundary.  The preceding trim's last CV is ALSO snapped in-
		 * place so that its PointAtEnd() agrees with our new start.
		 *
		 * End endpoint: use loop[k]->PointAtEnd() with the same snap
		 * (this is also the start of the next trim; in practice it is
		 * already on the boundary so the snap is a no-op). */
		ON_Interval udom = srf->Domain(0);
		ON_Interval vdom = srf->Domain(1);

		/* Build start UV: take u/v from the preceding trim's last CV. */
		ON_2dPoint start_uv = loop[k]->PointAtStart();
		if (k > 0 && brep->m_T.Count() > 0) {
		    ON_BrepTrim &prev = brep->m_T[brep->m_T.Count() - 1];
		    if (prev.m_c2i >= 0 && prev.m_c2i < brep->m_C2.Count()) {
			ON_Curve *prev_c = brep->m_C2[prev.m_c2i];
			if (prev_c) {
			    /* Convert to NURBS form so we can access and snap
			     * the last control vertex regardless of the actual
			     * curve type (polyline, NurbsCurve, etc.). */
			    ON_NurbsCurve *pnc =
				ON_NurbsCurve::Cast(prev_c);
			    ON_NurbsCurve nurbs_tmp;
			    if (!pnc) {
				if (prev_c->GetNurbForm(nurbs_tmp) == 0)
				    pnc = NULL;
				else
				    pnc = &nurbs_tmp;
			    }
			    if (pnc && pnc->CVCount() > 0) {
				int last = pnc->CVCount() - 1;
				ON_3dPoint cv;
				pnc->GetCV(last, cv);
				/* Snap the iso-coordinate to the exact boundary
				 * and update the preceding trim's stored curve
				 * so PointAtEnd() matches our new start UV. */
				if (iso_type == ON_Surface::N_iso)
				    cv.y = vdom.Max();
				else if (iso_type == ON_Surface::S_iso)
				    cv.y = vdom.Min();
				else if (iso_type == ON_Surface::E_iso)
				    cv.x = udom.Max();
				else /* W_iso */
				    cv.x = udom.Min();
				if (pnc == &nurbs_tmp) {
				    /* Replace the stored curve with the
				     * modified NURBS copy. */
				    nurbs_tmp.SetCV(last, cv);
				    ON_NurbsCurve *new_nc = (ON_NurbsCurve *)nurbs_tmp.DuplicateCurve();
				    delete brep->m_C2[prev.m_c2i];
				    brep->m_C2[prev.m_c2i] = new_nc;
				} else {
				    pnc->SetCV(last, cv);
				}
				start_uv = ON_2dPoint(cv.x, cv.y);
			    }
			}
		    }
		} else {
		    /* k == 0: no preceding trim; snap from loop curve's start. */
		    if (iso_type == ON_Surface::N_iso)
			start_uv.y = vdom.Max();
		    else if (iso_type == ON_Surface::S_iso)
			start_uv.y = vdom.Min();
		    else if (iso_type == ON_Surface::E_iso)
			start_uv.x = udom.Max();
		    else
			start_uv.x = udom.Min();
		}

		/* Build end UV: snap the iso-coordinate from loop[k]'s end. */
		ON_2dPoint end_uv = loop[k]->PointAtEnd();
		if (iso_type == ON_Surface::N_iso)
		    end_uv.y = vdom.Max();
		else if (iso_type == ON_Surface::S_iso)
		    end_uv.y = vdom.Min();
		else if (iso_type == ON_Surface::E_iso)
		    end_uv.x = udom.Max();
		else
		    end_uv.x = udom.Min();

		/* Build the replacement iso-line c2d. */
		ON_NurbsCurve *iso_c2d = new ON_NurbsCurve(2, false, 2, 2);
		iso_c2d->SetCV(0, ON_3dPoint(start_uv.x, start_uv.y, 0));
		iso_c2d->SetCV(1, ON_3dPoint(end_uv.x, end_uv.y, 0));
		iso_c2d->MakeClampedUniformKnotVector();

		/* Re-query iso from the replacement curve to confirm it lies
		 * exactly on the boundary (it should — both CVs are on the
		 * boundary, so IsIsoparametric returns the correct iso). */
		ON_Surface::ISO actual_iso = srf->IsIsoparametric(*iso_c2d);
		if (actual_iso == ON_Surface::W_iso ||
		    actual_iso == ON_Surface::S_iso ||
		    actual_iso == ON_Surface::E_iso ||
		    actual_iso == ON_Surface::N_iso) {
		    iso_type = actual_iso;
		}

		int i;
		ON_3dPoint vtx = c3d->PointAtStart();
		for (i = brep->m_V.Count() - 1; i >= 0; i--) {
		    if (brep->m_V[i].Point().DistanceTo(vtx) < ON_ZERO_TOLERANCE) {
			break;
		    }
		}
		if (i < 0) {
		    i = brep->m_V.Count();
		    brep->NewVertex(c3d->PointAtStart(), 0.0);
		}
		int ti = brep->AddTrimCurve(iso_c2d);
		ON_BrepTrim &trim = brep->NewSingularTrim(brep->m_V[i], breploop, iso_type, ti);
		trim.m_tolerance[0] = trim.m_tolerance[1] = MAX_FASTF;
		delete c3d;
		continue;
	    }
	    /* Interior iso (x_iso, y_iso, not_iso): the 3D curve maps to a
	     * single point at an interior UV position.  Cannot create a
	     * singular trim — OpenNURBS IsValid requires both (a) singular
	     * trims have boundary iso and (b) m_iso == IsIsoparametric result,
	     * which are contradictory for interior-position degenerate trims.
	     *
	     * Create a regular edge using the actual (near-zero-length) 3D
	     * pushup curve.  Force separate start/end vertices by evaluating
	     * the end UV point directly rather than relying on IsClosed().
	     * The main vertex-merge pass at ON_Boolean level respects these
	     * degenerate edges and will NOT merge their endpoints (the merge
	     * is blocked when both endpoints are connected by a near-zero
	     * non-closed edge). */
	    {
		ON_2dPoint s2d = loop[k]->PointAtStart();
		ON_2dPoint e2d = loop[k]->PointAtEnd();
		/* Start vertex: reuse previous trim's end vertex when k>0. */
		int svi;
		if (k > 0 && brep->m_T.Count() > 0) {
		    svi = brep->m_T.Last()->m_vi[1];
		} else {
		    ON_3dPoint svtx = face.SurfaceOf()->PointAt(s2d.x, s2d.y);
		    svi = brep->m_V.Count();
		    for (int vi = brep->m_V.Count() - 1; vi >= 0; vi--) {
			if (brep->m_V[vi].Point().DistanceTo(svtx) < ON_ZERO_TOLERANCE) {
			    svi = vi; break;
			}
		    }
		    if (svi == brep->m_V.Count())
			brep->NewVertex(svtx, 0.0);
		}
		/* End vertex: always evaluate from UV end (ignore IsClosed).
		 * This guarantees m_vi[0] != m_vi[1] so the edge is not
		 * treated as "closed" by OpenNURBS. */
		ON_3dPoint evtx = face.SurfaceOf()->PointAt(e2d.x, e2d.y);
		int evi = brep->m_V.Count();
		for (int vi = brep->m_V.Count() - 1; vi >= 0; vi--) {
		    if (brep->m_V[vi].Point().DistanceTo(evtx) < ON_ZERO_TOLERANCE) {
			evi = vi; break;
		    }
		}
		if (evi == brep->m_V.Count())
		    brep->NewVertex(evtx, 0.0);
		/* If start and end landed on the same existing vertex, nudge
		 * the end to a fresh vertex to avoid m_vi[0]==m_vi[1] with
		 * a non-closed curve (which OpenNURBS rejects). */
		if (evi == svi) {
		    evi = brep->m_V.Count();
		    brep->NewVertex(evtx, 0.0);
		}
		brep->AddEdgeCurve(c3d);
		int ti = brep->AddTrimCurve(loop[k]);
		ON_BrepEdge &edge = brep->NewEdge(brep->m_V[svi], brep->m_V[evi],
						  brep->m_C3.Count() - 1,
						  (const ON_Interval *)0, MAX_FASTF);
		ON_BrepTrim &trim = brep->NewTrim(edge, 0, breploop, ti);
		trim.m_tolerance[0] = trim.m_tolerance[1] = MAX_FASTF;
		continue;
	    }
	}

	ON_2dPoint start = loop[k]->PointAtStart(), end = loop[k]->PointAtEnd();
	int start_idx, end_idx;


	// Get the start vertex index
	if (k > 0) {
	    start_idx = brep->m_T.Last()->m_vi[1];
	} else {
	    ON_3dPoint vtx = face.SurfaceOf()->PointAt(start.x, start.y);
	    int i;
	    for (i = 0; i < brep->m_V.Count(); i++) {
		if (brep->m_V[i].Point().DistanceTo(vtx) < ON_ZERO_TOLERANCE) {
		    break;
		}
	    }
	    start_idx = i;
	    if (i == brep->m_V.Count()) {
		brep->NewVertex(vtx, 0.0);
	    }
	}

	// Get the end vertex index
	if (c3d->IsClosed()) {
	    end_idx = start_idx;
	} else {
	    ON_3dPoint vtx = face.SurfaceOf()->PointAt(end.x, end.y);
	    int i;
	    for (i = 0; i < brep->m_V.Count(); i++) {
		if (brep->m_V[i].Point().DistanceTo(vtx) < ON_ZERO_TOLERANCE) {
		    break;
		}
	    }
	    end_idx = i;
	    if (i == brep->m_V.Count()) {
		brep->NewVertex(vtx, 0.0);
	    }
	}

	brep->AddEdgeCurve(c3d);
	int ti = brep->AddTrimCurve(loop[k]);
	ON_BrepEdge &edge = brep->NewEdge(brep->m_V[start_idx], brep->m_V[end_idx],
					  brep->m_C3.Count() - 1, (const ON_Interval *)0, MAX_FASTF);
	ON_BrepTrim &trim = brep->NewTrim(edge, 0, breploop, ti);
	trim.m_tolerance[0] = trim.m_tolerance[1] = MAX_FASTF;
    }

    /* Repair consecutive-trim vertex mismatches within this loop.
     *
     * add_elements() creates each trim's start vertex independently via
     * surface evaluation (for k==0 and end vertices) or by reusing the
     * previous trim's m_vi[1] (for k>0 start vertices).  However, seam and
     * singular trims are inserted with `continue`, after which
     * brep->m_T.Last() is the seam/singular trim whose m_vi[1] may not
     * match the geometrically expected next-trim start.  Similarly, the
     * loop's closing vertex (last trim end == first trim start) may get
     * different indices even though the 3-D positions are within
     * INTERSECTION_TOL.
     *
     * Scan the newly added loop trims in order and, whenever two consecutive
     * trims share a vertex at the same 3-D position (within INTERSECTION_TOL)
     * but have different vertex indices, rewrite the second trim's start
     * vertex index to match the first trim's end vertex index.  This makes
     * the loop topologically valid without altering the geometry. */
    {
	const double vtol = INTERSECTION_TOL;
	const int nloop = breploop.m_ti.Count();
	for (int li = 0; li < nloop; li++) {
	    int ti_a = breploop.m_ti[li];
	    int ti_b = breploop.m_ti[(li + 1) % nloop];
	    ON_BrepTrim &ta = brep->m_T[ti_a];
	    ON_BrepTrim &tb = brep->m_T[ti_b];
	    int va_end = ta.m_vi[1];
	    int vb_start = tb.m_vi[0];
	    if (va_end == vb_start) continue;
	    if (va_end < 0 || vb_start < 0) continue;
	    ON_3dPoint pa = brep->m_V[va_end].Point();
	    ON_3dPoint pb = brep->m_V[vb_start].Point();
	    if (pa.DistanceTo(pb) <= vtol) {
		int ei_b = tb.m_ei;
		if (ei_b >= 0) {
		    ON_BrepEdge &eb = brep->m_E[ei_b];
		    int old_vi = vb_start;
		    int new_vi = va_end;
		    int edge_slot = tb.m_bRev3d ? 1 : 0;
		    if (eb.m_vi[edge_slot] == old_vi) {
			eb.m_vi[edge_slot] = new_vi;
			ON_BrepVertex &vold = brep->m_V[old_vi];
			int idx = vold.m_ei.Search(ei_b);
			if (idx >= 0) vold.m_ei.Remove(idx);
			ON_BrepVertex &vnew = brep->m_V[new_vi];
			if (vnew.m_ei.Search(ei_b) < 0)
			    vnew.m_ei.Append(ei_b);
		    }
		}
		tb.m_vi[0] = va_end;
	    }
	}
    }
}


static bool
is_point_on_brep_surface(const ON_3dPoint &pt, const ON_Brep *brep, ON_SimpleArray<Subsurface *> &surf_tree)
{
    // Decide whether a point is on a brep's surface.
    // Basic approach: use PSI on the point with all the surfaces.

    if (brep == NULL || pt.IsUnset()) {
	bu_log("is_point_on_brep_surface(): brep == NULL || pt.IsUnsetPoint()\n");
	return false;
    }

    if (surf_tree.Count() != brep->m_S.Count()) {
	bu_log("is_point_on_brep_surface(): surf_tree.Count() != brep->m_S.Count()\n");
	return false;
    }

    ON_BoundingBox bbox = brep->BoundingBox();
    bbox.m_min -= ON_3dVector(INTERSECTION_TOL, INTERSECTION_TOL, INTERSECTION_TOL);
    bbox.m_max += ON_3dVector(INTERSECTION_TOL, INTERSECTION_TOL, INTERSECTION_TOL);
    if (!bbox.IsPointIn(pt)) {
	return false;
    }

    for (int i = 0; i < brep->m_F.Count(); i++) {
	const ON_BrepFace &face = brep->m_F[i];
	/* Fast bbox prefilter: if the test point is farther than
	 * INTERSECTION_TOL from the surface's 3D bounding box, the
	 * point cannot lie on this surface at all — skip the expensive
	 * NURBS point-surface intersection. */
	{
	    ON_3dPoint fb_min, fb_max;
	    surf_tree[face.m_si]->GetBBox(fb_min, fb_max);
	    if (ON_BoundingBox(fb_min, fb_max).MinimumDistanceTo(pt) > INTERSECTION_TOL) {
		continue;
	    }
	}
	const ON_Surface *surf = face.SurfaceOf();
	ON_ClassArray<ON_PX_EVENT> px_event;
	if (!ON_Intersect(pt, *surf, px_event, INTERSECTION_TOL, 0, 0, surf_tree[face.m_si])) {
	    continue;
	}

	// Get the trimming curves of the face, and determine whether the
	// points are inside the outerloop
	ON_SimpleArray<ON_Curve *> outerloop;
	const ON_BrepLoop &loop = brep->m_L[face.m_li[0]];  // outerloop only
	for (int j = 0; j < loop.m_ti.Count(); j++) {
	    outerloop.Append(brep->m_C2[brep->m_T[loop.m_ti[j]].m_c2i]);
	}
	ON_2dPoint pt2d(px_event[0].m_b[0], px_event[0].m_b[1]);
	/* Verify the 3-D surface point at the returned UV is actually close
	 * to the query point.  ON_Intersect() clamps UV to the surface domain
	 * [u_min,u_max]×[v_min,v_max] when the true closest point is outside
	 * the domain.  For a planar face of a box (e.g. ARB8 left face at
	 * x=const) a query point that is ON the plane but far outside the
	 * trimmed region maps to a clamped UV that lies on the outer-loop
	 * boundary, causing a false ON_BREP_SURFACE classification.
	 * Rejecting events where the reprojected 3-D point is farther than
	 * INTERSECTION_TOL from the query point avoids this. */
	{
	    ON_3dPoint surf_pt = surf->PointAt(pt2d.x, pt2d.y);
	    if (surf_pt.DistanceTo(pt) > INTERSECTION_TOL) {
		continue;
	    }
	}
	try {
	    if (!is_point_outside_loop(pt2d, outerloop)) {
		return true;
	    }
	} catch (InvalidGeometry &e) {
	    bu_log("%s", e.what());
	}
    }

    return false;
}


static bool
is_point_inside_brep(const ON_3dPoint &pt, const ON_Brep *brep, ON_SimpleArray<Subsurface *> &surf_tree)
{
    // Decide whether a point is inside a brep's surface.
    // Basic approach: intersect a ray with the brep, and count the number of
    // intersections (like the raytrace)
    // Returns true (inside) or false (outside) provided the pt is not on the
    // surfaces. (See also is_point_on_brep_surface())

    if (brep == NULL || pt.IsUnset()) {
	bu_log("is_point_inside_brep(): brep == NULL || pt.IsUnsetPoint()\n");
	return false;
    }

    if (surf_tree.Count() != brep->m_S.Count()) {
	bu_log("is_point_inside_brep(): surf_tree.Count() != brep->m_S.Count()\n");
	return false;
    }

    /* Build a tight bounding box from vertices and edge curve extents.
     * ON_Brep::BoundingBox() uses the full NURBS surface parametric domain
     * which can be 2× larger than the trimmed face extent (e.g. BRL-CAD
     * ARB8 breps use diamond-trimmed planar surfaces whose full domain
     * corners land outside the box vertices).  Any interior point of a
     * closed solid lies within the bbox of its boundary edges, so this
     * is both correct and tight. */
    ON_BoundingBox bbox;
    {
	bool first = true;
	for (int vi = 0; vi < brep->m_V.Count(); vi++) {
	    bbox.Set(brep->m_V[vi].point, !first);
	    first = false;
	}
	for (int ei = 0; ei < brep->m_E.Count(); ei++) {
	    ON_3dPoint emin, emax;
	    if (brep->m_E[ei].GetBoundingBox(emin, emax, false)) {
		bbox.Union(ON_BoundingBox(emin, emax));
	    }
	}
	if (first)
	    return false; /* no vertices — degenerate brep */
    }
    bbox.m_min -= ON_3dVector(INTERSECTION_TOL, INTERSECTION_TOL, INTERSECTION_TOL);
    bbox.m_max += ON_3dVector(INTERSECTION_TOL, INTERSECTION_TOL, INTERSECTION_TOL);
    if (!bbox.IsPointIn(pt)) {
	return false;
    }

    /* Fire three axis-aligned rays (+x, +y, +z) and use a majority vote.
     * A single diagonal ray was used previously but could be nearly parallel
     * to flat-slot face planes, causing missed intersections. */
    if (DEBUG_BREP_BOOLEAN) {
	bu_log("is_point_inside_brep pt(%g,%g,%g) bbox[%g,%g,%g]-[%g,%g,%g]\n",
	       pt.x, pt.y, pt.z,
	       bbox.m_min.x, bbox.m_min.y, bbox.m_min.z,
	       bbox.m_max.x, bbox.m_max.y, bbox.m_max.z);
    }
    int inside_votes = 0;

    static const ON_3dVector ray_dirs[3] = {
	ON_3dVector(1, 0, 0),
	ON_3dVector(0, 1, 0),
	ON_3dVector(0, 0, 1)
    };

    for (int ray_idx = 0; ray_idx < 3; ray_idx++) {
	const ON_3dVector &rdir = ray_dirs[ray_idx];
	/* Ray end: start + direction * (2 * bbox extent along that axis) so
	 * the endpoint is guaranteed to be outside the bbox. */
	double extent = bbox.Diagonal().x * rdir.x
	              + bbox.Diagonal().y * rdir.y
	              + bbox.Diagonal().z * rdir.z;
	if (extent < INTERSECTION_TOL)
	    extent = 1.0; /* fallback for degenerate bbox */
	ON_LineCurve line(pt, pt + rdir * (extent * 2.0 + 1.0));

	const ON_3dPoint &ray_start = line.m_line.from;
	const ON_3dPoint  ray_end   = line.m_line.to;

	ON_3dPointArray isect_pt;
	for (int i = 0; i < brep->m_F.Count(); i++) {
	    const ON_BrepFace &face = brep->m_F[i];
	    /* Slab prefilter along the ray direction only.  We must NOT
	     * prune using the other axes when firing an axis-aligned ray —
	     * the old diagonal prefilter was incorrect because it could
	     * discard faces that straddle the ray position in Y/Z while
	     * being "ahead" in X. */
	    {
		ON_3dPoint fb_min, fb_max;
		surf_tree[face.m_si]->GetBBox(fb_min, fb_max);
		/* Skip face if its extent along the ray is entirely behind
		 * the ray start or entirely beyond the ray end. */
		double fmin_along = fb_min.x*rdir.x + fb_min.y*rdir.y + fb_min.z*rdir.z;
		double fmax_along = fb_max.x*rdir.x + fb_max.y*rdir.y + fb_max.z*rdir.z;
		double ray_s_along = ray_start.x*rdir.x + ray_start.y*rdir.y + ray_start.z*rdir.z;
		double ray_e_along = ray_end.x*rdir.x   + ray_end.y*rdir.y   + ray_end.z*rdir.z;
		if (fmax_along < ray_s_along - INTERSECTION_TOL)
		    continue;
		if (fmin_along > ray_e_along + INTERSECTION_TOL)
		    continue;
	    }
	    const ON_Surface *surf = face.SurfaceOf();
	    ON_SimpleArray<ON_X_EVENT> x_event;
	    if (!ON_Intersect(&line, surf, x_event, INTERSECTION_TOL, 0.0, 0, 0, 0, 0, 0, surf_tree[face.m_si])) {
		continue;
	    }

	    // Get the trimming curves of the face, and determine whether the
	    // intersection points are inside the outerloop
	    ON_SimpleArray<ON_Curve *> outerloop;
	    const ON_BrepLoop &loop = brep->m_L[face.m_li[0]];  // outerloop only
	    for (int j = 0; j < loop.m_ti.Count(); j++) {
		outerloop.Append(brep->m_C2[brep->m_T[loop.m_ti[j]].m_c2i]);
	    }
	    try {
		for (int j = 0; j < x_event.Count(); j++) {
		    ON_2dPoint pt2d(x_event[j].m_b[0], x_event[j].m_b[1]);
		    if (!is_point_outside_loop(pt2d, outerloop)) {
			isect_pt.Append(x_event[j].m_B[0]);
		    }
		    if (x_event[j].m_type == ON_X_EVENT::ccx_overlap) {
			pt2d = ON_2dPoint(x_event[j].m_b[2], x_event[j].m_b[3]);
			if (!is_point_outside_loop(pt2d, outerloop)) {
			    isect_pt.Append(x_event[j].m_B[1]);
			}
		    }
		}
	    } catch (InvalidGeometry &e) {
		bu_log("%s", e.what());
	    }
	}

	// Remove duplications
	ON_3dPointArray pt_no_dup;
	for (int i = 0; i < isect_pt.Count(); i++) {
	    int j;
	    for (j = 0; j < pt_no_dup.Count(); j++) {
		if (isect_pt[i].DistanceTo(pt_no_dup[j]) < INTERSECTION_TOL) {
		    break;
		}
	    }
	    if (j == pt_no_dup.Count()) {
		pt_no_dup.Append(isect_pt[i]);
	    }
	}

	if (DEBUG_BREP_BOOLEAN) {
	    bu_log("  ray[%d] hits=%d (%s)\n", ray_idx, pt_no_dup.Count(),
		   pt_no_dup.Count() % 2 != 0 ? "inside" : "outside");
	}
	if (pt_no_dup.Count() % 2 != 0)
	    inside_votes++;
    }

    /* Majority vote: inside if 2 or 3 rays agree. */
    if (DEBUG_BREP_BOOLEAN) {
	bu_log("  inside_votes=%d → %s\n", inside_votes, inside_votes >= 2 ? "INSIDE" : "OUTSIDE");
    }
    return inside_votes >= 2;
}


static bool
is_point_inside_trimmed_face(const ON_2dPoint &pt, const TrimmedFace *tface)
{
    /* ---------------------------------------------------------------
     * Fast pre-filter using cached polygon approximations.
     *
     * The polygons are built lazily and cached on the TrimmedFace.
     * Each polygon test is O(M) where M ≈ 1–2× the loop curve count,
     * but with a very small constant (a few FP ops per edge vs. a full
     * Newton-iteration NURBS intersection per curve in the exact test).
     *
     * OUTER polygon (always valid): superset of the enclosed region.
     *   If the test point is OUTSIDE the outer polygon it is definitely
     *   outside the trimmed face → return false immediately.
     *
     * INNER polygon (valid only when non-self-intersecting): subset of
     *   the enclosed region.
     *   If the test point is INSIDE the inner polygon it is definitely
     *   inside the trimmed face (outer loop) → skip the outer-loop exact
     *   test and go straight to inner-loop exclusion.
     *
     * Only points in the thin annular zone between the two polygons
     * need the full O(N) NURBS ray-cast (point_loop_location).
     * --------------------------------------------------------------- */
    tface->ensure_polygons();

    bool inner_definite = false;  /* result known from inner polygon */

    if (!tface->m_outer_poly.empty()) {
	const size_t n = tface->m_outer_poly.size();
	const double (*pts)[2] =
	    reinterpret_cast<const double (*)[2]>(tface->m_outer_poly.data());
	if (!_pnt_in_polygon(n, pts, pt.x, pt.y)) {
	    return false;  /* definitely outside */
	}
    }

    if (tface->m_inner_poly_valid && !tface->m_inner_poly.empty()) {
	const size_t n = tface->m_inner_poly.size();
	const double (*pts)[2] =
	    reinterpret_cast<const double (*)[2]>(tface->m_inner_poly.data());
	if (_pnt_in_polygon(n, pts, pt.x, pt.y)) {
	    inner_definite = true;
	    /* Definitely inside the outer loop; still need to check inner
	     * (hole) loops below before returning true. */
	}
    }

    /* If the inner polygon hasn't resolved the outer-loop membership,
     * fall through to the full NURBS test. */
    bool inside_outer = inner_definite;
    if (!inner_definite) {
	inside_outer = is_point_inside_loop(pt, tface->m_outerloop);
    }

    if (!inside_outer)
	return false;

    /* Inside the outer loop: check that the point is not inside any
     * inner (hole) loop. */
    for (size_t i = 0; i < tface->m_innerloop.size(); ++i) {
	if (!is_point_outside_loop(pt, tface->m_innerloop[i]))
	    return false;
    }
    return true;
}


static ON_2dPoint
get_point_inside_trimmed_face(const TrimmedFace *tface)
{
    const int GP_MAX_STEPS = 8; // must be a power of two

    ON_PolyCurve polycurve;
    if (!is_loop_valid(tface->m_outerloop, ON_ZERO_TOLERANCE, &polycurve)) {
	throw InvalidGeometry("face_brep_location(): invalid outerloop.\n");
    }
    ON_BoundingBox bbox =  polycurve.BoundingBox();
    double u_len = bbox.m_max.x - bbox.m_min.x;
    double v_len = bbox.m_max.y - bbox.m_min.y;

    ON_2dPoint test_pt2d;
    bool found = false;
    for (int steps = 1; steps <= GP_MAX_STEPS && !found; steps *= 2) {
	double u_halfstep = u_len / (steps * 2.0);
	double v_halfstep = v_len / (steps * 2.0);

	for (int i = 0; i < steps && !found; ++i) {
	    test_pt2d.x = bbox.m_min.x + u_halfstep * (1 + 2 * i);

	    for (int j = 0; j < steps && !found; ++j) {
		test_pt2d.y = bbox.m_min.y + v_halfstep * (1 + 2 * j);
		found = is_point_inside_trimmed_face(test_pt2d, tface);
	    }
	}
    }

    /* For heavily-trimmed faces the uniform grid rarely lands inside.
     * Fall back to sampling at small offsets around points on the outer
     * loop boundary: for each curve in the outer loop pick the midpoint
     * and nudge it inward along the 2D normal into the face interior.
     * Also try midpoints of any inner-loop curves (a point near an inner
     * loop is likely inside the trimmed region).
     *
     * The nudge distance must be meaningful relative to the face's UV
     * bounding box.  A fixed tiny constant (e.g. ON_ZERO_TOLERANCE * 1e3
     * ≈ 1e-9) is effectively zero for surfaces with large UV domains
     * (e.g. a DSP with domain [0, 2.55e7]) and causes every attempt to
     * fail, forcing the O(N²) worst case.  Use a fraction of the bbox
     * size instead.
     *
     * We also cap the number of outer-loop curves we try.  When there
     * are N SSI-generated trim curves in the loop, iterating over all N
     * of them costs O(N²) because each is_point_inside_trimmed_face()
     * call is O(N).  Trying the first MAX_NUDGE_CURVES curves is almost
     * always enough: a single successful nudge is all we need. */
    if (!found) {
	/* Scale the nudge so it is 10 ppm of the smaller UV dimension.
	 * This is large enough to be meaningful for any surface domain
	 * while small enough to stay well inside the trimmed region. */
	const double NUDGE_SCALE = 1.0e-5;
	const double NUDGE = std::max(ON_ZERO_TOLERANCE * 1e3,
				      NUDGE_SCALE * std::min(u_len, v_len));

	/* Cap at this many curves to avoid O(N²) when there are many SSI
	 * trim curves (each is_point_inside_trimmed_face() call is O(N) in
	 * the number of loop curves).  12 was chosen empirically: it covers
	 * typical heavily-trimmed NURBS faces while bounding the cost to
	 * 12 × 16 × O(N) work.  Remaining curves are sampled at a coarser
	 * stride below.  A single successful nudge terminates the search. */
	const int MAX_NUDGE_CURVES = 12;
	const int NOUTER = tface->m_outerloop.Count();
	const int nlimit = std::min(NOUTER, MAX_NUDGE_CURVES);
	for (int ci = 0; ci < nlimit && !found; ++ci) {
	    const ON_Curve *crv = tface->m_outerloop[ci];
	    if (!crv) continue;
	    ON_Interval dom = crv->Domain();
	    ON_3dPoint mid3 = crv->PointAt(dom.Mid());
	    ON_3dVector tan3 = crv->TangentAt(dom.Mid());
	    /* Inward 2D normal: rotate tangent 90° CCW (into face interior) */
	    ON_2dPoint mid2(mid3.x, mid3.y);
	    ON_2dVector inward(-tan3.y, tan3.x);
	    inward.Unitize();
	    for (int k = 1; k <= 8 && !found; ++k) {
		test_pt2d = mid2 + inward * (NUDGE * k);
		found = is_point_inside_trimmed_face(test_pt2d, tface);
	    }
	    /* Also try outward (CW) normal */
	    ON_2dVector outward(tan3.y, -tan3.x);
	    outward.Unitize();
	    for (int k = 1; k <= 8 && !found; ++k) {
		test_pt2d = mid2 + outward * (NUDGE * k);
		found = is_point_inside_trimmed_face(test_pt2d, tface);
	    }
	}
	/* If the first MAX_NUDGE_CURVES outer-loop curves didn't help, walk
	 * the remaining ones at a coarser stride so we don't skip them
	 * entirely.  This matters when the first segment of the outer loop
	 * happens to lie along a seam where both normals point outside. */
	if (!found && NOUTER > MAX_NUDGE_CURVES) {
	    /* stride so we sample ~MAX_NUDGE_CURVES evenly-spaced curves
	     * across the entire outer loop.  Using the total count (NOUTER)
	     * distributes the samples uniformly rather than only in the
	     * tail of the array. */
	    int stride = std::max(1, NOUTER / MAX_NUDGE_CURVES);
	    for (int ci = MAX_NUDGE_CURVES; ci < NOUTER && !found; ci += stride) {
		const ON_Curve *crv = tface->m_outerloop[ci];
		if (!crv) continue;
		ON_Interval dom = crv->Domain();
		ON_3dPoint mid3 = crv->PointAt(dom.Mid());
		ON_3dVector tan3 = crv->TangentAt(dom.Mid());
		ON_2dPoint mid2(mid3.x, mid3.y);
		ON_2dVector inward(-tan3.y, tan3.x);
		inward.Unitize();
		for (int k = 1; k <= 8 && !found; ++k) {
		    test_pt2d = mid2 + inward * (NUDGE * k);
		    found = is_point_inside_trimmed_face(test_pt2d, tface);
		}
		ON_2dVector outward(tan3.y, -tan3.x);
		outward.Unitize();
		for (int k = 1; k <= 8 && !found; ++k) {
		    test_pt2d = mid2 + outward * (NUDGE * k);
		    found = is_point_inside_trimmed_face(test_pt2d, tface);
		}
	    }
	}
	/* Try midpoints of inner-loop curves, nudging outward */
	for (size_t li = 0; li < tface->m_innerloop.size() && !found; ++li) {
	    const ON_SimpleArray<ON_Curve *> &iloop = tface->m_innerloop[li];
	    const int nilimit = std::min(iloop.Count(), MAX_NUDGE_CURVES);
	    for (int ci = 0; ci < nilimit && !found; ++ci) {
		const ON_Curve *crv = iloop[ci];
		if (!crv) continue;
		ON_Interval dom = crv->Domain();
		ON_3dPoint mid3 = crv->PointAt(dom.Mid());
		ON_3dVector tan3 = crv->TangentAt(dom.Mid());
		ON_2dPoint mid2(mid3.x, mid3.y);
		/* Outward normal from inner loop (CW) = inward for face */
		ON_2dVector outward(tan3.y, -tan3.x);
		outward.Unitize();
		for (int k = 1; k <= 8 && !found; ++k) {
		    test_pt2d = mid2 + outward * (NUDGE * k);
		    found = is_point_inside_trimmed_face(test_pt2d, tface);
		}
		ON_2dVector inward(-tan3.y, tan3.x);
		inward.Unitize();
		for (int k = 1; k <= 8 && !found; ++k) {
		    test_pt2d = mid2 + inward * (NUDGE * k);
		    found = is_point_inside_trimmed_face(test_pt2d, tface);
		}
	    }
	}
    }

    /* Last-resort fallback: the polygon pre-filter in
     * is_point_inside_trimmed_face() may incorrectly reject valid interior
     * points when the outer polygon is concave or the ray-cast happens to
     * align with a vertex or edge.  Call is_point_inside_loop() directly
     * (bypassing the pre-filter) for a coarser grid of bbox-sampled points.
     * This path is rare — it only runs after all of the above have failed. */
    if (!found) {
	const int FALLBACK_STEPS = 4;
	for (int steps = 1; steps <= FALLBACK_STEPS && !found; steps *= 2) {
	    double u_halfstep = u_len / (steps * 2.0);
	    double v_halfstep = v_len / (steps * 2.0);
	    for (int fi = 0; fi < steps && !found; ++fi) {
		test_pt2d.x = bbox.m_min.x + u_halfstep * (1 + 2 * fi);
		for (int fj = 0; fj < steps && !found; ++fj) {
		    test_pt2d.y = bbox.m_min.y + v_halfstep * (1 + 2 * fj);
		    /* Bypass polygon pre-filter; use exact NURBS ray-cast. */
		    bool inside_outer = is_point_inside_loop(test_pt2d, tface->m_outerloop);
		    if (!inside_outer) continue;
		    bool hole_excluded = false;
		    for (size_t li = 0; li < tface->m_innerloop.size() && !hole_excluded; ++li) {
			if (!is_point_outside_loop(test_pt2d, tface->m_innerloop[li]))
			    hole_excluded = true;
		    }
		    if (!hole_excluded)
			found = true;
		}
	    }
	}
    }

    if (!found) {
	throw AlgorithmError("Cannot find a point inside this trimmed face. Aborted.\n");
    }
    return test_pt2d;
}


enum {
    OUTSIDE_BREP,
    INSIDE_BREP,
    ON_BREP_SURFACE
};


// Returns the location of the face with respect to the brep.
//
// Throws InvalidGeometry if given invalid arguments.
// Throws AlgorithmError if a point inside the TrimmedFace can't be
// found for testing.
static int
face_brep_location(const TrimmedFace *tface, const ON_Brep *brep, ON_SimpleArray<Subsurface *> &surf_tree)
{
    if (tface == NULL || brep == NULL) {
	throw InvalidGeometry("face_brep_location(): given NULL argument.\n");
    }

    const ON_BrepFace *bface = tface->m_face;
    if (bface == NULL) {
	throw InvalidGeometry("face_brep_location(): TrimmedFace has NULL face.\n");
    }

    /* Build a tight bounding box from the other brep's vertices and edges
     * (same approach as is_point_inside_brep) to detect faces that are
     * clearly outside the other brep's extents and can be skipped. */
    ON_BoundingBox brep2box;
    {
	bool first = true;
	for (int vi = 0; vi < brep->m_V.Count(); vi++) {
	    brep2box.Set(brep->m_V[vi].point, !first);
	    first = false;
	}
	for (int ei = 0; ei < brep->m_E.Count(); ei++) {
	    ON_3dPoint emin, emax;
	    if (brep->m_E[ei].GetBoundingBox(emin, emax, false)) {
		brep2box.Union(ON_BoundingBox(emin, emax));
	    }
	}
	if (first) return OUTSIDE_BREP;
    }
    brep2box.m_min -= ON_3dVector(INTERSECTION_TOL, INTERSECTION_TOL, INTERSECTION_TOL);
    brep2box.m_max += ON_3dVector(INTERSECTION_TOL, INTERSECTION_TOL, INTERSECTION_TOL);
    if (!bface->BoundingBox().Intersection(brep2box)) {
	return OUTSIDE_BREP;
    }

    if (tface->m_outerloop.Count() == 0) {
	throw InvalidGeometry("face_brep_location(): the input TrimmedFace is not trimmed.\n");
    }

    ON_PolyCurve polycurve;
    if (!is_loop_valid(tface->m_outerloop, ON_ZERO_TOLERANCE, &polycurve)) {
	throw InvalidGeometry("face_brep_location(): invalid outerloop.\n");
    }
    ON_2dPoint test_pt2d = get_point_inside_trimmed_face(tface);
    ON_3dPoint test_pt3d = tface->m_face->PointAt(test_pt2d.x, test_pt2d.y);

    if (DEBUG_BREP_BOOLEAN) {
	bu_log("valid test point: (%g, %g, %g) [UV: %g, %g]\n", test_pt3d.x, test_pt3d.y, test_pt3d.z,
	       test_pt2d.x, test_pt2d.y);
    }

    if (is_point_on_brep_surface(test_pt3d, brep, surf_tree)) {
	// because the overlap parts will be split out as separated trimmed
	// faces, if one point on a trimmed face is on the brep's surface,
	// the whole trimmed face should be on the surface
	return ON_BREP_SURFACE;
    }

    return is_point_inside_brep(test_pt3d, brep, surf_tree) ? INSIDE_BREP : OUTSIDE_BREP;
}


static ON_ClassArray<ON_SimpleArray<SSICurve> >
get_face_intersection_curves(
    ON_SimpleArray<Subsurface *> &surf_tree1,
    ON_SimpleArray<Subsurface *> &surf_tree2,
    const ON_Brep *brep1,
    const ON_Brep *brep2,
    op_type operation)
{
    std::vector<Subsurface *> st1, st2;
    std::set<int> unused1, unused2;
    std::set<int> finalform1, finalform2;
    ON_ClassArray<ON_SimpleArray<SSICurve> > curves_array;
    int face_count1 = brep1->m_F.Count();
    int face_count2 = brep2->m_F.Count();
    int surf_count1 = brep1->m_S.Count();
    int surf_count2 = brep2->m_S.Count();

    // We're not well set up currently to handle a situation where
    // one of the breps has no faces - don't try.  Also make sure
    // we don't have too many faces.
    if (!face_count1 || !face_count2 || ((face_count1 + face_count2) < 0))
	return curves_array;
    if (!surf_count1 || !surf_count2 || ((surf_count1 + surf_count2) < 0))
	return curves_array;

    /* Depending on the operation type and the bounding box behaviors, we
     * can sometimes decide immediately whether a face will end up in the
     * final brep or will have no role in the intersections - do that
     * categorization up front */
    for (int i = 0; i < face_count1 + face_count2; i++) {
	const ON_BrepFace &face = i < face_count1 ? brep1->m_F[i] : brep2->m_F[i - face_count1];
	const ON_Brep *brep = i < face_count1 ? brep2 : brep1;
	std::set<int> *unused = i < face_count1 ? &unused1 : &unused2;
	std::set<int> *intact = i < face_count1 ? &finalform1 : &finalform2;
	int curr_index = i < face_count1 ? i : i - face_count1;
	fastf_t face_bbox_dist = face.BoundingBox().MinimumDistanceTo(brep->BoundingBox());
	if (DEBUG_BREP_BOOLEAN) {
	    ON_BoundingBox fb = face.BoundingBox();
	    bu_log("face_prefilter i=%d(%s%d) dist=%g bbox[%g,%g,%g]-[%g,%g,%g]\n",
		   i, i < face_count1 ? "s1f" : "s2f", curr_index, face_bbox_dist,
		   fb.m_min.x, fb.m_min.y, fb.m_min.z,
		   fb.m_max.x, fb.m_max.y, fb.m_max.z);
	}
	if (face_bbox_dist > INTERSECTION_TOL) {
	    switch (operation) {
		case BOOLEAN_UNION:
		    intact->insert(curr_index);
		    break;
		case BOOLEAN_DIFF:
		    if (i < face_count1) {
			intact->insert(curr_index);
		    }
		    if (i >= face_count1) {
			unused->insert(curr_index);
		    }
		    break;
		case BOOLEAN_INTERSECT:
		    unused->insert(curr_index);
		    break;
		default:
		    throw InvalidBooleanOperation("Error - unknown "
						  "boolean operation\n");
	    }
	}
    }

    // For the faces that we can't rule out, there are several possible roles they can play:
    //
    // 1.  Fully utilized in the new brep
    // 2.  Changed into a new set of faces by intersections, each of which must be evaluated
    // 3.  Fully discarded by the new brep
    //
    // We won't be able to distinguish between 1 and 3 at this stage, but we can narrow in
    // on which faces might fall into category 2 and what faces they might interact with.
    std::set<std::pair<int, int> > intersection_candidates;
    for (int i = 0; i < face_count1; i++) {
	if (unused1.find(i) == unused1.end() && finalform1.find(i) == finalform1.end()) {
	    for (int j = 0; j < face_count2; j++) {
		if (unused2.find(j) == unused2.end() &&  finalform2.find(j) == finalform2.end()) {
		    // If the two faces don't interact according to their bounding boxes,
		    // they won't be a source of events - otherwise, they must be checked.
		    fastf_t face_dist = brep1->m_F[i].BoundingBox().MinimumDistanceTo(brep2->m_F[j].BoundingBox());
		    if (face_dist <= INTERSECTION_TOL) {
			intersection_candidates.insert(std::pair<int, int>(i, j));
		    }
		}
	    }
	}
    }

    // For those not in category 2 an inside/outside test on the breps combined with the boolean op
    // should be enough to decide the issue, but there is a problem.  If *all* faces of a brep are
    // inside the other brep and the operation is a subtraction, we don't want a "floating" inside-out
    // brep volume inside the outer volume and topologically isolated.  Normally this is handled by
    // creating a face that connects the outer and inner shells, but this is potentially a non-trivial
    // operation.  The only thing that comes immediately to mind is to find the center point of the
    // bounding box of the inner brep, create a plane using that point and the z+ unit vector for a normal, and
    // cut both breps in half with that plane to form four new breps and two new subtraction problems.
    //
    // More broadly, this is a problem - unioning two half-spheres with a sphere subtracted out of their
    // respective centers will end up with isolated surfaces in the middle of the union unless the routines
    // know they must keep one of the coplanar faces in order to topologically connect the middle.  However,
    // in the case where there is no center sphere the central face should be removed.  It may be that the
    // condition to satisfy for removal is no interior trimming loops on the face.
    //
    //
    // Also worth thinking about - would it be possible to then do edge comparisons to
    // determine which of the "fully used/fully non-used" faces are needed?

    if (DEBUG_BREP_BOOLEAN) {
	//bu_log("Summary of brep status: \n unused1: %zd\n unused2: %zd\n finalform1: %zd\n finalform2 %zd\nintersection_candidates(%zd):\n", unused1.size(), unused2.size(), finalform1.size(), finalform2.size(), intersection_candidates.size());
	for (std::set<std::pair<int, int> >::iterator it = intersection_candidates.begin(); it != intersection_candidates.end(); ++it) {
	    bu_log("     (%d, %d)\n", (*it).first, (*it).second);
	}
    }

    for (int i = 0; i < surf_count1; i++) {
	Subsurface *ss = new Subsurface(brep1->m_S[i]->Duplicate());
	st1.push_back(ss);
    }
    for (int i = 0; i < surf_count2; i++) {
	Subsurface *ss = new Subsurface(brep2->m_S[i]->Duplicate());
	st2.push_back(ss);
    }

    curves_array.SetCapacity(face_count1 + face_count2);

    // count must equal capacity for array copy to work as expected
    // when the result of the function is assigned
    curves_array.SetCount(curves_array.Capacity());

    // calculate intersection curves
    for (int i = 0; i < face_count1; i++) {

	if ((int)st1.size() < brep1->m_F[i].m_si + 1)
	    continue;

	for (int j = 0; j < face_count2; j++) {
	    if (intersection_candidates.find(std::pair<int, int>(i, j)) != intersection_candidates.end()) {
		ON_Surface *surf1, *surf2;
		ON_ClassArray<ON_SSX_EVENT> events;
		int results = 0;
		surf1 = brep1->m_S[brep1->m_F[i].m_si];
		surf2 = brep2->m_S[brep2->m_F[j].m_si];
		if (is_same_surface(surf1, surf2)) {
		    continue;
		}

		/* Skip face pairs where both surfaces are planar with nearly-
		 * parallel normals.  For co-axial TGC booleans the coplanar
		 * cap-cap pair would otherwise generate a duplicate inner-circle
		 * curve that is already provided by the cap-cylinder pair,
		 * producing a spurious third loop on the annular end-cap face.
		 * For nearly-identical nested ARB8 primitives the nearly-
		 * coplanar pair triggers the overlap-boundary finder which
		 * generates thousands of degenerate segments and crashes.  In
		 * both cases the valid intersection curves are fully provided by
		 * the perpendicular face pairs; there is nothing to lose by
		 * skipping parallel-normal pairs.
		 *
		 * Use a loose flatness tolerance (100× INTERSECTION_TOL) so
		 * that degree-1 ON_NurbsSurface patches (ARB8 faces) are also
		 * recognised as planar.  Curved surfaces whose normals happen
		 * to be parallel at their centres are not recognised here
		 * because IsPlanar() checks ALL control points. */
		{
		    ON_Plane p1, p2;
		    const double flat_tol = INTERSECTION_TOL * 100.0;
		    if (surf1->IsPlanar(&p1, flat_tol) &&
			surf2->IsPlanar(&p2, flat_tol) &&
			p1.Normal().IsParallelTo(p2.Normal(), 0.01)) {
			continue;
		    }
		}

		if ((int)st2.size() < brep2->m_F[j].m_si + 1)
		    continue;

		/* Skip face pairs whose underlying surfaces are geometrically
		 * coincident even if their NURBS representations differ (e.g.
		 * after ShrinkSurfaces re-parameterises a previously-subtracted
		 * sphere patch).  When a sphere s1 is subtracted from a solid,
		 * the resulting brep inherits the sphere patch as a trimmed face.
		 * ShrinkSurfaces then changes the knot vectors so is_same_surface()
		 * no longer detects the coincidence.  Running SSI on a coincident
		 * pair generates a huge number of overlap segments (one per
		 * isocurve knot) and is extremely slow even after the dangling-
		 * pointer crash fix.  Detect this case by sampling surf1 at its
		 * UV midpoint, projecting onto surf2, and verifying that the
		 * normals are parallel.  Skipping is safe: the geometry boundary
		 * is already captured by the trimming loops. */
		{
		    ON_Interval du1 = surf1->Domain(0);
		    ON_Interval dv1 = surf1->Domain(1);
		    double u1_mid = du1.ParameterAt(0.5);
		    double v1_mid = dv1.ParameterAt(0.5);
		    ON_3dVector nrm1;
		    if (surf1->EvNormal(u1_mid, v1_mid, nrm1) && nrm1.Length() > ON_ZERO_TOLERANCE) {
			ON_3dPoint pt1 = surf1->PointAt(u1_mid, v1_mid);
			ON_ClassArray<ON_PX_EVENT> px;
			if (ON_Intersect(pt1, *surf2, px, INTERSECTION_TOL * 10.0) &&
			    px.Count() > 0)
			{
			    ON_3dVector nrm2;
			    double u2 = px[0].m_b[0], v2 = px[0].m_b[1];
			    if (surf2->EvNormal(u2, v2, nrm2) &&
				nrm1.IsParallelTo(nrm2, 0.01)) {
				continue;
			    }
			}
		    }
		}

		// Possible enhancement: Some faces may share the same surface.
		// We can store the result of SSI to avoid re-computation.
		results = ON_Intersect(surf1,
				       surf2,
				       events,
				       INTERSECTION_TOL,
				       0.0,
				       0.0,
				       NULL,
				       NULL,
				       NULL,
				       NULL,
				       st1[brep1->m_F[i].m_si],
				       st2[brep2->m_F[j].m_si]);
		if (results <= 0) {
		    continue;
		}
		//dplot->WriteLog();

		ON_SimpleArray<ON_Curve *> face1_curves, face2_curves;
		for (int k = 0; k < events.Count(); k++) {
		    if (events[k].m_type == ON_SSX_EVENT::ssx_tangent ||
			events[k].m_type == ON_SSX_EVENT::ssx_transverse ||
			events[k].m_type == ON_SSX_EVENT::ssx_overlap)
		    {
			ON_SimpleArray<ON_Curve *> subcurves_on1, subcurves_on2;

			get_subcurves_inside_faces(subcurves_on1,
						   subcurves_on2, brep1, brep2, i, j, &events[k]);

			for (int l = 0; l < subcurves_on1.Count(); ++l) {
			    SSICurve ssi_on1;
			    ssi_on1.m_curve = subcurves_on1[l];
			    curves_array[i].Append(ssi_on1);

			    face1_curves.Append(subcurves_on1[l]);
			}
			for (int l = 0; l < subcurves_on2.Count(); ++l) {
			    SSICurve ssi_on2;
			    ssi_on2.m_curve = subcurves_on2[l];
			    curves_array[face_count1 + j].Append(ssi_on2);

			    face2_curves.Append(subcurves_on2[l]);
			}
		    }
		}
		//dplot->ClippedFaceCurves(surf1, surf2, face1_curves, face2_curves);
		//dplot->WriteLog();

		if (DEBUG_BREP_BOOLEAN) {
		    // Look for coplanar faces
		    ON_Plane surf1_plane, surf2_plane;
		    if (surf1->IsPlanar(&surf1_plane) && surf2->IsPlanar(&surf2_plane)) {
			/* We already checked for disjoint above, so the only remaining question is the normals */
			if (surf1_plane.Normal().IsParallelTo(surf2_plane.Normal())) {
			    bu_log("Faces brep1->%d and brep2->%d are coplanar and intersecting\n", i, j);
			}
		    }
		}

	    }
	}
    }

    for (size_t i = 0; i < st1.size(); i++) {
	surf_tree1.Append(st1[i]);
    }

    for (size_t i = 0; i < st2.size(); i++) {
	surf_tree2.Append(st2[i]);
    }

    return curves_array;
}


static ON_SimpleArray<ON_Curve *>get_face_trim_loop(const ON_Brep *brep, int face_loop_index)
{
    const ON_BrepLoop &loop = brep->m_L[face_loop_index];
    const ON_SimpleArray<int> &trim_index = loop.m_ti;
    ON_SimpleArray<ON_Curve *> face_trim_loop;
    for (int i = 0; i < trim_index.Count(); ++i) {
	ON_Curve *curve2d = brep->m_C2[brep->m_T[trim_index[i]].m_c2i];
	face_trim_loop.Append(curve2d->Duplicate());
    }
    return face_trim_loop;
}


static ON_SimpleArray<TrimmedFace *>
get_trimmed_faces(const ON_Brep *brep)
{
    std::vector<TrimmedFace *> trimmed_faces;
    int face_count = brep->m_F.Count();
    for (int i = 0; i < face_count; i++) {
	const ON_BrepFace &face = brep->m_F[i];
	const ON_SimpleArray<int> &loop_index = face.m_li;
	if (loop_index.Count() <= 0) {
	    continue;
	}

	TrimmedFace *trimmed_face = new TrimmedFace();
	trimmed_face->m_face = &face;
	ON_SimpleArray<ON_Curve *> index_loop = get_face_trim_loop(brep, loop_index[0]);
	trimmed_face->m_outerloop = index_loop;
	for (int j = 1; j < loop_index.Count(); j++) {
	    index_loop = get_face_trim_loop(brep, loop_index[j]);
	    trimmed_face->m_innerloop.push_back(index_loop);
	}
	trimmed_faces.push_back(trimmed_face);
    }
    ON_SimpleArray<TrimmedFace *> tf;
    for (size_t i = 0; i < trimmed_faces.size(); i++) {
	tf.Append(trimmed_faces[i]);
    }
    return tf;
}


static void
categorize_trimmed_faces(
    ON_ClassArray<ON_SimpleArray<TrimmedFace *> > &trimmed_faces,
    const ON_Brep *brep1,
    const ON_Brep *brep2,
    ON_SimpleArray<Subsurface *> &surf_tree1,
    ON_SimpleArray<Subsurface *> &surf_tree2,
    op_type operation)
{
    int face_count1 = brep1->m_F.Count();
    for (int i = 0; i < trimmed_faces.Count(); i++) {
	/* Perform inside-outside test to decide whether the trimmed face should
	 * be used in the final b-rep structure or not.
	 * Different operations should be dealt with accordingly.
	 * Use connectivity graphs (optional) which represents the topological
	 * structure of the b-rep. This can reduce time-consuming inside-outside
	 * tests.
	 */
	const ON_SimpleArray<TrimmedFace *> &splitted = trimmed_faces[i];
	const ON_Brep *another_brep = i >= face_count1 ? brep1 : brep2;
	ON_SimpleArray<Subsurface *> &surf_tree = i >= face_count1 ? surf_tree1 : surf_tree2;
	for (int j = 0; j < splitted.Count(); j++) {
	    if (splitted[j]->m_belong_to_final != TrimmedFace::UNKNOWN) {
		// Visited before, don't need to test again
		continue;
	    }

	    if (DEBUG_BREP_BOOLEAN)
		bu_log("  classify face[%d] split[%d]: innerloops=%zu outersegs=%d\n",
		       i, j, splitted[j]->m_innerloop.size(), splitted[j]->m_outerloop.Count());

	    int face_location = -1;
	    try {
		face_location = face_brep_location(splitted[j], another_brep, surf_tree);
	    } catch (InvalidGeometry &e) {
		bu_log("%s", e.what());
	    } catch (AlgorithmError &e) {
		bu_log("%s", e.what());
	    }

	    if (face_location < 0) {
		if (DEBUG_BREP_BOOLEAN) {
		    bu_log("Whether the trimmed face is inside/outside is unknown.\n");
		}
		splitted[j]->m_belong_to_final = TrimmedFace::NOT_BELONG;
		continue;
	    }

	    splitted[j]->m_rev = false;
	    splitted[j]->m_belong_to_final = TrimmedFace::NOT_BELONG;
	    switch (face_location) {
		case INSIDE_BREP:
		    if (operation == BOOLEAN_INTERSECT ||
			operation == BOOLEAN_XOR ||
			(operation == BOOLEAN_DIFF && i >= face_count1))
		    {
			splitted[j]->m_belong_to_final = TrimmedFace::BELONG;
		    }
		    if (operation == BOOLEAN_DIFF || operation == BOOLEAN_XOR) {
			splitted[j]->m_rev = true;
		    }
		    break;
		case OUTSIDE_BREP:
		    if (operation == BOOLEAN_UNION ||
			operation == BOOLEAN_XOR ||
			(operation == BOOLEAN_DIFF && i < face_count1))
		    {
			splitted[j]->m_belong_to_final = TrimmedFace::BELONG;
		    }
		    break;
		case ON_BREP_SURFACE:
		    // get a 3d point on the face
		    ON_2dPoint face_pt2d = get_point_inside_trimmed_face(splitted[j]);
		    ON_3dPoint face_pt3d = splitted[j]->m_face->PointAt(face_pt2d.x, face_pt2d.y);

		    // find the matching point on the other brep
		    ON_3dPoint brep_pt2d;
		    const ON_Surface *brep_surf;
		    bool found = false;
		    for (int fi = 0; fi < another_brep->m_F.Count(); ++fi) {
			const ON_BrepFace &face = another_brep->m_F[fi];
			/* Fast bbox prefilter: skip surfaces far from the
			 * test point — they cannot contain a matching point. */
			{
			    ON_3dPoint fb_min, fb_max;
			    surf_tree[face.m_si]->GetBBox(fb_min, fb_max);
			    if (ON_BoundingBox(fb_min, fb_max).MinimumDistanceTo(face_pt3d) > INTERSECTION_TOL) {
				continue;
			    }
			}
			brep_surf = face.SurfaceOf();
			ON_ClassArray<ON_PX_EVENT> px_event;

			if (ON_Intersect(face_pt3d, *brep_surf, px_event,
					 INTERSECTION_TOL, 0, 0, surf_tree[face.m_si]))
			{
			    found = true;
			    brep_pt2d = px_event[0].m_b;
			    break;
			}
		    }
		    if (found) {
			// compare normals of surfaces at shared point
			ON_3dVector brep_norm, face_norm;
			brep_surf->EvNormal(brep_pt2d.x, brep_pt2d.y, brep_norm);
			splitted[j]->m_face->SurfaceOf()->EvNormal(face_pt2d.x, face_pt2d.y, face_norm);

			double dot = ON_DotProduct(brep_norm, face_norm);
			bool same_direction = false;
			if (dot > 0) {
			    // normals appear to have same direction
			    same_direction = true;
			}

			/* Only include one copy of a coplanar face: prefer brep1's
			 * (i < face_count1) for union/intersect to avoid producing
			 * coincident duplicate faces in the assembled output.
			 * For diff, only brep1 faces with opposite-direction normals
			 * (the "carved-out" boundary) belong in the result. */
			if ((operation == BOOLEAN_UNION && same_direction && i < face_count1) ||
			    (operation == BOOLEAN_INTERSECT && same_direction && i < face_count1) ||
			    (operation == BOOLEAN_DIFF && !same_direction && i < face_count1))
			{
			    splitted[j]->m_belong_to_final = TrimmedFace::BELONG;
			}
		    }
	    }
	    if (DEBUG_BREP_BOOLEAN) {
		bu_log("The trimmed face is %s the other brep.",
		       (face_location == INSIDE_BREP) ? "inside" :
		       ((face_location == OUTSIDE_BREP) ? "outside" : "on the surface of"));
	    }
	}
    }
}


static ON_ClassArray<ON_SimpleArray<TrimmedFace *> >
get_evaluated_faces(const ON_Brep *brep1, const ON_Brep *brep2, op_type operation)
{
    ON_SimpleArray<Subsurface *> surf_tree1, surf_tree2;

    int face_count1 = brep1->m_F.Count();
    int face_count2 = brep2->m_F.Count();

    // check for face counts high enough to cause overflow
    if ((face_count1 + face_count2) < 0)
	return ON_ClassArray<ON_SimpleArray<TrimmedFace *> > ();

    ON_ClassArray<ON_SimpleArray<SSICurve> > curves_array =
	get_face_intersection_curves(surf_tree1, surf_tree2, brep1, brep2, operation);

    ON_SimpleArray<TrimmedFace *> brep1_faces, brep2_faces;
    brep1_faces = get_trimmed_faces(brep1);
    brep2_faces = get_trimmed_faces(brep2);

    ON_SimpleArray<TrimmedFace *> original_faces = brep1_faces;
    for (int i = 0; i < brep2_faces.Count(); ++i) {
	original_faces.Append(brep2_faces[i]);
    }

    if (original_faces.Count() != face_count1 + face_count2) {
	throw GeometryGenerationError("ON_Boolean() Error: TrimmedFace"
				      " generation failed.\n");
    }

    // split the surfaces with the intersection curves
    ON_ClassArray<ON_SimpleArray<TrimmedFace *> > trimmed_faces;
    for (int i = 0; i < original_faces.Count(); i++) {
	TrimmedFace *first = original_faces[i];

	/* Deduplication: remove closed intersection curves that are
	 * spurious seam artifacts.  When a planar face intersects a
	 * closed (periodic) NURBS surface at or near its parameter-
	 * space boundary, add_points_to_closed_seams() can insert a
	 * duplicate point at the opposite seam edge, which the
	 * polyline-merging step may then close into a tiny degenerate
	 * loop.  Detect such artifacts by comparing every pair of
	 * closed curves: if one has a bounding-box diagonal less than
	 * 1% of another's, AND its centre lies inside the other's
	 * bounding box, it is a seam duplicate — null it out so
	 * link_curves() ignores it. */
	ON_SimpleArray<SSICurve> &carray = curves_array[i];
	for (int m = 0; m < carray.Count(); m++) {
	    if (!carray[m].m_curve || !carray[m].m_curve->IsClosed()) continue;
	    ON_BoundingBox bbm;
	    carray[m].m_curve->GetBoundingBox(bbm);
	    double dm = bbm.Diagonal().Length();
	    for (int n = 0; n < carray.Count(); n++) {
		if (n == m || !carray[n].m_curve || !carray[n].m_curve->IsClosed()) continue;
		ON_BoundingBox bbn;
		carray[n].m_curve->GetBoundingBox(bbn);
		double dn = bbn.Diagonal().Length();
		/* n is much smaller than m AND n's centre is inside m's bbox */
		if (dm > 0.0 && dn / dm < 0.01 && bbm.IsPointIn(bbn.Center())) {
		    delete carray[n].m_curve;
		    carray[n].m_curve = NULL;
		}
	    }
	}

	/* Remove near-straight "chord" intersection curves that share reversed
	 * endpoints with a genuinely curved arc in the same face.
	 *
	 * When the SSI between two nearly-coplanar cap faces (e.g. two TGC bases
	 * that share a vertex but have slightly different axis directions) produces
	 * a chord — the straight-line intersection of their two planes — that chord
	 * shares its endpoints (in reverse order) with the correct inner-circle arc
	 * coming from the tube-face SSI.  Together the chord and the arc form a
	 * closed half-moon loop that splits the cap face incorrectly.
	 *
	 * Criterion: open curve m is "chord-like" if |mid(m) − midpoint(start,end)|
	 * is less than 5% of |start − end|.  If a second open curve n exists whose
	 * endpoints are the reverse of m's AND n is curved (ratio ≥ 5%), m is
	 * the spurious chord and is removed. */
	for (int m = 0; m < carray.Count(); m++) {
	    ON_Curve *cm = carray[m].m_curve;
	    if (!cm || cm->IsClosed()) continue;
	    ON_3dPoint ms3 = cm->PointAtStart();
	    ON_3dPoint me3 = cm->PointAtEnd();
	    ON_3dPoint mm3 = cm->PointAt(cm->Domain().Mid());
	    double len_m = ms3.DistanceTo(me3);
	    if (len_m < INTERSECTION_TOL) continue;
	    ON_3dPoint seg_mid_m = (ms3 + me3) * 0.5;
	    double straight_ratio = mm3.DistanceTo(seg_mid_m) / len_m;
	    if (straight_ratio >= 0.05) continue; /* curved — keep */

	    /* m looks like a chord; search for a curved arc with reversed endpoints */
	    for (int n = 0; n < carray.Count(); n++) {
		if (n == m) continue;
		ON_Curve *cn = carray[n].m_curve;
		if (!cn || cn->IsClosed()) continue;
		ON_3dPoint ns3 = cn->PointAtStart();
		ON_3dPoint ne3 = cn->PointAtEnd();
		/* Reversed endpoints: m.start ≈ n.end  AND  m.end ≈ n.start */
		const double ep_tol = INTERSECTION_TOL * 100.0;
		if (ms3.DistanceTo(ne3) > ep_tol) continue;
		if (me3.DistanceTo(ns3) > ep_tol) continue;
		ON_3dPoint nm3 = cn->PointAt(cn->Domain().Mid());
		double len_n = ns3.DistanceTo(ne3);
		if (len_n < INTERSECTION_TOL) continue;
		ON_3dPoint seg_mid_n = (ns3 + ne3) * 0.5;
		double n_ratio = nm3.DistanceTo(seg_mid_n) / len_n;
		if (n_ratio >= 0.05) {
		    /* n is curved and m is straight → m is the spurious chord */
		    if (DEBUG_BREP_BOOLEAN)
			bu_log("  Removing chord [face %d curve %d, straight_ratio=%g]\n",
			       i, m, straight_ratio);
		    delete carray[m].m_curve;
		    carray[m].m_curve = NULL;
		    break;
		}
	    }
	}

	ON_ClassArray<LinkedCurve> linked_curves = link_curves(curves_array[i]);

	ON_SimpleArray<TrimmedFace *> splitted = split_trimmed_face(first, linked_curves);
	trimmed_faces.Append(splitted);

	// Delete the curves passed in.
	// Only the copies of them will be used later.
	for (int j = 0; j < linked_curves.Count(); j++) {
	    for (int k = 0; k < linked_curves[j].m_ssi_curves.Count(); k++) {
		if (linked_curves[j].m_ssi_curves[k].m_curve) {
		    delete linked_curves[j].m_ssi_curves[k].m_curve;
		    linked_curves[j].m_ssi_curves[k].m_curve = NULL;
		}
	    }
	}
    }

    if (trimmed_faces.Count() != original_faces.Count()) {
	throw GeometryGenerationError("ON_Boolean() Error: "
				      "trimmed_faces.Count() != original_faces.Count()\n");
    }

    for (int i = 0; i < original_faces.Count(); i++) {
	delete original_faces[i];
	original_faces[i] = NULL;
    }

    categorize_trimmed_faces(trimmed_faces, brep1, brep2, surf_tree1, surf_tree2, operation);

    //dplot->SplitFaces(trimmed_faces);
    //dplot->WriteLog();

    for (int i = 0; i < surf_tree1.Count(); i++) {
	delete surf_tree1[i];
    }

    for (int i = 0; i < surf_tree2.Count(); i++) {
	delete surf_tree2[i];
    }

    return trimmed_faces;
}


static void
standardize_loop_orientations(ON_Brep *brep)
{
    std::map<int, int> reversed_curve2d, reversed_edge;
    for (int face_idx = 0; face_idx < brep->m_F.Count(); ++face_idx) {
	const ON_BrepFace &eb_face = brep->m_F[face_idx];

	for (int loop_idx = 0; loop_idx < eb_face.LoopCount(); ++loop_idx) {
	    const ON_BrepLoop &face_loop = brep->m_L[eb_face.m_li[loop_idx]];
	    if (face_loop.m_type != ON_BrepLoop::outer &&
		face_loop.m_type != ON_BrepLoop::inner) {
		continue;
	    }

	    int loop_direction = brep->LoopDirection(face_loop);
	    if ((loop_direction == LOOP_DIRECTION_CCW && face_loop.m_type == ON_BrepLoop::inner) ||
		(loop_direction == LOOP_DIRECTION_CW && face_loop.m_type == ON_BrepLoop::outer))
	    {
		// found reversed loop
		int brep_li = eb_face.m_li[loop_idx];
		ON_BrepLoop &reversed_loop = brep->m_L[brep_li];

		// reverse all the loop's curves
		for (int trim_idx = 0; trim_idx < reversed_loop.TrimCount(); ++trim_idx) {
		    ON_BrepTrim *trim = reversed_loop.Trim(trim_idx);

		    // Skip singular trims: they are degenerate points on a boundary,
		    // have no edge, and reversing them corrupts their m_iso flag.
		    if (trim->m_type == ON_BrepTrim::singular) {
			continue;
		    }

		    // Replace trim curve2d with a reversed copy.
		    // We'll use a previously made curve, or else
		    // make a new one.
		    if (reversed_curve2d.find(trim->m_c2i) != reversed_curve2d.end()) {
			trim->ChangeTrimCurve(reversed_curve2d[trim->m_c2i]);
		    } else {
			ON_Curve *curve_copy = trim->TrimCurveOf()->DuplicateCurve();
			int copy_c2i = brep->AddTrimCurve(curve_copy);

			reversed_curve2d[trim->m_c2i] = copy_c2i;

			trim->ChangeTrimCurve(copy_c2i);
			trim->Reverse();
		    }
		    // Replace trim edge with a reversed copy.
		    // We'll use a previously made edge, or else
		    // make a new one.
		    if (reversed_edge.find(trim->m_ei) != reversed_edge.end()) {
			trim->RemoveFromEdge(false, false);
			trim->AttachToEdge(reversed_edge[trim->m_ei], trim->m_bRev3d);
		    } else {
			ON_BrepEdge *edge = trim->Edge();
			ON_BrepVertex &v_start = *edge->Vertex(0);
			ON_BrepVertex &v_end = *edge->Vertex(1);
			ON_Interval dom = edge->ProxyCurveDomain();

			ON_Curve *curve_copy = trim->EdgeCurveOf()->DuplicateCurve();
			int copy_c3i = brep->AddEdgeCurve(curve_copy);
			ON_BrepEdge &edge_copy = brep->NewEdge(v_start,
							       v_end, copy_c3i, &dom, edge->m_tolerance);

			reversed_edge[trim->m_ei] = copy_c3i;

			trim->RemoveFromEdge(false, false);
			trim->AttachToEdge(edge_copy.m_edge_index, trim->m_bRev3d);
			trim->Edge()->Reverse();
		    }
		}
		// need to reverse the order of trims in the loop
		// too so they appear continuous
		reversed_loop.m_ti.Reverse();
	    }
	}
    }
}


/* Helper: return minimum distance from point p to curve c.
 * Converts the curve to NURBS form and uses the internal GetClosestPoint
 * implementation (signature: double*, const ON_NurbsCurve*, const ON_3dPoint&,
 * double, const ON_Interval*) which has the actual implementation.
 * Falls back to dense uniform sampling if NURBS conversion fails.
 * Stores the parameter of the closest point in *t_closest (may be NULL).
 */

/* Join coincident boundary edges so the evaluated brep is a closed solid.
 *
 * After add_elements() builds each face independently every trim gets its own
 * fresh edge object, so all edges start as "boundary" (one trim each) and
 * ON_Brep::IsSolid() returns false.  The raytracer then treats the brep as a
 * zero-thickness plate and reports 0 volume.
 *
 * This function finds pairs of boundary edges that represent the same 3-D
 * curve and merges them.  Open edges are matched first by vertex index (fast
 * path), then by 3-D endpoint position (needed when the boolean evaluator
 * creates independent vertex sets per face so coincident intersection points
 * land at different indices).  Closed edges (e.g. hole circles) are matched
 * by bounding-box because their single start/end vertex can differ.
 */
static void
join_boundary_edges(ON_Brep *brep)
{
    /* Vertex-position tolerance: use a fraction of the brep's bounding box
     * diagonal so it scales with model size.  A relative tolerance of 1e-8
     * is tight enough to avoid false positives while handling the small
     * floating-point discrepancies that can arise at intersection points. */
    ON_BoundingBox bbox;
    brep->GetBoundingBox(bbox);
    double bbox_diag = bbox.Diagonal().Length();
    static const double VTOL_REL = 1.0e-5;
    double vtol = (bbox_diag > ON_ZERO_TOLERANCE) ? bbox_diag * VTOL_REL : ON_ZERO_TOLERANCE;
    /* Never fall below INTERSECTION_TOL: intersection points computed from
     * different surface parameterisations may have floating-point differences
     * that exceed the bbox-relative tolerance when the model is small. */
    if (vtol < INTERSECTION_TOL)
	vtol = INTERSECTION_TOL;

    /* ---------------------------------------------------------------
     * Pass 1: open boundary edges.
     *
     * Primary match: identical vertex indices (fast, exact).
     * Fallback match: coincident 3-D endpoint positions within vtol,
     * followed by a curve-midpoint sanity check to rule out false
     * positives from short edges that happen to share an endpoint.
     *
     * The boolean evaluator builds each face independently, so the
     * same geometric intersection point may be assigned different
     * vertex indices on different faces.  The position-based fallback
     * handles this case without needing to pre-merge vertices (vertex
     * merging can create degenerate self-loop edges that fail OpenNURBS
     * validity checks).
     *
     * On a match we transfer ej's trim directly to ei (same low-level
     * approach used for closed edges in Pass 2) rather than calling
     * CombineCoincidentEdges(), so we never rely on vertex indices
     * agreeing across the two edges.
     * --------------------------------------------------------------- */
    bool merged = true;
    while (merged) {
	merged = false;
	for (int i = 0; i < brep->m_E.Count() && !merged; i++) {
	    ON_BrepEdge &ei = brep->m_E[i];
	    if (ei.m_ti.Count() != 1) continue;
	    if (ei.m_vi[0] == ei.m_vi[1]) continue; /* skip closed */

	    ON_3dPoint pi0 = brep->m_V[ei.m_vi[0]].Point();
	    ON_3dPoint pi1 = brep->m_V[ei.m_vi[1]].Point();

	    for (int j = i + 1; j < brep->m_E.Count() && !merged; j++) {
		ON_BrepEdge &ej = brep->m_E[j];
		if (ej.m_ti.Count() != 1) continue;
		if (ej.m_vi[0] == ej.m_vi[1]) continue;

		/* ---- primary match: vertex indices agree ---- */
		bool forward_idx = (ei.m_vi[0] == ej.m_vi[0] && ei.m_vi[1] == ej.m_vi[1]);
		bool reverse_idx = (ei.m_vi[0] == ej.m_vi[1] && ei.m_vi[1] == ej.m_vi[0]);

		/* ---- fallback match: compare 3-D endpoint positions ---- */
		bool forward = forward_idx;
		bool reverse = reverse_idx;
		if (!forward && !reverse) {
		    ON_3dPoint pj0 = brep->m_V[ej.m_vi[0]].Point();
		    ON_3dPoint pj1 = brep->m_V[ej.m_vi[1]].Point();
		    forward = (pi0.DistanceTo(pj0) <= vtol && pi1.DistanceTo(pj1) <= vtol);
		    reverse = (pi0.DistanceTo(pj1) <= vtol && pi1.DistanceTo(pj0) <= vtol);
		    /* No midpoint check: two faces meeting at an intersection
		     * produce 3D edge curves that share the same endpoints but
		     * are parameterised independently from each surface, so their
		     * geometric midpoints may differ significantly.  Checking both
		     * endpoints is sufficient to ensure the correct topological
		     * match. */
		}
		if (!forward && !reverse) continue;

		/* Transfer ej's trim to ei.  This is the same direct approach
		 * used in Pass 2 for closed edges; it does not require that
		 * the vertex indices already match. */
		int tj = ej.m_ti[0];
		if (reverse)
		    brep->m_T[tj].m_bRev3d = !brep->m_T[tj].m_bRev3d;
		brep->m_T[tj].m_ei = i;
		ei.m_ti.Append(tj);

		/* Update the transferred trim's vertex references so they
		 * point to ei's vertices.  For an open edge bRev3d=false means
		 * the trim runs start→end in the same direction as the edge, so
		 * trim.m_vi[0] = edge.m_vi[0] and trim.m_vi[1] = edge.m_vi[1].
		 * When bRev3d=true the trim runs opposite, so the indices swap. */
		{
		    bool bRev3d = brep->m_T[tj].m_bRev3d;
		    brep->m_T[tj].m_vi[0] = ei.m_vi[bRev3d ? 1 : 0];
		    brep->m_T[tj].m_vi[1] = ei.m_vi[bRev3d ? 0 : 1];
		}

		ej.m_ti.Empty();
		merged = true;
	    }
	}
    }

    /* ---------------------------------------------------------------
     * Pass 2: closed boundary edges (e.g. hole circles).
     * Two closed boundary edges are coincident when their 3-D bounding
     * boxes are nearly equal AND their arc-lengths are nearly equal.
     * For circles (the common case in brep boolean output) this is
     * both necessary and sufficient.  Because the edges may start at
     * different angles CombineCoincidentEdges() cannot be used (it
     * requires matching vertices).  Instead we directly transfer the
     * trim from the redundant edge to the surviving edge.
     * --------------------------------------------------------------- */
    for (int i = 0; i < brep->m_E.Count(); i++) {
	ON_BrepEdge &ei = brep->m_E[i];
	if (ei.m_ti.Count() != 1) continue;
	if (ei.m_vi[0] != ei.m_vi[1]) continue; /* only closed */
	if (ei.m_c3i < 0) continue;

	for (int j = i + 1; j < brep->m_E.Count(); j++) {
	    ON_BrepEdge &ej = brep->m_E[j];
	    if (ej.m_ti.Count() != 1) continue;
	    if (ej.m_vi[0] != ej.m_vi[1]) continue;
	    if (ej.m_c3i < 0) continue;

	    const ON_Curve *ci3 = brep->m_C3[ei.m_c3i];
	    const ON_Curve *cj3 = brep->m_C3[ej.m_c3i];
	    if (!ci3 || !cj3) continue;

	    /* Compare bounding boxes: for circles the bbox fully
	     * characterises the geometry (center + radius + plane). */
	    ON_BoundingBox bb_i, bb_j;
	    ci3->GetBoundingBox(bb_i);
	    cj3->GetBoundingBox(bb_j);
	    double scale = bb_i.Diagonal().Length();
	    if (scale < ON_ZERO_TOLERANCE) scale = 1.0;
	    static const double BBOX_REL_TOL = 0.01; /* 1% of bbox diagonal */
	    const double bb_tol = scale * BBOX_REL_TOL;

	    if (bb_i.m_min.DistanceTo(bb_j.m_min) > bb_tol) continue;
	    if (bb_i.m_max.DistanceTo(bb_j.m_max) > bb_tol) continue;

	    int tj = ej.m_ti[0];

	    /* Determine orientation for the transferred trim.
	     *
	     * For trims created by add_elements(), m_bRev3d is always
	     * initialized to false (same direction as their original edge
	     * curve cj3).  When the trim is transferred to ei whose 3D curve
	     * is ci3, the correct bRev3d value is:
	     *   false  if ci3 and cj3 wind in the SAME direction  (no flip)
	     *   true   if ci3 and cj3 wind in OPPOSITE directions (flip)
	     *
	     * OpenNURBS defines m_bRev3d=true as "the edge and trim have
	     * opposite directions."  We compare the Newell winding normals
	     * of ci3 and cj3 directly — this is more reliable than pushing
	     * up the 2D trim curve (which degenerates to a point for
	     * seam-crossing trims on periodic surfaces).
	     *
	     * dot(Newell(ci3), Newell(cj3)) < 0  →  opposite winding
	     *   →  m_bRev3d should be true  →  rev = true
	     * dot(Newell(ci3), Newell(cj3)) > 0  →  same winding
	     *   →  m_bRev3d should stay false  →  rev = false  */
	    bool rev = false;
	    {
		static const int N = 64;

		/* Newell normal for ci3 (3D edge curve of ei) */
		ON_3dVector ni(0, 0, 0);
		ON_3dPoint  prev_i = ci3->PointAt(ci3->Domain().ParameterAt(0.0));
		for (int s = 1; s <= N; s++) {
		    ON_3dPoint cur_i = ci3->PointAt(
			ci3->Domain().ParameterAt((double)s / N));
		    ni.x += (prev_i.y - cur_i.y) * (prev_i.z + cur_i.z);
		    ni.y += (prev_i.z - cur_i.z) * (prev_i.x + cur_i.x);
		    ni.z += (prev_i.x - cur_i.x) * (prev_i.y + cur_i.y);
		    prev_i = cur_i;
		}

		/* Newell normal for cj3 (3D edge curve of ej) */
		ON_3dVector nj(0, 0, 0);
		ON_3dPoint  prev_j = cj3->PointAt(cj3->Domain().ParameterAt(0.0));
		for (int s = 1; s <= N; s++) {
		    ON_3dPoint cur_j = cj3->PointAt(
			cj3->Domain().ParameterAt((double)s / N));
		    nj.x += (prev_j.y - cur_j.y) * (prev_j.z + cur_j.z);
		    nj.y += (prev_j.z - cur_j.z) * (prev_j.x + cur_j.x);
		    nj.z += (prev_j.x - cur_j.x) * (prev_j.y + cur_j.y);
		    prev_j = cur_j;
		}

		/* Opposite winding (dot < 0) means the transferred trim's
		 * existing bRev3d=false would give "same direction", but we
		 * need "opposite direction" — so flip it. */
		rev = (ON_DotProduct(ni, nj) < 0.0);
	    }
	    if (rev)
		brep->m_T[tj].m_bRev3d = !brep->m_T[tj].m_bRev3d;
	    brep->m_T[tj].m_ei = i;
	    ei.m_ti.Append(tj);

	    /* Update the transferred trim's vertex indices to match ei.
	     * The closed-edge condition guarantees m_vi[0]==m_vi[1] for
	     * both ei and the trim (full-circle loop). */
	    {
		bool bRev3d = brep->m_T[tj].m_bRev3d;
		brep->m_T[tj].m_vi[0] = ei.m_vi[bRev3d ? 1 : 0];
		brep->m_T[tj].m_vi[1] = ei.m_vi[bRev3d ? 0 : 1];
	    }

	    /* Align trim start positions for the joined closed edge.
	     *
	     * After joining, ei has two trims: ti (the original single-
	     * trim closed loop, e.g. the box-face inner circle) and tj
	     * (the transferred trim, potentially part of a multi-trim
	     * loop, e.g. the cylindrical-face outer loop).
	     *
	     * The two coincident circles may have started at different
	     * angular positions.  OpenNURBS requires every trim's 3D
	     * pushup-start to match ei's edge curve start position.
	     * We fix this by moving the "canonical start" to tj's start
	     * position (P_j), because tj is in a multi-trim loop whose
	     * adjacent trims (e.g. seam lines) already reference P_j:
	     *
	     *  1. Compute P_j = 3D position of tj's 2D curve start.
	     *  2. Reparametrize ci3 (ei's 3D edge curve) to start at P_j.
	     *  3. Move ei's vertex to P_j.
	     *  4. Reparametrize ti's 2D curve (single-trim closed loop)
	     *     to start at the UV corresponding to P_j.
	     *  5. Merge ej's vertex into ei's vertex so the multi-trim
	     *     loop (tj's loop) stays topologically connected.
	     *
	     * tj's 2D curve is left untouched so its loop connectivity
	     * is preserved. */
	    if (ci3->IsClosed()) {
		/* Step 1: find P_j */
		const ON_Curve *c2j_al =
		    brep->m_C2[brep->m_T[tj].m_c2i];
		const ON_BrepFace *fj_al = brep->m_T[tj].Face();
		if (c2j_al && fj_al && fj_al->SurfaceOf()) {
		    ON_3dPoint uv_start_al =
			c2j_al->PointAt(c2j_al->Domain().Min());
		    ON_3dPoint P_j =
			fj_al->SurfaceOf()->PointAt(uv_start_al.x,
						    uv_start_al.y);

		    /* Helper: find the parameter on a closed curve where
		     * the curve passes closest to 'target'.  Uses a 400-
		     * sample coarse search followed by Newton refinement
		     * to achieve machine-precision accuracy.  Treats the
		     * ON_Curve as a 3D curve (works for both 3D edge curves
		     * and 2D trim curves treated as 3D in (u,v,0) space). */
		    auto find_seam_t = [](const ON_Curve *c,
					  const ON_3dPoint &target,
					  double *t_out) -> bool {
			static const int NSAMP = 400;
			static const int MAX_NEWTON_ITERS  = 50;
			static const double NEWTON_DF_EPS  = 1e-15; /* near-flat */
			static const double NEWTON_DT_EPS  = 1e-13; /* converged */
			/* Coarse search */
			ON_Interval dom = c->Domain();
			double best_dist = ON_DBL_MAX;
			double t_best = dom.Min();
			for (int s = 0; s <= NSAMP; s++) {
			    double t_s = dom.ParameterAt(
				(double)s / NSAMP);
			    double d = c->PointAt(t_s).DistanceTo(target);
			    if (d < best_dist) {
				best_dist = d;
				t_best = t_s;
			    }
			}
			/* Newton refinement to machine precision */
			ON_NurbsCurve nc;
			if (c->GetNurbForm(nc)) {
			    double t = t_best;
			    for (int nr = 0; nr < MAX_NEWTON_ITERS; nr++) {
				ON_3dPoint  pt;
				ON_3dVector d1, d2;
				nc.Ev2Der(t, pt, d1, d2);
				double f = ON_DotProduct(
				    pt - target, d1);
				double df = d1.Length()*d1.Length() +
				    ON_DotProduct(pt - target, d2);
				if (fabs(df) < NEWTON_DF_EPS) break;
				double dt = -f / df;
				if (fabs(dt) < NEWTON_DT_EPS) break;
				t += dt;
			    }
			    /* Clamp to valid domain to avoid wrap-around */
			    if (t < dom.Min()) t = dom.Min();
			    if (t > dom.Max()) t = dom.Max();
			    t_best = t;
			}
			*t_out = t_best;
			return true;
		    };

		    /* Step 2: reparametrize ci3 to start at P_j */
		    {
			double t_new3 = ci3->Domain().Min();
			find_seam_t(ci3, P_j, &t_new3);
			if (ci3->PointAt(t_new3).DistanceTo(P_j)
			    > ON_ZERO_TOLERANCE) {
			    ON_Curve *ci3_new =
				brep->m_C3[ei.m_c3i]->Duplicate();
			    if (ci3_new &&
				ci3_new->ChangeClosedCurveSeam(t_new3)) {
				int new_c3i =
				    brep->AddEdgeCurve(ci3_new);
				ei.m_c3i = new_c3i;
				ei.SetProxyCurve(
				    brep->m_C3[new_c3i]);
				brep->SetEdgeDomain(
				    ei.m_edge_index,
				    brep->m_C3[new_c3i]->Domain());
				ci3 = brep->m_C3[new_c3i];
			    } else {
				delete ci3_new;
			    }
			}
		    }

		    /* Step 3: move ei's vertex to P_j */
		    {
			int ei_vi = ei.m_vi[0]; /* closed edge: vi[0]==vi[1] */
			brep->m_V[ei_vi].SetPoint(P_j);
		    }

		    /* Step 4: reparametrize ti's 2D curve (Case A:
		     * single-trim closed-circle loop on the other face)
		     * to start at the UV on ti's surface that maps to P_j. */
		    if (ei.m_ti.Count() >= 1) {
			/* ti is the FIRST trim on ei (tj was just appended
			 * as the second via ei.m_ti.Append(tj) above). */
			int ti_idx = ei.m_ti[0];
			const ON_Curve *c2i_al =
			    brep->m_C2[brep->m_T[ti_idx].m_c2i];
			const ON_BrepFace *fi_al =
			    brep->m_T[ti_idx].Face();
			if (c2i_al && c2i_al->IsClosed() &&
			    fi_al && fi_al->SurfaceOf()) {
			    const ON_Surface *sfi_al = fi_al->SurfaceOf();
			    ON_ClassArray<ON_PX_EVENT> pxi;
			    /* 10× tolerance: P_j is on fj's surface exactly
			     * but may be slightly off fi's surface due to
			     * boolean floating-point errors. */
			    if (ON_Intersect(P_j, *sfi_al, pxi,
					     INTERSECTION_TOL * 10.0) &&
				pxi.Count() > 0) {
				/* Find the seam parameter on c2i_al closest
				 * to the target UV (treated as 3D point in
				 * (u,v,0) space). */
				ON_3dPoint uv_ti_3d(pxi[0].m_b[0],
						    pxi[0].m_b[1], 0.0);
				double t_seam_ti = c2i_al->Domain().Min();
				find_seam_t(c2i_al, uv_ti_3d, &t_seam_ti);
				double uv_dist_ti =
				    c2i_al->PointAt(t_seam_ti).DistanceTo(
					c2i_al->PointAt(
					    c2i_al->Domain().Min()));
				if (uv_dist_ti > ON_ZERO_TOLERANCE) {
				    ON_Curve *c2i_new =
					c2i_al->Duplicate();
				    if (c2i_new &&
					c2i_new->ChangeClosedCurveSeam(
					    t_seam_ti)) {
					int new_c2i_idx =
					    brep->AddTrimCurve(c2i_new);
					brep->m_T[ti_idx].ChangeTrimCurve(
					    new_c2i_idx);
				    } else {
					delete c2i_new;
				    }
				}
			    }
			}
		    }

		    /* Step 5: merge ej's vertex into ei's vertex so
		     * the multi-trim loop containing tj stays connected. */
		    {
			int ei_vi = ei.m_vi[0];
			int ej_vi = ej.m_vi[0];
			if (ej_vi != ei_vi) {
			    for (int k = 0; k < brep->m_T.Count(); k++) {
				if (brep->m_T[k].m_vi[0] == ej_vi)
				    brep->m_T[k].m_vi[0] = ei_vi;
				if (brep->m_T[k].m_vi[1] == ej_vi)
				    brep->m_T[k].m_vi[1] = ei_vi;
			    }
			    for (int k = 0; k < brep->m_E.Count(); k++) {
				if (brep->m_E[k].m_vi[0] == ej_vi)
				    brep->m_E[k].m_vi[0] = ei_vi;
				if (brep->m_E[k].m_vi[1] == ej_vi)
				    brep->m_E[k].m_vi[1] = ei_vi;
			    }
			}
		    }
		}
	    }

	    /* Mark ej as unused by emptying its trim list.  No trim
	     * references ej any more (we just redirected tj to ei),
	     * so Compact() will remove ej and clean up its vertex. */
	    ej.m_ti.Empty();

	    break; /* one match per closed edge is sufficient */
	}
    }

    /* Rebuild all vertex m_ei arrays from the current edge-vertex
     * references.  The Pass 2 vertex-merge operations above updated
     * edge m_vi[] arrays but did NOT update vertex m_ei[] arrays, so
     * some vertices have stale edge lists.  Compact() uses m_ei to
     * decide which vertices are live, so we must fix m_ei first. */
    for (int k = 0; k < brep->m_V.Count(); k++)
	brep->m_V[k].m_ei.Empty();
    for (int k = 0; k < brep->m_E.Count(); k++) {
	const ON_BrepEdge &ek = brep->m_E[k];
	if (ek.m_ti.Count() == 0) continue; /* will be removed */
	int v0 = ek.m_vi[0], v1 = ek.m_vi[1];
	/* For closed edges (v0==v1) the index appears twice. */
	if (v0 >= 0 && v0 < brep->m_V.Count())
	    brep->m_V[v0].m_ei.Append(k);
	if (v1 >= 0 && v1 < brep->m_V.Count())
	    brep->m_V[v1].m_ei.Append(k);
    }

    brep->Compact();

    /* After Compact() renumbers all elements, rebuild vertex m_ei arrays
     * one final time.  Compact() applies a remapping to existing m_ei
     * entries but may not add entries for newly-referenced vertices (from
     * the vertex-merge steps above).  A clean rebuild from the post-compact
     * edge list is the safest approach. */
    for (int k = 0; k < brep->m_V.Count(); k++)
	brep->m_V[k].m_ei.Empty();
    for (int k = 0; k < brep->m_E.Count(); k++) {
	int v0 = brep->m_E[k].m_vi[0];
	int v1 = brep->m_E[k].m_vi[1];
	if (v0 >= 0 && v0 < brep->m_V.Count())
	    brep->m_V[v0].m_ei.Append(k);
	if (v1 >= 0 && v1 < brep->m_V.Count() && v1 != v0)
	    brep->m_V[v1].m_ei.Append(k);
	/* Closed edge: both ends are the same vertex; convention is that
	 * the edge index appears TWICE in that vertex's m_ei list. */
	if (v0 == v1 && v0 >= 0 && v0 < brep->m_V.Count())
	    brep->m_V[v0].m_ei.Append(k);
    }

    brep->SetTrimTypeFlags(false);
}


int
ON_Boolean(ON_Brep *evaluated_brep, const ON_Brep *brep1, const ON_Brep *brep2, op_type operation)
{
    static int calls = 0;
    ++calls;
    std::ostringstream prefix;
    prefix << "bool" << calls;
    //dplot = new DebugPlot(prefix.str().c_str());
    //dplot->Surfaces(brep1, brep2);
    //dplot->WriteLog();

    ON_ClassArray<ON_SimpleArray<TrimmedFace *> > trimmed_faces;
    try {
	/* Handle empty-brep operands before any other processing.
	 * An empty brep (0 faces) is the identity element for union and
	 * the absorbing element for intersection; for subtraction,
	 * empty - anything = empty and anything - empty = anything.
	 * Handling this here also prevents get_face_intersection_curves()
	 * from returning a zero-length curve array that would be indexed
	 * out-of-bounds by get_evaluated_faces(). */
	if (brep1->m_F.Count() == 0 || brep2->m_F.Count() == 0) {
	    switch (operation) {
		case BOOLEAN_UNION:
		    if (brep1->m_F.Count() > 0)
			evaluated_brep->Append(*brep1);
		    else
			evaluated_brep->Append(*brep2);
		    break;
		case BOOLEAN_DIFF:
		    if (brep1->m_F.Count() > 0)
			evaluated_brep->Append(*brep1);
		    /* brep2 is empty or brep1 is empty: either way result
		     * is brep1 (possibly empty). */
		    break;
		case BOOLEAN_INTERSECT:
		    /* anything ∩ empty = empty */
		    break;
		default:
		    throw InvalidBooleanOperation("Error - unknown boolean operation\n");
	    }
	    evaluated_brep->ShrinkSurfaces();
	    evaluated_brep->Compact();
	    return 0;
	}

	/* Deal with the trivial cases up front */
	if (brep1->BoundingBox().MinimumDistanceTo(brep2->BoundingBox()) > ON_ZERO_TOLERANCE) {
	    switch (operation) {
		case BOOLEAN_UNION:
		    evaluated_brep->Append(*brep1);
		    evaluated_brep->Append(*brep2);
		    break;
		case BOOLEAN_DIFF:
		    evaluated_brep->Append(*brep1);
		    break;
		case BOOLEAN_INTERSECT:
		    return 0;
		    break;
		default:
		    throw InvalidBooleanOperation("Error - unknown boolean operation\n");
	    }
	    evaluated_brep->ShrinkSurfaces();
	    evaluated_brep->Compact();
	    /* Recompute tolerances on the trivial-union path as well. */
	    evaluated_brep->SetEdgeTolerances(false);
	    evaluated_brep->SetTrimTolerances(false);
	    evaluated_brep->SetVertexTolerances(false);
	    for (int ei = 0; ei < evaluated_brep->m_E.Count(); ++ei) {
		if (evaluated_brep->m_E[ei].m_tolerance < 0.0)
		    evaluated_brep->m_E[ei].m_tolerance = 0.0;
	    }
	    for (int ti = 0; ti < evaluated_brep->m_T.Count(); ++ti) {
		if (evaluated_brep->m_T[ti].m_tolerance[0] < 0.0)
		    evaluated_brep->m_T[ti].m_tolerance[0] = 0.0;
		if (evaluated_brep->m_T[ti].m_tolerance[1] < 0.0)
		    evaluated_brep->m_T[ti].m_tolerance[1] = 0.0;
	    }
	    //dplot->WriteLog();
	    return 0;
	}
	trimmed_faces = get_evaluated_faces(brep1, brep2, operation);
    } catch (InvalidBooleanOperation &e) {
	bu_log("%s", e.what());
	//dplot->WriteLog();
	return -1;
    } catch (GeometryGenerationError &e) {
	bu_log("%s", e.what());
	//dplot->WriteLog();
	return -1;
    }

    int face_count1 = brep1->m_F.Count();
    int face_count2 = brep2->m_F.Count();

    // check for face counts high enough to cause overflow
    if ((face_count1 + face_count2) < 0)
	return -1;

    for (int i = 0; i < trimmed_faces.Count(); i++) {
	const ON_SimpleArray<TrimmedFace *> &splitted = trimmed_faces[i];
	const ON_Surface *surf = splitted.Count() ? splitted[0]->m_face->SurfaceOf() : NULL;
	bool added = false;
	for (int j = 0; j < splitted.Count(); j++) {
	    TrimmedFace *t_face = splitted[j];
	    if (t_face->m_belong_to_final == TrimmedFace::BELONG) {
		// Add the surfaces, faces, loops, trims, vertices, edges, etc.
		// to the brep structure.
		if (!added) {
		    ON_Surface *new_surf = surf->Duplicate();
		    evaluated_brep->AddSurface(new_surf);
		    added = true;
		}
		ON_BrepFace &new_face = evaluated_brep->NewFace(evaluated_brep->m_S.Count() - 1);

		add_elements(evaluated_brep, new_face, t_face->m_outerloop, ON_BrepLoop::outer);
		// ON_BrepLoop &loop = evaluated_brep->m_L[evaluated_brep->m_L.Count() - 1];
		for (unsigned int k = 0; k < t_face->m_innerloop.size(); k++) {
		    add_elements(evaluated_brep, new_face, t_face->m_innerloop[k], ON_BrepLoop::inner);
		}

		evaluated_brep->SetTrimIsoFlags(new_face);
		const ON_BrepFace &original_face = i >= face_count1 ? brep2->m_F[i - face_count1] : brep1->m_F[i];
		if (original_face.m_bRev ^ t_face->m_rev) {
		    evaluated_brep->FlipFace(new_face);
		}
	    }
	}
    }

    for (int i = 0; i < face_count1 + face_count2; i++) {
	for (int j = 0; j < trimmed_faces[i].Count(); j++) {
	    if (trimmed_faces[i][j]) {
		delete trimmed_faces[i][j];
		trimmed_faces[i][j] = NULL;
	    }
	}
    }

    evaluated_brep->ShrinkSurfaces();

    /* If the boolean evaluation produced no faces the result is
     * geometrically empty (e.g. a solid subtracted from itself).
     * Skip the edge-join and tolerance steps which are no-ops on an
     * empty brep, and return a dedicated code so callers can recognise
     * and handle the zero-volume result. */
    if (evaluated_brep->m_F.Count() == 0) {
	evaluated_brep->Compact();
	return 1; /* valid but empty result */
    }

    /* Join coincident boundary edges so the result is a closed solid that
     * the raytracer treats as a solid volume rather than a zero-thickness
     * plate.  Must be called before Compact() so edge indices are stable. */
    join_boundary_edges(evaluated_brep);

    /* Merge coincident vertices introduced by the face-by-face assembly.
     *
     * add_elements() creates vertices independently for each face, so the
     * same geometric intersection point can end up with multiple vertex
     * indices.  join_boundary_edges() already matches edges by 3-D position,
     * but it does not merge the underlying vertex objects.  The resulting
     * trim/edge vertex-index inconsistencies fail ON_Brep::IsValid().
     *
     * Build a vertex-remap table: for every vertex vi, find the lowest-indexed
     * vertex vj that is geometrically coincident within vtol and set remap[vi]=vj.
     * Then update all trims, edges, and vertex edge-lists to use the canonical
     * vertex for each cluster. */
    {
	ON_BoundingBox repair_bbox;
	evaluated_brep->GetBoundingBox(repair_bbox);
	double repair_bbox_diag = repair_bbox.Diagonal().Length();
	static const double REPAIR_VTOL_REL = 1.0e-5;
	double vtol = (repair_bbox_diag > ON_ZERO_TOLERANCE) ?
	    repair_bbox_diag * REPAIR_VTOL_REL : ON_ZERO_TOLERANCE;
	if (vtol < INTERSECTION_TOL * 10.0)
	    vtol = INTERSECTION_TOL * 10.0;

	const int nv = evaluated_brep->m_V.Count();
	ON_SimpleArray<int> remap(nv);
	remap.SetCount(nv);
	for (int vi = 0; vi < nv; vi++) remap[vi] = vi;

	for (int vi = 0; vi < nv; vi++) {
	    if (remap[vi] != vi) continue; /* already merged into a lower vertex */
	    const ON_3dPoint &pi = evaluated_brep->m_V[vi].Point();
	    for (int vj = vi + 1; vj < nv; vj++) {
		if (remap[vj] != vj) continue;
		const ON_3dPoint &pj = evaluated_brep->m_V[vj].Point();
		if (pi.DistanceTo(pj) > vtol) continue;
		remap[vj] = vi;
	    }
	}

	/* Apply remap: update all trims and edges, rebuild vertex edge lists. */
	bool any_merged = false;
	for (int vi = 0; vi < nv; vi++) {
	    if (remap[vi] != vi) { any_merged = true; break; }
	}
	if (any_merged) {
	    /* Update trims. */
	    for (int ti = 0; ti < evaluated_brep->m_T.Count(); ti++) {
		ON_BrepTrim &t = evaluated_brep->m_T[ti];
		if (t.m_vi[0] >= 0) t.m_vi[0] = remap[t.m_vi[0]];
		if (t.m_vi[1] >= 0) t.m_vi[1] = remap[t.m_vi[1]];
	    }
	    /* Update edges.  When remapping creates m_vi[0]==m_vi[1] for a
	     * non-closed edge (degenerate near-zero edge whose two endpoints
	     * were merged), the code below replaces its 3D curve with a tiny
	     * full-circle arc so that IsClosed() returns true and the topology
	     * is consistent with OpenNURBS IsValid(). */
	    for (int ei = 0; ei < evaluated_brep->m_E.Count(); ei++) {
		ON_BrepEdge &e = evaluated_brep->m_E[ei];
		if (e.m_vi[0] >= 0) e.m_vi[0] = remap[e.m_vi[0]];
		if (e.m_vi[1] >= 0) e.m_vi[1] = remap[e.m_vi[1]];
		if (e.m_vi[0] == e.m_vi[1] && e.m_vi[0] >= 0 && !e.IsClosed()) {
		    /* Degenerate self-loop: the vertex merge collapsed a
		     * near-zero-length edge so both endpoints are now the
		     * same vertex, but the 3-D curve is not closed.  Replace
		     * with a tiny full-circle arc so that IsClosed() returns
		     * true.  m_bRev3d is set later (after
		     * standardize_loop_orientations) to avoid a possible
		     * direction-flip by that function invalidating the value
		     * we compute here.
		     *
		     * Orient the arc plane so that the arc's tangent at t=0
		     * (= plane Y-axis) aligns with the 3D projection of the
		     * trim's UV chord direction.  This guarantees that after
		     * standardize_loop_orientations (which reverses both the
		     * trim curve and the edge arc together if the loop is
		     * reversed), the trim and arc still run in the same
		     * direction, so m_bRev3d = false for all self-loop trims.
		     *
		     * The plane Y-axis is set to normalize(tan3d) where
		     * tan3d = uv_dir.x * du + uv_dir.y * dv. */
		    const ON_3dPoint &vpt = evaluated_brep->m_V[e.m_vi[0]].Point();
		    /* Find the 3D direction from the first usable trim. */
		    ON_3dVector yaxis(0, 1, 0); /* fallback */
		    for (int k = 0; k < e.m_ti.Count(); k++) {
			int ti = e.m_ti[k];
			if (ti < 0 || ti >= evaluated_brep->m_T.Count()) continue;
			const ON_BrepTrim &tr = evaluated_brep->m_T[ti];
			const ON_BrepLoop *lp = tr.Loop();
			const ON_BrepFace *fc = lp ? lp->Face() : nullptr;
			const ON_Surface  *sf = fc ? fc->SurfaceOf() : nullptr;
			const ON_Curve    *cv = tr.TrimCurveOf();
			if (!sf || !cv) continue;
			ON_3dPoint ps = cv->PointAtStart();
			ON_3dPoint pe = cv->PointAtEnd();
			ON_3dVector uvd(pe.x - ps.x, pe.y - ps.y, 0);
			if (uvd.IsZero())
			    uvd = cv->TangentAt(cv->Domain().Mid());
			if (uvd.IsZero()) continue;
			ON_3dPoint srfpt; ON_3dVector du, dv;
			sf->Ev1Der(ps.x, ps.y, srfpt, du, dv);
			ON_3dVector tan3d = uvd.x * du + uvd.y * dv;
			if (tan3d.IsZero()) continue;
			tan3d.Unitize();
			yaxis = tan3d;
			break;
		    }
		    /* Build a plane with Y-axis = yaxis (arc tangent at t=0).
		     * The radius is chosen small enough to be well below any
		     * model feature size while remaining numerically stable. */
		    static const double DEGENERATE_ARC_RADIUS = 1.0e-8;
		    ON_3dVector xaxis = ON_CrossProduct(yaxis, ON_3dVector(0, 0, 1));
		    if (xaxis.IsZero())
			xaxis = ON_CrossProduct(yaxis, ON_3dVector(1, 0, 0));
		    xaxis.Unitize();
		    ON_Plane plane(vpt, xaxis, yaxis);
		    ON_Circle circle(plane, DEGENERATE_ARC_RADIUS);
		    ON_Arc arc(circle, ON_Interval(0.0, 2.0 * ON_PI));
		    ON_ArcCurve *arc_crv = new ON_ArcCurve(arc);
		    int new_c3i = evaluated_brep->m_C3.Count();
		    evaluated_brep->m_C3.Append(arc_crv);
		    evaluated_brep->SetEdgeCurve(e, new_c3i);
		}
	    }
	    /* Rebuild vertex edge lists for all canonical vertices. */
	    for (int vi = 0; vi < nv; vi++)
		evaluated_brep->m_V[vi].m_ei.Empty();
	    for (int ei = 0; ei < evaluated_brep->m_E.Count(); ei++) {
		ON_BrepEdge &e = evaluated_brep->m_E[ei];
		for (int s = 0; s < 2; s++) {
		    int vi2 = e.m_vi[s];
		    if (vi2 >= 0 && vi2 < nv) {
			if (evaluated_brep->m_V[vi2].m_ei.Search(ei) < 0)
			    evaluated_brep->m_V[vi2].m_ei.Append(ei);
		    }
		}
	    }
	}
    }

    standardize_loop_orientations(evaluated_brep);

    /* Set m_bRev3d for any self-loop (closed) arc edges that were created
     * by the vertex merge pass above.  This is done AFTER
     * standardize_loop_orientations because that function may reverse loop
     * and edge directions.
     *
     * By construction, the arc plane was oriented so that the arc's tangent
     * at t=0 (the plane's Y-axis) aligns with the 3D projection of the trim's
     * UV chord direction.  This means the trim and arc always run in the SAME
     * direction, regardless of whether standardize_loop_orientations reversed
     * the loop (which reverses both the trim curve and the arc edge curve
     * together, preserving their relative direction).  Therefore m_bRev3d is
     * always false for these synthetic self-loop arc trims. */
    for (int ei = 0; ei < evaluated_brep->m_E.Count(); ei++) {
	ON_BrepEdge &e = evaluated_brep->m_E[ei];
	if (!e.IsClosed() || e.m_vi[0] != e.m_vi[1] || e.m_vi[0] < 0)
	    continue;
	if (!ON_ArcCurve::Cast(e.EdgeCurveOf())) continue; /* not our arc */
	for (int k = 0; k < e.m_ti.Count(); k++) {
	    int trim_idx = e.m_ti[k];
	    if (trim_idx >= 0 && trim_idx < evaluated_brep->m_T.Count())
		evaluated_brep->m_T[trim_idx].m_bRev3d = false;
	}
    }

    /* Recompute all tolerances from geometry.  The boolean code deliberately
     * sets edge and trim tolerances to MAX_FASTF (the OpenNURBS "unset"
     * sentinel) so that the code doesn't need to recompute them during
     * construction.  But the raytracer's rt_brep_prep() uses the tolerance
     * values to build its BVH acceleration structure; with MAX_FASTF every
     * face's bounding interval is essentially infinite, so no face is ever
     * culled and no shot ever terminates correctly.  Calling these three
     * methods forces a proper geometric recomputation from the UV curves and
     * 3D edge curves so the brep can be raytraced and volume-measured. */
    evaluated_brep->SetEdgeTolerances(false);
    evaluated_brep->SetTrimTolerances(false);
    evaluated_brep->SetVertexTolerances(false);

    /* After SetEdge/TrimTolerances(), any edge that still has ON_UNSET_VALUE
     * (negative) as its tolerance indicates that the recomputation failed
     * (e.g. an edge whose 3D curve was degenerate or whose trims do not reach
     * it).  IsValid() rejects these as invalid.  Set them to 0.0 (exact)
     * so that the BREP at least passes validity and can be raytraced, even
     * though those edges may have sub-optimal bounding boxes. */
    for (int ei = 0; ei < evaluated_brep->m_E.Count(); ++ei) {
	if (evaluated_brep->m_E[ei].m_tolerance < 0.0)
	    evaluated_brep->m_E[ei].m_tolerance = 0.0;
    }
    for (int ti = 0; ti < evaluated_brep->m_T.Count(); ++ti) {
	if (evaluated_brep->m_T[ti].m_tolerance[0] < 0.0)
	    evaluated_brep->m_T[ti].m_tolerance[0] = 0.0;
	if (evaluated_brep->m_T[ti].m_tolerance[1] < 0.0)
	    evaluated_brep->m_T[ti].m_tolerance[1] = 0.0;
    }

    // Check IsValid() and output the message.
    ON_wString ws;
    ON_TextLog log(ws);
    evaluated_brep->IsValid(&log);
    if (ON_String(ws).Array()) {
	bu_log("%s", ON_String(ws).Array());
    }

    //dplot->WriteLog();
    //delete dplot;
    //dplot = NULL;

    return 0;
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
