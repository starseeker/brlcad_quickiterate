/*                       R E P A I R . C P P
 * BRL-CAD
 *
 * Copyright (c) 2020-2025 United States Government as represented by
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
/** @file repair.cpp
 *
 * Routines related to repairing BoTs
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "manifold/manifold.h"

#include <Mathematics/Vector3.h>
#include <Mathematics/MeshRepair.h>
#include <Mathematics/MeshHoleFilling.h>
#include <Mathematics/MeshPreprocessing.h>
#include <Mathematics/MeshQuality.h>
#include <Mathematics/MeshRemesh.h>

#include "bu/parallel.h"
#include "bg/trimesh.h"
#include "rt/defines.h"
#include "rt/application.h"
#include "rt/db_io.h"
#include "rt/geom.h"
#include "rt/primitives/bot.h"
#include "rt/rt_instance.h"
#include "rt/shoot.h"
#include "rt/wdb.h"

// The checking done with raytracing here is basically the checking done by
// libged's lint command, without the output collection done there for
// reporting purposes.  This is a yes/no decision as to whether the "repaired"
// BoT is suitable to be returned.
//
// No such automated checks can catch all cases where the result isn't what a
// user originally expected for all inputs (in the most general cases that
// question is actually not well defined) but we CAN catch a few situations
// where the result is technically manifold but the mesh still does something
// unexpected during solid raytracing.

struct lint_worker_vars {
    struct rt_i *rtip;
    struct resource *resp;
    int tri_start;
    int tri_end;
    bool reverse;
    void *ptr;
};

class lint_worker_data {
    public:
	lint_worker_data(struct rt_i *rtip, struct resource *res);
	~lint_worker_data();
	void shoot(int ind, bool reverse);

	int curr_tri = -1;
	double ttol = 0.0;

	bool error_found = false;

	struct application ap;
	struct rt_bot_internal *bot = NULL;
	const std::unordered_set<int> *bad_faces = NULL;
};

static bool
bot_face_normal(vect_t *n, struct rt_bot_internal *bot, int i)
{
    vect_t a,b;

    /* sanity */
    if (!n || !bot || i < 0 || (size_t)i > bot->num_faces ||
            bot->faces[i*3+2] < 0 || (size_t)bot->faces[i*3+2] > bot->num_vertices) {
        return false;
    }

    VSUB2(a, &bot->vertices[bot->faces[i*3+1]*3], &bot->vertices[bot->faces[i*3]*3]);
    VSUB2(b, &bot->vertices[bot->faces[i*3+2]*3], &bot->vertices[bot->faces[i*3]*3]);
    VCROSS(*n, a, b);
    VUNITIZE(*n);
    if (bot->orientation == RT_BOT_CW) {
        VREVERSE(*n, *n);
    }

    return true;
}

static int
_hit_noop(struct application *UNUSED(ap), struct partition *PartHeadp, struct seg *UNUSED(segs))
{
    if (PartHeadp->pt_forw == PartHeadp)
	return 1;

    return 0;
}
static int
_miss_noop(struct application *UNUSED(ap))
{
    return 0;
}

static int
_overlap_noop(struct application *UNUSED(ap), struct partition *UNUSED(pp),
	struct region *UNUSED(reg1), struct region *UNUSED(reg2), struct partition *UNUSED(hp))
{
    // I don't think this is supposed to happen with a single primitive?
    return 0;
}

static int
_miss_err(struct application *ap)
{
    lint_worker_data *tinfo = (lint_worker_data *)ap->a_uptr;
    tinfo->error_found = true;
    return 0;
}

static int
_tc_hit(struct application *ap, struct partition *PartHeadp, struct seg *segs)
{
    if (PartHeadp->pt_forw == PartHeadp)
	return 1;

    lint_worker_data *tinfo = (lint_worker_data *)ap->a_uptr;

    struct seg *s = (struct seg *)segs->l.forw;
    if (s->seg_in.hit_dist > 2*SQRT_SMALL_FASTF) {
	// This is a problem (although it's not the thin volume problem.) No point in
	// continuing, flag and return.
	tinfo->error_found = true;
	return 0;
    }

    for (BU_LIST_FOR(s, seg, &(segs->l))) {
	// We're only interested in thin interactions centering around the
	// triangle in question - other triangles along the shotline will be
	// checked in different shots
	if (s->seg_in.hit_dist > tinfo->ttol)
	    break;

	double dist = s->seg_out.hit_dist - s->seg_in.hit_dist;
	if (dist > VUNITIZE_TOL)
	    continue;

	// Error condition met - set flag
	tinfo->error_found = true;
	return 0;
    }

    return 0;
}

static int
_ck_up_hit(struct application *ap, struct partition *PartHeadp, struct seg *UNUSED(segs))
{
    if (PartHeadp->pt_forw == PartHeadp)
	return 1;

    lint_worker_data *tinfo = (lint_worker_data *)ap->a_uptr;

    // TODO - validate whether the vector between the two hit points is
    // parallel to the ray.  Saw one case where it seemed as if we were getting
    // an offset that resulted in a higher distance, but only because there was
    // a shift of one of the hit points off the ray by more than ttol
    struct partition *pp = PartHeadp->pt_forw;
    if (pp->pt_inhit->hit_dist > tinfo->ttol)
	return 0;

    // We've got something < tinfo->ttol above our triangle - too close, trouble
    tinfo->error_found = true;
    return 0;
}

static int
_uh_hit(struct application *ap, struct partition *PartHeadp, struct seg *segs)
{
    if (PartHeadp->pt_forw == PartHeadp)
	return 1;

    lint_worker_data *tinfo = (lint_worker_data *)ap->a_uptr;

    struct seg *s = (struct seg *)segs->l.forw;
    if (s->seg_in.hit_dist < 2*SQRT_SMALL_FASTF)
	return 0;

    // Segment's first hit didn't come from the expected triangle.
    tinfo->error_found = true;
    return 0;
}


extern "C" void
bot_repair_lint_worker(int cpu, void *ptr)
{
    struct lint_worker_vars *state = &(((struct lint_worker_vars *)ptr)[cpu]);
    lint_worker_data *d = (lint_worker_data *)state->ptr;

    for (int i = state->tri_start; i < state->tri_end; i++) {
	d->shoot(i, state->reverse);
    }
}

lint_worker_data::lint_worker_data(struct rt_i *rtip, struct resource *res)
{
    RT_APPLICATION_INIT(&ap);
    ap.a_onehit = 0;
    ap.a_rt_i = rtip;             /* application uses this instance */
    ap.a_hit = _hit_noop;         /* where to go on a hit */
    ap.a_miss = _miss_noop;       /* where to go on a miss */
    ap.a_overlap = _overlap_noop; /* where to go if an overlap is found */
    ap.a_onehit = 0;              /* whether to stop the raytrace on the first hit */
    ap.a_resource = res;
    ap.a_uptr = (void *)this;
}

lint_worker_data::~lint_worker_data()
{
}

void
lint_worker_data::shoot(int ind, bool reverse)
{
    if (!bot)
	return;

    // Set curr_tri so the callbacks know what our origin triangle is
    curr_tri = ind;

    // If we already know this face is no good, skip
    if (bad_faces && bad_faces->find(curr_tri) != bad_faces->end())
	return;

    // Triangle passes filters, continue processing
    vect_t rnorm, n, backout;
    if (!bot_face_normal(&n, bot, ind))
	return;
    // Reverse the triangle normal for a ray direction
    VREVERSE(rnorm, n);

    // We want backout to get the ray origin off the triangle surface.  If
    // we're shooting up from the triangle (reverse) we "backout" into the
    // triangle, if we're shooting into the triangle we back out above it.
    if (reverse) {
	// We're reversing for "close" testing, and a close triangle may be
	// degenerately close to our test triangle.  Hence, we back below
	// the surface to be sure.
	VMOVE(backout, rnorm);
	VMOVE(ap.a_ray.r_dir, n);
    } else {
	VMOVE(backout, n);
	VMOVE(ap.a_ray.r_dir, rnorm);
    }
    VSCALE(backout, backout, SQRT_SMALL_FASTF);

    point_t rpnts[3];
    point_t tcenter;
    VMOVE(rpnts[0], &bot->vertices[bot->faces[ind*3+0]*3]);
    VMOVE(rpnts[1], &bot->vertices[bot->faces[ind*3+1]*3]);
    VMOVE(rpnts[2], &bot->vertices[bot->faces[ind*3+2]*3]);
    VADD3(tcenter, rpnts[0], rpnts[1], rpnts[2]);
    VSCALE(tcenter, tcenter, 1.0/3.0);

    // Take the shot
    VADD2(ap.a_ray.r_pt, tcenter, backout);
    (void)rt_shootray(&ap);
}


typedef int (*fhit_t)(struct application *, struct partition *, struct seg *);
typedef int (*fmiss_t)(struct application *);

static bool
bot_check(struct lint_worker_vars *state, fhit_t hf, fmiss_t mf, int onehit, bool reverse, size_t ncpus)
{
    // We always need at least one worker data container to do any work at all.
    if (!ncpus)
	return false;

    // Much of the information needed for different tests is common and thus can be
    // reused, but some aspects are specific to each test - let all the worker data
    // containers know what the specifics are for this test.
    for (size_t i = 0; i < ncpus; i++) {
	lint_worker_data *d = (lint_worker_data *)state[i].ptr;
	d->ap.a_hit = hf;
	d->ap.a_miss = mf;
	d->ap.a_onehit = onehit;
	state[i].reverse = reverse;
    }

    bu_parallel(bot_repair_lint_worker, ncpus, (void *)state);

    // Check the thread results to see if any errors were reported
    for (size_t i = 0; i < ncpus; i++) {
	lint_worker_data *d = (lint_worker_data *)state[i].ptr;
	if (d->error_found)
	    return false;
    }

    return true;
}

static int
bot_repair_lint(struct rt_bot_internal *bot)
{
    // Empty BoTs are a problem
    if (!bot || !bot->num_faces)
	return -1;

    // Default to valid
    int ret = 0;

    // We need to use the raytracer to test this BoT, but it is not a database
    // entity yet.  Accordingly, we set up an in memory db_i and add this BoT
    // to it so we can raytrace it.  Any failure here means we weren't able to
    // do the test and (in the absence of confirmed testing success) we have no
    // choice but to report failure.
    struct db_i *dbip = db_open_inmem();
    if (dbip == DBI_NULL)
        return -1;
    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);
    // wdb_export is destructive to the rt_bot_internal, so we need
    // to make a copy.
    struct rt_bot_internal *dbot = rt_bot_dup(bot);
    wdb_export(wdbp, "r.bot", (void *)dbot, ID_BOT, 1.0);
    struct directory *dp = db_lookup(wdbp->dbip, "r.bot", LOOKUP_QUIET);
    if (dp == RT_DIR_NULL) {
	db_close(dbip);
	return -1;
    }

    // Note that these tests won't work as expected if the BoT is
    // self-intersecting...
    struct rt_i *rtip = rt_new_rti(dbip);
    rt_gettree(rtip, dp->d_namep);
    rt_prep(rtip);

    // Set up memory
    //size_t ncpus = bu_avail_cpus();
    size_t ncpus = 1;
    struct lint_worker_vars *state = (struct lint_worker_vars *)bu_calloc(ncpus+1, sizeof(struct lint_worker_vars ), "state");
    struct resource *resp = (struct resource *)bu_calloc(ncpus+1, sizeof(struct resource), "resources");

    // We need to divy up the faces.  Since all triangle intersections will
    // (hopefully) take about the same length of time to run, we don't do anything
    // fancy about chunking up the work.
    int tri_step = bot->num_faces / ncpus;

    for (size_t i = 0; i < ncpus; i++) {
	state[i].rtip = rtip;
	state[i].resp = &resp[i];
	rt_init_resource(state[i].resp, (int)i, state[i].rtip);
	state[i].tri_start = i * tri_step;
	state[i].tri_end = state[i].tri_start + tri_step;
	//bu_log("%d: tri_state: %d, tri_end %d\n", (int)i, state[i].tri_start, state[i].tri_end);
	state[i].reverse = false;

	lint_worker_data *d = new lint_worker_data(rtip, state[i].resp);
	d->bot = bot;
	d->ttol = VUNITIZE_TOL;
	state[i].ptr = (void *)d;
    }

    // Make sure the last thread ends on the last face
    state[ncpus-1].tri_end = bot->num_faces - 1;
    //bu_log("%d: tri_end %d\n", (int)ncpus-1, state[ncpus-1].tri_end);

    /* Unexpected miss test.
     * Note that we are deliberately using onehit=1 for the miss test to check
     * the intersection behavior of the individual triangles */
    if (!bot_check(state, _hit_noop, _miss_err, 1, false, ncpus)) {
	//bu_log("unexpected_miss\n");
	ret = 1;
	goto bot_lint_cleanup;
    }

    /* Thin volume test.
     * Thin face pairings are a common artifact of coplanar faces in boolean
     * evaluations */
    if (!bot_check(state, _tc_hit, _miss_err, 0, false, ncpus)){
	//bu_log("thin_volume\n");
	ret = 2;
	goto bot_lint_cleanup;
    }

    /* Close face test.
     * When testing for faces that are too close to a given face, we need to
     * reverse the ray direction */
    if (!bot_check(state, _ck_up_hit, _miss_noop, 0, true, ncpus)){
	//bu_log("close_face\n");
	ret = 2;
	goto bot_lint_cleanup;
    }

    /* Unexpected hit test.
     * Checking for the case where we end up with a hit from a triangle other
     * than the one we derive the ray from. */
    if (!bot_check(state, _uh_hit, _miss_noop, 0, false, ncpus)){
	//bu_log("unexpected_hit\n");
	ret = 2;
	goto bot_lint_cleanup;
    }

bot_lint_cleanup:
    for (size_t i = 0; i < ncpus; i++) {
	lint_worker_data *d = (lint_worker_data *)state[i].ptr;
	delete d;
    }

    rt_free_rti(rtip);
    bu_free(state, "state");
    bu_free(resp, "resp");
    db_close(dbip);

    return ret;
}

// Helper: compute bounding box diagonal of a GTE vertex set
static double
gte_bbox_diagonal(std::vector<gte::Vector3<double>> const& vertices)
{
    if (vertices.empty())
	return 0.0;
    double minx = vertices[0][0], maxx = vertices[0][0];
    double miny = vertices[0][1], maxy = vertices[0][1];
    double minz = vertices[0][2], maxz = vertices[0][2];
    for (auto const& v : vertices) {
	if (v[0] < minx) minx = v[0];
	if (v[0] > maxx) maxx = v[0];
	if (v[1] < miny) miny = v[1];
	if (v[1] > maxy) maxy = v[1];
	if (v[2] < minz) minz = v[2];
	if (v[2] > maxz) maxz = v[2];
    }
    double dx = maxx - minx, dy = maxy - miny, dz = maxz - minz;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

// Helper: compute total mesh area from GTE vertex/triangle arrays
static double
gte_mesh_area(std::vector<gte::Vector3<double>> const& vertices,
	      std::vector<std::array<int32_t, 3>> const& triangles)
{
    double area = 0.0;
    for (auto const& tri : triangles) {
	gte::Vector3<double> const& p0 = vertices[tri[0]];
	gte::Vector3<double> const& p1 = vertices[tri[1]];
	gte::Vector3<double> const& p2 = vertices[tri[2]];
	gte::Vector3<double> e1 = p1 - p0;
	gte::Vector3<double> e2 = p2 - p0;
	gte::Vector3<double> cross = gte::Cross(e1, e2);
	area += gte::Length(cross) * 0.5;
    }
    return area;
}

static void
bot_to_gte(std::vector<gte::Vector3<double>>& vertices,
	   std::vector<std::array<int32_t, 3>>& triangles,
	   struct rt_bot_internal *bot)
{
    vertices.resize(bot->num_vertices);
    for (size_t i = 0; i < bot->num_vertices; i++) {
	vertices[i][0] = bot->vertices[3*i+0];
	vertices[i][1] = bot->vertices[3*i+1];
	vertices[i][2] = bot->vertices[3*i+2];
    }
    triangles.resize(bot->num_faces);
    for (size_t i = 0; i < bot->num_faces; i++) {
	triangles[i][0] = bot->faces[3*i+0];
	triangles[i][1] = bot->faces[3*i+1];
	triangles[i][2] = bot->faces[3*i+2];
    }

    // After the initial raw load, do a repair pass to remove degenerate
    // triangles, colocated vertices, and small connected components - matching
    // the pre-processing that the previous Geogram-based path performed.
    double bbox_diag = gte_bbox_diagonal(vertices);
    double epsilon = 1e-6 * (0.01 * bbox_diag);
    gte::MeshRepair<double>::Parameters repairParams;
    repairParams.epsilon = epsilon;
    gte::MeshRepair<double>::Repair(vertices, triangles, repairParams);

    double area = gte_mesh_area(vertices, triangles);
    double min_comp_area = 0.03 * area;
    if (min_comp_area > 0.0) {
	size_t nf_before = triangles.size();
	gte::MeshPreprocessing<double>::RemoveSmallComponents(vertices, triangles, min_comp_area);
	if (triangles.size() != nf_before) {
	    gte::MeshRepair<double>::Repair(vertices, triangles, repairParams);
	}
    }
}

static void
gte_to_manifold(manifold::MeshGL *gmm,
		std::vector<gte::Vector3<double>> const& vertices,
		std::vector<std::array<int32_t, 3>> const& triangles)
{
    for (auto const& v : vertices) {
	gmm->vertProperties.push_back(v[0]);
	gmm->vertProperties.push_back(v[1]);
	gmm->vertProperties.push_back(v[2]);
    }
    for (auto const& tri : triangles) {
	// TODO - CW vs CCW orientation handling?
	gmm->triVerts.push_back(tri[0]);
	gmm->triVerts.push_back(tri[1]);
	gmm->triVerts.push_back(tri[2]);
    }
}

static struct rt_bot_internal *
manifold_to_bot(manifold::MeshGL *omesh)
{
    struct rt_bot_internal *nbot;
    BU_GET(nbot, struct rt_bot_internal);
    nbot->magic = RT_BOT_INTERNAL_MAGIC;
    nbot->mode = RT_BOT_SOLID;
    nbot->orientation = RT_BOT_CCW;
    nbot->thickness = NULL;
    nbot->face_mode = (struct bu_bitv *)NULL;
    nbot->bot_flags = 0;
    nbot->num_vertices = (int)omesh->vertProperties.size()/3;
    nbot->num_faces = (int)omesh->triVerts.size()/3;
    nbot->vertices = (double *)calloc(omesh->vertProperties.size(), sizeof(double));;
    nbot->faces = (int *)calloc(omesh->triVerts.size(), sizeof(int));
    for (size_t j = 0; j < omesh->vertProperties.size(); j++)
	nbot->vertices[j] = omesh->vertProperties[j];
    for (size_t j = 0; j < omesh->triVerts.size(); j++)
	nbot->faces[j] = omesh->triVerts[j];

    return nbot;
}

struct rt_bot_internal *
gte_to_bot(std::vector<gte::Vector3<double>> const& vertices,
	   std::vector<std::array<int32_t, 3>> const& triangles)
{
    struct rt_bot_internal *nbot;
    BU_GET(nbot, struct rt_bot_internal);
    nbot->magic = RT_BOT_INTERNAL_MAGIC;
    nbot->mode = RT_BOT_SOLID;
    nbot->orientation = RT_BOT_CCW;
    nbot->thickness = NULL;
    nbot->face_mode = (struct bu_bitv *)NULL;
    nbot->bot_flags = 0;
    nbot->num_vertices = (int)vertices.size();
    nbot->num_faces = (int)triangles.size();
    nbot->vertices = (double *)calloc(nbot->num_vertices*3, sizeof(double));
    nbot->faces = (int *)calloc(nbot->num_faces*3, sizeof(int));

    for (size_t j = 0; j < vertices.size(); j++) {
	nbot->vertices[3*j+0] = vertices[j][0];
	nbot->vertices[3*j+1] = vertices[j][1];
	nbot->vertices[3*j+2] = vertices[j][2];
    }

    for (size_t j = 0; j < triangles.size(); j++) {
	// TODO - CW vs CCW orientation handling?
	nbot->faces[3*j+0] = triangles[j][0];
	nbot->faces[3*j+1] = triangles[j][1];
	nbot->faces[3*j+2] = triangles[j][2];
    }

    return nbot;
}

int
rt_bot_repair(struct rt_bot_internal **obot, struct rt_bot_internal *bot, struct rt_bot_repair_info *settings)
{
    if (!bot || !obot || !settings)
	return -1;

    // Unless we produce something, obot will be NULL
    *obot = NULL;

    manifold::MeshGL64 bot_mesh;
    for (size_t j = 0; j < bot->num_vertices ; j++) {
	bot_mesh.vertProperties.insert(bot_mesh.vertProperties.end(), bot->vertices[3*j+0]);
	bot_mesh.vertProperties.insert(bot_mesh.vertProperties.end(), bot->vertices[3*j+1]);
	bot_mesh.vertProperties.insert(bot_mesh.vertProperties.end(), bot->vertices[3*j+2]);
    }
    if (bot->orientation == RT_BOT_CW) {
	for (size_t j = 0; j < bot->num_faces; j++) {
	    bot_mesh.triVerts.insert(bot_mesh.triVerts.end(), bot->faces[3*j]);
	    bot_mesh.triVerts.insert(bot_mesh.triVerts.end(), bot->faces[3*j+2]);
	    bot_mesh.triVerts.insert(bot_mesh.triVerts.end(), bot->faces[3*j+1]);
	}
    } else {
	for (size_t j = 0; j < bot->num_faces; j++) {
	    bot_mesh.triVerts.insert(bot_mesh.triVerts.end(), bot->faces[3*j]);
	    bot_mesh.triVerts.insert(bot_mesh.triVerts.end(), bot->faces[3*j+1]);
	    bot_mesh.triVerts.insert(bot_mesh.triVerts.end(), bot->faces[3*j+2]);
	}
    }

    int num_vertices = (int)bot->num_vertices;
    int num_faces = (int)bot->num_faces;
    int bg_not_solid = bg_trimesh_solid2(num_vertices, num_faces, bot->vertices, bot->faces, NULL);

    if (!bot_mesh.Merge() && !bg_not_solid) {
	// BoT is already manifold
	return 1;
    }

    manifold::Manifold omanifold(bot_mesh);
    if (omanifold.Status() == manifold::Manifold::Error::NoError) {
	// MeshGL.Merge() produced a manifold mesh.  That this worked
	// essentially means the changes needed were EXTREMELY minimal, and we
	// don't bother with further processing before returning the result.
	manifold::MeshGL omesh = omanifold.GetMeshGL();
	struct rt_bot_internal *nbot = manifold_to_bot(&omesh);
	*obot = nbot;
	return 0;
    }

    // Set up GTE mesh from the BoT data, including initial repair and
    // small connected component removal.
    std::vector<gte::Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    bot_to_gte(vertices, triangles, bot);

    // To try to fill in ALL holes we default to 1e30, which is a
    // value used in the Geogram code for a large hole size.
    double hole_size = 1e30;

    // See if the settings override the default
    double area = gte_mesh_area(vertices, triangles);
    if (!NEAR_ZERO(settings->max_hole_area, SMALL_FASTF)) {
	hole_size = settings->max_hole_area;
    } else if (!NEAR_ZERO(settings->max_hole_area_percent, SMALL_FASTF)) {
	hole_size = area * (settings->max_hole_area_percent/100.0);
    }

    // Hole-filling parameters shared by all passes below.
    //
    // LSCM (Least Squares Conformal Maps) is preferred over CDT here because:
    //   - CDT projects the hole boundary onto a best-fit plane, which fails for
    //     non-planar or highly curved holes (common in individual aircraft-skin
    //     meshes like the GenericTwin shapes).
    //   - LSCM maps the boundary to a circle using arc-length parameterization,
    //     which always succeeds for any topologically simple boundary loop
    //     regardless of planarity.
    //   - autoFallback=true further falls back to 3D ear-clipping if LSCM
    //     itself fails (e.g. self-intersecting boundary).
    //
    // Do NOT run MeshRepair after FillHoles.  The repair pass removes colocated
    // or degenerate-looking triangles, but it also removes newly-added hole-fill
    // triangles that share edges with the repaired mesh — which re-opens the
    // holes we just filled.  The final Manifold check below is the authoritative
    // test; no intermediate repair is needed.
    gte::MeshHoleFilling<double>::Parameters fillParams;
    fillParams.maxArea = hole_size;
    fillParams.method = gte::MeshHoleFilling<double>::TriangulationMethod::LSCM;
    fillParams.autoFallback = true;

    // Helper: attempt LSCM hole fill on (v, t) and check Manifold.
    // Returns true and writes the result Manifold into gm_out on success.
    // The area check (filled area >= original) guards against fill removing geometry.
    auto try_fill = [&](std::vector<gte::Vector3<double>> v,
			std::vector<std::array<int32_t, 3>> t,
			double ref_area,
			manifold::Manifold& gm_out) -> bool {
	gte::MeshHoleFilling<double>::FillHoles(v, t, fillParams);
	double new_a = gte_mesh_area(v, t);
	if (new_a < ref_area)
	    return false;
	manifold::MeshGL gmm;
	gte_to_manifold(&gmm, v, t);
	manifold::Manifold gm(gmm);
	if (gm.Status() != manifold::Manifold::Error::NoError)
	    return false;
	gm_out = gm;
	return true;
    };

    // --- Pass 1: straightforward LSCM fill --------------------------------
    //
    // This handles the common case: holes with simple boundary loops.
    manifold::Manifold gmanifold;
    if (!try_fill(vertices, triangles, area, gmanifold)) {

	// --- Pass 2: SplitNonManifoldVertices then LSCM fill -----------------
	//
	// Pass 1 can fail when the mesh has non-manifold vertices — points where
	// multiple independent triangle fans share a single vertex index.
	// bg_trimesh_solid2 (BRL-CAD's solid check) only verifies face-pair
	// connectivity and consistent winding; it does NOT detect non-manifold
	// vertices where three or more surface patches converge.
	//
	// Consequence: the hole-boundary detection in MeshHoleFilling walks
	// boundary half-edges expecting a simple loop, but a non-manifold vertex
	// on the boundary creates a branching point, so the loop is never
	// completed and the hole is left unfilled.
	//
	// SplitNonManifoldVertices resolves this by duplicating the shared vertex
	// into one copy per fan, converting the non-simple boundary into multiple
	// simple loops that LSCM can then triangulate individually.
	//
	// ConnectFacets and ReorientFacetsAntiMoebius are run first because
	// SplitNonManifoldVertices requires consistent per-facet adjacency and
	// a coherent winding orientation to identify which triangles belong to
	// each independent fan at a shared vertex.
	//
	// This pass is intentionally NOT run unconditionally (only on Pass 1
	// failure) because SplitNonManifoldVertices can break intentional
	// multi-body topology where two bodies legitimately share a boundary edge
	// — duplicating that edge's vertices would tear the bodies apart.
	//
	// Move the preprocessed vectors into Pass 2 (they are not needed after
	// this point, so we avoid an extra copy of the vertex/triangle arrays).
	std::vector<gte::Vector3<double>> v2 = std::move(vertices);
	std::vector<std::array<int32_t, 3>> t2 = std::move(triangles);
	std::vector<int32_t> adj2;
	gte::MeshRepair<double>::ConnectFacets(t2, adj2);
	gte::MeshRepair<double>::ReorientFacetsAntiMoebius(v2, t2, adj2);
	gte::MeshRepair<double>::SplitNonManifoldVertices(v2, t2, adj2);
	if (!try_fill(v2, t2, area, gmanifold)) {
	    // Both passes failed to produce a manifold mesh
	    return -1;
	}
    }

    // Output is manifold, make a new bot
    manifold::MeshGL omesh = gmanifold.GetMeshGL();
    struct rt_bot_internal *nbot = manifold_to_bot(&omesh);

    // Once we have an rt_bot_internal, see what the solid raytracer's linting
    // thinks of this unless the user has explicitly told us not to do any
    // validation beyond the manifold check.  The above is enough for boolean
    // evaluation input, but won't necessarily clear all problem cases that
    // might arise in a solid raytrace.
    if (settings->strict) {
	int lint_ret = bot_repair_lint(nbot);
	if (lint_ret) {
	    bu_log("Error - new BoT does not pass lint test!\n");
	    rt_bot_internal_free(nbot);
	    BU_PUT(nbot, struct rt_bot_internal);
	    return -1;
	}
    }

    // Post-repair quality check and optional auto-remesh.
    //
    // When settings->auto_remesh is enabled, compute Verdict-style mesh quality
    // metrics (aspect ratio, scaled Jacobian, min angle) on the repaired mesh.
    // If the median aspect ratio exceeds the threshold, automatically remesh to
    // improve triangle quality.  The manifold property is re-verified after
    // remeshing; if it is lost the original repaired mesh is returned.
    //
    // CAUTION: On models with thin/elongated structural elements (stringers,
    // cross-supports) — such as GenericTwin — RemeshCVT frequently breaks the
    // manifold property (>80% of poor-quality shapes on GenericTwin fail the
    // manifold check after remesh).  For these shapes the high aspect ratio
    // reflects the physical geometry, not a repair defect; remeshing rarely
    // helps.  Therefore auto_remesh is intentionally opt-in (default off).
    if (settings->auto_remesh) {
	// Collect vertices and triangles from nbot for quality evaluation
	std::vector<gte::Vector3<double>> q_verts;
	std::vector<std::array<int32_t, 3>> q_tris;
	bot_to_gte(q_verts, q_tris, nbot);

	// Compute aggregate quality (using GTE-native Verdict-equivalent metrics)
	gte::MeshQuality<double>::MeshMetrics qm =
	    gte::MeshQuality<double>::ComputeMeshMetricsVec3(q_verts, q_tris);

	double ar_threshold = (settings->remesh_quality_ar > 0.0)
	    ? (double)settings->remesh_quality_ar
	    : 3.0;

	bool poor_quality = (qm.totalTriangles > 0 &&
			     qm.aspectRatio.median > ar_threshold);

	if (poor_quality) {
	    bu_log("rt_bot_repair: median aspect ratio %.2f exceeds %.2f — auto-remeshing\n",
		   qm.aspectRatio.median, ar_threshold);

	    // Target the same vertex count as the repaired mesh
	    size_t nb_pts = nbot->num_vertices;

	    gte::MeshRemesh<double>::Parameters remeshParams;
	    remeshParams.targetVertexCount = nb_pts;
	    remeshParams.useAnisotropic    = true;
	    remeshParams.anisotropyScale   = 0.04; // same as BRL-CAD remesh default
	    if (settings->remesh_time_limit > 0.0)
		remeshParams.lloydTimeLimit = (double)settings->remesh_time_limit;

	    // Remesh into separate output containers to avoid aliasing
	    std::vector<gte::Vector3<double>> rm_verts;
	    std::vector<std::array<int32_t, 3>> rm_tris;
	    bool remesh_ok = gte::MeshRemesh<double>::RemeshCVT(q_verts, q_tris, rm_verts, rm_tris, remeshParams);

	    if (!remesh_ok) {
		bu_log("rt_bot_repair: auto-remesh failed, keeping repaired mesh as-is\n");
	    } else {
		// Verify the remeshed result is still manifold.
		// If anisotropic remesh breaks manifold, retry with isotropic CVT.
		manifold::MeshGL grmm;
		gte_to_manifold(&grmm, rm_verts, rm_tris);
		manifold::Manifold grmanifold(grmm);
		bool remesh_manifold = (grmanifold.Status() == manifold::Manifold::Error::NoError);

		if (!remesh_manifold) {
		    // Anisotropic remesh broke manifold — retry with isotropic (3D CVT)
		    bu_log("rt_bot_repair: anisotropic remesh not manifold — retrying isotropic\n");
		    gte::MeshRemesh<double>::Parameters isoParams;
		    isoParams.targetVertexCount = nb_pts;
		    isoParams.useAnisotropic    = false;
		    if (settings->remesh_time_limit > 0.0)
			isoParams.lloydTimeLimit = (double)settings->remesh_time_limit;
		    std::vector<gte::Vector3<double>> iso_verts;
		    std::vector<std::array<int32_t, 3>> iso_tris;
		    bool iso_ok = gte::MeshRemesh<double>::RemeshCVT(q_verts, q_tris, iso_verts, iso_tris, isoParams);
		    if (iso_ok) {
			gte_to_manifold(&grmm, iso_verts, iso_tris);
			grmanifold = manifold::Manifold(grmm);
			if (grmanifold.Status() == manifold::Manifold::Error::NoError) {
			    rm_verts = std::move(iso_verts);
			    rm_tris  = std::move(iso_tris);
			    remesh_manifold = true;
			}
		    }
		    if (!remesh_manifold) {
			bu_log("rt_bot_repair: remeshed result is not manifold — keeping repaired mesh\n");
		    }
		}

		if (remesh_manifold) {
		    // Replace nbot with the remeshed version
		    manifold::MeshGL remeshed_mesh = grmanifold.GetMeshGL();
		    struct rt_bot_internal *remeshed_bot = manifold_to_bot(&remeshed_mesh);

		    // Run quality check on remeshed result for logging
		    std::vector<gte::Vector3<double>> r_verts;
		    std::vector<std::array<int32_t, 3>> r_tris;
		    bot_to_gte(r_verts, r_tris, remeshed_bot);		    gte::MeshQuality<double>::MeshMetrics qm2 =
			gte::MeshQuality<double>::ComputeMeshMetricsVec3(r_verts, r_tris);
		    bu_log("rt_bot_repair: remesh complete — median AR before: %.2f  after: %.2f\n",
			   qm.aspectRatio.median, qm2.aspectRatio.median);

		    rt_bot_internal_free(nbot);
		    BU_PUT(nbot, struct rt_bot_internal);
		    nbot = remeshed_bot;
		}
	    }
	}
    }

    *obot = nbot;
    return 0;
}


struct rt_bot_internal *
rt_bot_remove_faces(struct bu_ptbl *rm_face_indices, const struct rt_bot_internal *orig_bot)
{
    if (!rm_face_indices || !BU_PTBL_LEN(rm_face_indices))
	return NULL;


    std::unordered_set<size_t> rm_indices;
    for (size_t i = 0; i < BU_PTBL_LEN(rm_face_indices); i++) {
	size_t ind = (size_t)(uintptr_t)BU_PTBL_GET(rm_face_indices, i);
	rm_indices.insert(ind);
    }

    int *nfaces = (int *)bu_calloc(orig_bot->num_faces * 3, sizeof(int), "new faces array");
    size_t nfaces_ind = 0;
    for (size_t i = 0; i < orig_bot->num_faces; i++) {
	if (rm_indices.find(i) != rm_indices.end())
	    continue;
	nfaces[3*nfaces_ind + 0] = orig_bot->faces[3*i+0];
	nfaces[3*nfaces_ind + 1] = orig_bot->faces[3*i+1];
	nfaces[3*nfaces_ind + 2] = orig_bot->faces[3*i+2];
	nfaces_ind++;
    }

    // Having built a faces array with the specified triangles removed, we now
    // garbage collect to produce re-indexed face and point arrays with just the
    // active data (vertices may be no longer active in the BoT depending on
    // which faces were removed.
    int *nfacesarray = NULL;
    point_t *npointsarray = NULL;
    int npntcnt = 0;
    int new_num_faces = bg_trimesh_3d_gc(&nfacesarray, &npointsarray, &npntcnt, nfaces, nfaces_ind, (const point_t *)orig_bot->vertices);

    if (new_num_faces < 3) {
	new_num_faces = 0;
	npntcnt = 0;
	bu_free(nfacesarray, "nfacesarray");
	nfacesarray = NULL;
	bu_free(npointsarray, "npointsarray");
	npointsarray = NULL;
    }

    // Done with the nfaces array
    bu_free(nfaces, "free unmapped new faces array");

    // Make the new rt_bot_internal
    struct rt_bot_internal *bot = NULL;
    BU_GET(bot, struct rt_bot_internal);
    bot->magic = RT_BOT_INTERNAL_MAGIC;
    bot->mode = orig_bot->mode;
    bot->orientation = orig_bot->orientation;
    bot->bot_flags = orig_bot->bot_flags;
    bot->num_vertices = npntcnt;
    bot->num_faces = new_num_faces;
    bot->vertices = (fastf_t *)npointsarray;
    bot->faces = nfacesarray;

    // TODO - need to properly rebuild these arrays as well, if orig_bot has them - bg_trimesh_3d_gc only
    // handles the vertices themselves
    bot->thickness = NULL;
    bot->face_mode = NULL;
    bot->normals = NULL;
    bot->face_normals = NULL;
    bot->uvs = NULL;
    bot->face_uvs = NULL;

    return bot;
}

struct rt_bot_internal *
rt_bot_dup(const struct rt_bot_internal *obot)
{
    if (!obot)
	return NULL;

    struct rt_bot_internal *bot = NULL;
    BU_GET(bot, struct rt_bot_internal);
    bot->magic = obot->magic;
    bot->mode = obot->mode;
    bot->orientation = obot->orientation;
    bot->bot_flags = obot->bot_flags;

    bot->num_faces = obot->num_faces;
    bot->faces = (int *)bu_malloc(obot->num_faces * sizeof(int)*3, "bot faces");
    memcpy(bot->faces, obot->faces, obot->num_faces * sizeof(int)*3);

    bot->num_vertices = obot->num_vertices;
    bot->vertices = (fastf_t*)bu_malloc(obot->num_vertices * sizeof(fastf_t)*3, "bot verts");
    memcpy(bot->vertices, obot->vertices, obot->num_vertices * sizeof(fastf_t)*3);

    if (obot->thickness) {
	bot->thickness = (fastf_t*)bu_malloc(obot->num_faces * sizeof(fastf_t), "bot thicknesses");
	memcpy(bot->thickness, obot->thickness, obot->num_faces * sizeof(fastf_t));
    }

    if (obot->face_mode) {
	bot->face_mode = (struct bu_bitv *)bu_malloc(obot->num_faces * sizeof(struct bu_bitv), "bot face_mode");
	memcpy(bot->face_mode, obot->face_mode, obot->num_faces * sizeof(struct bu_bitv));
    }

    if (obot->normals && obot->num_normals) {
	bot->num_normals = obot->num_normals;
	bot->normals = (fastf_t*)bu_malloc(obot->num_normals * sizeof(fastf_t)*3, "bot normals");
	memcpy(bot->normals, obot->normals, obot->num_normals * sizeof(fastf_t)*3);
    }

    if (obot->face_normals && obot->num_face_normals) {
	bot->num_face_normals = obot->num_face_normals;
	bot->face_normals = (int*)bu_malloc(obot->num_face_normals * sizeof(int)*3, "bot face normals");
	memcpy(bot->face_normals, obot->face_normals, obot->num_face_normals * sizeof(int)*3);
    }

    if (obot->num_uvs && obot->uvs) {
	bot->num_uvs = obot->num_uvs;
	bot->uvs = (fastf_t*)bu_malloc(obot->num_uvs * sizeof(fastf_t)*3, "bot uvs");
	memcpy(bot->uvs, obot->uvs, obot->num_uvs * sizeof(fastf_t)*3);
    }

    if (obot->num_face_uvs && obot->face_uvs) {
	bot->num_face_uvs = obot->num_face_uvs;
	bot->face_uvs = (int*)bu_malloc(obot->num_face_uvs * sizeof(int)*3, "bot face_uvs");
	memcpy(bot->face_uvs, obot->face_uvs, obot->num_face_uvs * sizeof(int)*3);
    }

    return bot;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
