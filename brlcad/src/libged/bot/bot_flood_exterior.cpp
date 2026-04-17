/*              B O T _ F L O O D _ E X T E R I O R . C P P
 * BRL-CAD
 *
 * Copyright (c) 2024-2025 United States Government as represented by
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
/** @file libged/bot/bot_flood_exterior.cpp
 *
 * Tier-2 exterior face classification: "would get wet if submerged."
 *
 * A face is exterior when there exists a path of empty (non-solid)
 * voxels connecting the face to the unbounded exterior space. This
 * correctly handles narrow channels, small holes, and nested shells
 * that the tier-1 ray-sampling approach may miss.
 *
 * Algorithm
 * ---------
 *  1. Build an occupancy grid (solid / air) from the BRL-CAD raytrace.
 *     Rays are shot in +X for each (Y,Z) column. A voxel is marked solid
 *     when any hit segment encloses it. Additional +Y and +Z passes are
 *     shot to capture thin walls parallel to the X axis.
 *
 *  2. BFS flood-fill starting from the corner of the padded grid boundary,
 *     expanding through air voxels that have not been visited yet.
 *
 *  3. A face is exterior if any of its six face-adjacent voxels was reached
 *     by the flood fill.
 *
 * Design note
 * -----------
 * The voxelization helper (rt_rtip_to_occupancy_grid) is designed to be
 * reusable outside of the bot exterior context. It takes only a prepped
 * rt_i and a voxel size, and returns an openvdb::BoolGrid whose active
 * voxels are the "solid interior" cells of the geometry. The plan is to
 * move this helper to librt once the interface stabilises so that it can
 * serve as a foundation for a voxel-based manifold mesh generation fallback.
 *
 * TODO: move rt_rtip_to_occupancy_grid to a librt header once the
 *       interface is considered stable.
 */

#include "common.h"

#include <cmath>
#include <algorithm>
#include <queue>

/* Suppress warnings from OpenVDB and its transitive headers. */
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wshadow"
#  pragma GCC diagnostic ignored "-Wpedantic"
#  pragma GCC diagnostic ignored "-Wignored-attributes"
#endif

#include <openvdb/openvdb.h>

#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

#include "vmath.h"
#include "rt/application.h"
#include "rt/ray_partition.h"
#include "rt/seg.h"
#include "rt/geom.h"
#include "raytrace.h"
#include "bu/log.h"


/* -----------------------------------------------------------------------
 * Internal hit/miss callbacks for occupancy grid building
 * ---------------------------------------------------------------------- */

/*
 * Per-ray context shared between the application struct and our callbacks.
 * Updated for every ray shot, so the hit callback knows which voxel column
 * to mark.
 */
struct OccRayCtx {
    openvdb::BoolGrid::Accessor *acc; /* write accessor for solid grid */
    double ray_origin_axis;           /* world coord of ray start along shoot axis */
    double mdl_min_axis;              /* model bbox min along shoot axis */
    double voxel_size;
    int idx_a_padded;                 /* voxel index along first transverse axis (+padding) */
    int idx_b_padded;                 /* voxel index along second transverse axis (+padding) */
    int n_axis;                       /* grid size along shoot axis (including padding) */
    int axis;                         /* 0=+X, 1=+Y, 2=+Z */
};


static int
occ_miss(struct application *UNUSED(ap))
{
    return 0;
}


/*
 * For each solid partition, compute the range of voxels along the shoot
 * axis that are inside the geometry and mark them in the occupancy grid.
 */
static int
occ_hit(struct application *ap, struct partition *PartHeadp, struct seg *UNUSED(seg))
{
    struct OccRayCtx *ctx = (struct OccRayCtx *)ap->a_uptr;
    openvdb::BoolGrid::Accessor &acc = *ctx->acc;

    for (struct partition *pp = PartHeadp->pt_forw; pp != PartHeadp; pp = pp->pt_forw) {
	double in_world  = ctx->ray_origin_axis + pp->pt_inhit->hit_dist;
	double out_world = ctx->ray_origin_axis + pp->pt_outhit->hit_dist;

	/* Convert world coordinates to padded voxel indices. */
	int v_in  = (int)((in_world  - ctx->mdl_min_axis) / ctx->voxel_size) + 1;
	int v_out = (int)((out_world - ctx->mdl_min_axis) / ctx->voxel_size) + 1;

	/* Clamp to valid range (inside padding). */
	if (v_in  < 1)                  v_in  = 1;
	if (v_out >= ctx->n_axis - 1)   v_out = ctx->n_axis - 2;
	if (v_in  > v_out)              continue;

	for (int v = v_in; v <= v_out; v++) {
	    openvdb::Coord c;
	    switch (ctx->axis) {
		case 0: c = openvdb::Coord(v, ctx->idx_a_padded, ctx->idx_b_padded); break;
		case 1: c = openvdb::Coord(ctx->idx_b_padded, v, ctx->idx_a_padded); break;
		default: c = openvdb::Coord(ctx->idx_a_padded, ctx->idx_b_padded, v); break;
	    }
	    acc.setValue(c, true);
	}
    }
    return 1;
}


/* -----------------------------------------------------------------------
 * rt_rtip_to_occupancy_grid
 *
 * Build an OpenVDB BoolGrid from a prepped rt_i.  Active (true) voxels
 * are cells that lie inside the solid geometry.  The grid is padded by
 * one cell on every side so that the BFS seed can always start outside.
 *
 * Parameters
 *   rtip        - prepped raytrace instance (rt_prep already called)
 *   voxel_size  - size of each voxel in model units (> 0)
 *   nx/ny/nz    - output: grid dimensions (caller-supplied to avoid
 *                 recomputing them in the flood-fill step)
 *
 * The returned grid uses grid-index coordinates with origin at the
 * padded corner.  Index (1, 1, 1) corresponds to the model-space point
 * (mdl_min[X], mdl_min[Y], mdl_min[Z]).
 * ---------------------------------------------------------------------- */
static openvdb::BoolGrid::Ptr
rt_rtip_to_occupancy_grid(struct rt_i *rtip, double voxel_size, int *nx, int *ny, int *nz)
{
    /* Grid dimensions: model cells + 1 padding cell on each side. */
    int nx_m = (int)std::ceil((rtip->mdl_max[X] - rtip->mdl_min[X]) / voxel_size);
    int ny_m = (int)std::ceil((rtip->mdl_max[Y] - rtip->mdl_min[Y]) / voxel_size);
    int nz_m = (int)std::ceil((rtip->mdl_max[Z] - rtip->mdl_min[Z]) / voxel_size);

    if (nx_m < 1) nx_m = 1;
    if (ny_m < 1) ny_m = 1;
    if (nz_m < 1) nz_m = 1;

    *nx = nx_m + 2;
    *ny = ny_m + 2;
    *nz = nz_m + 2;

    openvdb::BoolGrid::Ptr solid = openvdb::BoolGrid::create(false);
    openvdb::BoolGrid::Accessor acc = solid->getAccessor();

    struct OccRayCtx ctx;
    ctx.acc        = &acc;
    ctx.voxel_size = voxel_size;

    struct application ap;
    RT_APPLICATION_INIT(&ap);
    ap.a_rt_i   = rtip;
    ap.a_hit    = occ_hit;
    ap.a_miss   = occ_miss;
    ap.a_onehit = 0;    /* collect all hits */
    ap.a_uptr   = &ctx;

    /* --- Pass 1: shoot rays in +X (one ray per Y-Z voxel column) --- */
    ctx.axis         = 0;
    ctx.mdl_min_axis = rtip->mdl_min[X];
    ctx.n_axis       = *nx;
    ctx.ray_origin_axis = rtip->mdl_min[X] - voxel_size; /* one cell before bbox */

    for (int k = 0; k < nz_m; k++) {
	ctx.idx_b_padded = k + 1;
	for (int j = 0; j < ny_m; j++) {
	    ctx.idx_a_padded = j + 1;
	    VSET(ap.a_ray.r_pt,
		 ctx.ray_origin_axis,
		 rtip->mdl_min[Y] + (j + 0.5) * voxel_size,
		 rtip->mdl_min[Z] + (k + 0.5) * voxel_size);
	    VSET(ap.a_ray.r_dir, 1.0, 0.0, 0.0);
	    rt_shootray(&ap);
	}
    }

    /* --- Pass 2: shoot rays in +Y (one ray per X-Z voxel column) --- */
    ctx.axis         = 1;
    ctx.mdl_min_axis = rtip->mdl_min[Y];
    ctx.n_axis       = *ny;
    ctx.ray_origin_axis = rtip->mdl_min[Y] - voxel_size;

    for (int k = 0; k < nz_m; k++) {
	ctx.idx_a_padded = k + 1;
	for (int i = 0; i < nx_m; i++) {
	    ctx.idx_b_padded = i + 1;
	    VSET(ap.a_ray.r_pt,
		 rtip->mdl_min[X] + (i + 0.5) * voxel_size,
		 ctx.ray_origin_axis,
		 rtip->mdl_min[Z] + (k + 0.5) * voxel_size);
	    VSET(ap.a_ray.r_dir, 0.0, 1.0, 0.0);
	    rt_shootray(&ap);
	}
    }

    /* --- Pass 3: shoot rays in +Z (one ray per X-Y voxel column) --- */
    ctx.axis         = 2;
    ctx.mdl_min_axis = rtip->mdl_min[Z];
    ctx.n_axis       = *nz;
    ctx.ray_origin_axis = rtip->mdl_min[Z] - voxel_size;

    for (int j = 0; j < ny_m; j++) {
	ctx.idx_b_padded = j + 1;
	for (int i = 0; i < nx_m; i++) {
	    ctx.idx_a_padded = i + 1;
	    VSET(ap.a_ray.r_pt,
		 rtip->mdl_min[X] + (i + 0.5) * voxel_size,
		 rtip->mdl_min[Y] + (j + 0.5) * voxel_size,
		 ctx.ray_origin_axis);
	    VSET(ap.a_ray.r_dir, 0.0, 0.0, 1.0);
	    rt_shootray(&ap);
	}
    }

    return solid;
}


/* -----------------------------------------------------------------------
 * BFS flood fill from a corner of the padded region.
 *
 * Returns a BoolGrid whose active voxels are all empty-space cells
 * reachable from the exterior without crossing a solid voxel.
 * On return, *n_exterior is set to the number of exterior voxels found.
 * ---------------------------------------------------------------------- */
static openvdb::BoolGrid::Ptr
flood_fill_exterior(openvdb::BoolGrid::Ptr solid, int nx, int ny, int nz,
		    long long *n_exterior)
{
    openvdb::BoolGrid::Ptr exterior = openvdb::BoolGrid::create(false);
    openvdb::BoolGrid::Accessor        ext_acc   = exterior->getAccessor();
    openvdb::BoolGrid::ConstAccessor   solid_acc = solid->getConstAccessor();

    /* Seed: (0, 0, 0) is always in the padding layer, outside the model. */
    std::queue<openvdb::Coord> q;
    openvdb::Coord seed(0, 0, 0);
    ext_acc.setValue(seed, true);
    q.push(seed);
    long long count = 1;

    /* 6-connected face neighbors. */
    static const int D[6][3] = {
	{ 1,0,0}, {-1,0,0},
	{ 0,1,0}, { 0,-1,0},
	{ 0,0,1}, { 0,0,-1}
    };

    while (!q.empty()) {
	openvdb::Coord c = q.front();
	q.pop();

	for (int d = 0; d < 6; d++) {
	    openvdb::Coord nbr(c[0]+D[d][0], c[1]+D[d][1], c[2]+D[d][2]);

	    /* Bounds check against grid extents. */
	    if (nbr[0] < 0 || nbr[0] >= nx ||
		nbr[1] < 0 || nbr[1] >= ny ||
		nbr[2] < 0 || nbr[2] >= nz)
		continue;

	    /* Stop at solid walls. */
	    if (solid_acc.getValue(nbr))
		continue;

	    /* Skip already-visited exterior voxels. */
	    if (ext_acc.getValue(nbr))
		continue;

	    ext_acc.setValue(nbr, true);
	    q.push(nbr);
	    count++;
	}
    }

    *n_exterior = count;
    return exterior;
}


/* -----------------------------------------------------------------------
 * C-callable entry point
 * ---------------------------------------------------------------------- */

extern "C" int
bot_flood_exterior_classify(struct rt_i *rtip,
			    struct rt_bot_internal *bot,
			    int *face_exterior,
			    double voxel_size)
{
    if (!rtip || !bot || !face_exterior)
	return -1;

    /* Auto-size: aim for ~100 voxels along the shortest dimension. */
    if (voxel_size <= 0.0) {
	double dx = rtip->mdl_max[X] - rtip->mdl_min[X];
	double dy = rtip->mdl_max[Y] - rtip->mdl_min[Y];
	double dz = rtip->mdl_max[Z] - rtip->mdl_min[Z];
	double min_dim = std::min({dx, dy, dz});
	voxel_size = (min_dim > 0.0) ? (min_dim / 100.0) : 1.0;
    }

    int nx = 0, ny = 0, nz = 0;

    /* Must call openvdb::initialize() before using any OpenVDB type. */
    openvdb::initialize();

    /* Step 1: voxelise the model into a solid-occupancy grid. */
    bu_log("bot flood exterior: voxel_size=%.4g\n", voxel_size);
    openvdb::BoolGrid::Ptr solid = rt_rtip_to_occupancy_grid(rtip, voxel_size, &nx, &ny, &nz);
    bu_log("bot flood exterior: grid %dx%dx%d\n", nx, ny, nz);

    /* Step 2: BFS flood fill to find exterior (water-reachable) voxels. */
    long long n_ext_vox = 0;
    openvdb::BoolGrid::Ptr exterior = flood_fill_exterior(solid, nx, ny, nz, &n_ext_vox);
    bu_log("bot flood exterior: %lld exterior voxels\n", n_ext_vox);

    /* Step 3: For each face, mark it exterior if any face-adjacent voxel
     * (in the 6-connected sense) is in the exterior set. */
    openvdb::BoolGrid::ConstAccessor ext_acc = exterior->getConstAccessor();

    static const int D[6][3] = {
	{ 1,0,0}, {-1,0,0},
	{ 0,1,0}, { 0,-1,0},
	{ 0,0,1}, { 0,0,-1}
    };

    int num_exterior = 0;
    for (size_t fi = 0; fi < bot->num_faces; fi++) {
	int vi0 = bot->faces[fi*3+0];
	int vi1 = bot->faces[fi*3+1];
	int vi2 = bot->faces[fi*3+2];

	/* Face centroid. */
	double cx = (bot->vertices[vi0*3+X] + bot->vertices[vi1*3+X] + bot->vertices[vi2*3+X]) / 3.0;
	double cy = (bot->vertices[vi0*3+Y] + bot->vertices[vi1*3+Y] + bot->vertices[vi2*3+Y]) / 3.0;
	double cz = (bot->vertices[vi0*3+Z] + bot->vertices[vi1*3+Z] + bot->vertices[vi2*3+Z]) / 3.0;

	/* Padded voxel index of centroid (origin offset by 1 for padding). */
	int ix = (int)((cx - rtip->mdl_min[X]) / voxel_size) + 1;
	int iy = (int)((cy - rtip->mdl_min[Y]) / voxel_size) + 1;
	int iz = (int)((cz - rtip->mdl_min[Z]) / voxel_size) + 1;

	/* Check all six face-adjacent voxels. */
	face_exterior[fi] = 0;
	for (int d = 0; d < 6 && !face_exterior[fi]; d++) {
	    openvdb::Coord nbr(ix+D[d][0], iy+D[d][1], iz+D[d][2]);
	    if (nbr[0] >= 0 && nbr[0] < nx &&
		nbr[1] >= 0 && nbr[1] < ny &&
		nbr[2] >= 0 && nbr[2] < nz &&
		ext_acc.getValue(nbr)) {
		face_exterior[fi] = 1;
	    }
	}

	if (face_exterior[fi])
	    num_exterior++;
    }

    return num_exterior;
}


/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
