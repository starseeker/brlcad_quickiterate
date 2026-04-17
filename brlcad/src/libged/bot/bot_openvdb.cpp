/*                  B O T _ O P E N V D B . C P P
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
/** @file libged/bot/bot_openvdb.cpp
 *
 * Shared OpenVDB utilities for the bot plugin.
 *
 * Three building blocks are implemented here:
 *
 *  bot_to_sdf()                — BoT → OpenVDB signed-distance field
 *  sdf_to_bot()                — OpenVDB SDF → rt_bot_internal
 *  rt_rtip_to_occupancy_grid() — raytrace a prepped rt_i into a BoolGrid
 *
 * See bot_openvdb.h for the public API documentation.
 */

#include "common.h"

#ifdef BRLCAD_OPENVDB

#  include <cmath>
#  include <algorithm>
#  include <limits>

#  include <openvdb/openvdb.h>
#  include <openvdb/tools/MeshToVolume.h>
#  include <openvdb/tools/VolumeToMesh.h>

#  include "vmath.h"
#  include "bu/malloc.h"
#  include "bu/log.h"
#  include "rt/application.h"
#  include "rt/ray_partition.h"
#  include "rt/seg.h"
#  include "rt/geom.h"
#  include "raytrace.h"

#  include "bot_openvdb.h"


/* -----------------------------------------------------------------------
 * botDataAdapter
 *
 * Adapts an rt_bot_internal for use with openvdb::tools::meshToVolume.
 * meshToVolume expects world-space coordinates from getIndexSpacePoint
 * when a transform is supplied (the name "IndexSpace" in the OpenVDB
 * template interface is misleading — values are converted by the caller
 * using the provided transform).
 * ---------------------------------------------------------------------- */
struct botDataAdapter {
    struct rt_bot_internal *bot;

    size_t polygonCount() const { return bot->num_faces; }
    size_t pointCount()   const { return bot->num_vertices; }
    size_t vertexCount(size_t /*polygon*/) const { return 3; }

    void getIndexSpacePoint(size_t n, size_t v, openvdb::Vec3d &pos) const {
	int idx = bot->faces[(n * 3) + v];
	pos[X] = bot->vertices[(idx * 3) + X];
	pos[Y] = bot->vertices[(idx * 3) + Y];
	pos[Z] = bot->vertices[(idx * 3) + Z];
    }

    explicit botDataAdapter(struct rt_bot_internal *bip) : bot(bip) {}
};


/* -----------------------------------------------------------------------
 * bot_to_sdf
 * ---------------------------------------------------------------------- */
openvdb::FloatGrid::Ptr
bot_to_sdf(struct rt_bot_internal *bot, double voxel_size)
{
    if (!bot || !bot->num_vertices || !bot->num_faces)
	return openvdb::FloatGrid::Ptr();

    /* Auto-size: aim for ~100 voxels along the longest bbox dimension. */
    if (voxel_size <= 0.0) {
	double xmin = bot->vertices[0], xmax = bot->vertices[0];
	double ymin = bot->vertices[1], ymax = bot->vertices[1];
	double zmin = bot->vertices[2], zmax = bot->vertices[2];
	for (size_t i = 1; i < bot->num_vertices; i++) {
	    double x = bot->vertices[i * 3 + X];
	    double y = bot->vertices[i * 3 + Y];
	    double z = bot->vertices[i * 3 + Z];
	    if (x < xmin) xmin = x; if (x > xmax) xmax = x;
	    if (y < ymin) ymin = y; if (y > ymax) ymax = y;
	    if (z < zmin) zmin = z; if (z > zmax) zmax = z;
	}
	double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
	double diag = std::sqrt(dx*dx + dy*dy + dz*dz);
	voxel_size = (diag > 0.0) ? (diag / 100.0) : 1.0;
    }

    /* Use a narrow band (3 voxels) for both sides.  Deep interior voxels
     * default to the background value (positive = exterior), which is
     * correct for surface extraction via volumeToMesh at isoValue 0. */
    const float exteriorBandWidth = 3.0f;
    const float interiorBandWidth = 3.0f;

    openvdb::initialize();

    struct botDataAdapter bda(bot);
    openvdb::math::Transform::Ptr xform =
	openvdb::math::Transform::createLinearTransform(voxel_size);

    return openvdb::tools::meshToVolume<openvdb::FloatGrid>(
	bda, *xform, exteriorBandWidth, interiorBandWidth);
}


/* -----------------------------------------------------------------------
 * sdf_to_bot
 * ---------------------------------------------------------------------- */
struct rt_bot_internal *
sdf_to_bot(openvdb::FloatGrid::Ptr grid, double adaptivity)
{
    if (!grid)
	return NULL;

    if (adaptivity < 0.0) adaptivity = 0.0;
    if (adaptivity > 1.0) adaptivity = 1.0;

    std::vector<openvdb::Vec3s> points;
    std::vector<openvdb::Vec3I> triangles;
    std::vector<openvdb::Vec4I> quads;

    openvdb::tools::volumeToMesh(*grid, points, triangles, quads,
				 /*isoValue=*/0.0, adaptivity);

    if (points.empty())
	return NULL;

    /* Each quad is split into two triangles. */
    size_t n_tris = triangles.size() + quads.size() * 2;
    if (n_tris == 0)
	return NULL;

    struct rt_bot_internal *obot;
    BU_GET(obot, struct rt_bot_internal);
    obot->magic       = RT_BOT_INTERNAL_MAGIC;
    obot->mode        = RT_BOT_SOLID;
    obot->orientation = RT_BOT_CCW;
    obot->thickness   = NULL;
    obot->face_mode   = (struct bu_bitv *)NULL;
    obot->bot_flags   = 0;
    obot->normals     = NULL;
    obot->face_normals = NULL;
    obot->num_normals = 0;
    obot->num_face_normals = 0;

    obot->num_vertices = points.size();
    obot->vertices = (fastf_t *)bu_malloc(obot->num_vertices * 3 * sizeof(fastf_t),
					  "sdf_to_bot vertices");
    for (size_t i = 0; i < points.size(); i++) {
	obot->vertices[i * 3 + X] = points[i].x();
	obot->vertices[i * 3 + Y] = points[i].y();
	obot->vertices[i * 3 + Z] = points[i].z();
    }

    obot->num_faces = n_tris;
    obot->faces = (int *)bu_malloc(n_tris * 3 * sizeof(int), "sdf_to_bot faces");

    /* Copy triangles directly. */
    for (size_t i = 0; i < triangles.size(); i++) {
	obot->faces[i * 3 + X] = triangles[i].x();
	obot->faces[i * 3 + Y] = triangles[i].y();
	obot->faces[i * 3 + Z] = triangles[i].z();
    }

    /* Split each quad into two triangles.
     * The fan is: (q[0],q[1],q[2]) and (q[0],q[2],q[3]).
     * Use i*2 offsets (not i and i+1) to avoid the off-by-one bug
     * where every second quad overwrites the previous triangle. */
    size_t ntri = triangles.size();
    for (size_t i = 0; i < quads.size(); i++) {
	size_t base = ntri + i * 2;
	obot->faces[base * 3 + X] = quads[i][0];
	obot->faces[base * 3 + Y] = quads[i][1];
	obot->faces[base * 3 + Z] = quads[i][2];

	obot->faces[(base + 1) * 3 + X] = quads[i][0];
	obot->faces[(base + 1) * 3 + Y] = quads[i][2];
	obot->faces[(base + 1) * 3 + Z] = quads[i][3];
    }

    return obot;
}


/* -----------------------------------------------------------------------
 * rt_rtip_to_occupancy_grid internals
 *
 * These hit/miss callbacks and the per-ray context are used only by
 * rt_rtip_to_occupancy_grid below.
 * ---------------------------------------------------------------------- */

struct OccRayCtx {
    openvdb::BoolGrid::Accessor *acc;
    double ray_origin_axis;
    double mdl_min_axis;
    double voxel_size;
    int idx_a_padded;
    int idx_b_padded;
    int n_axis;
    int axis; /* 0=+X, 1=+Y, 2=+Z */
};


static int
occ_miss(struct application *UNUSED(ap))
{
    return 0;
}


static int
occ_hit(struct application *ap, struct partition *PartHeadp, struct seg *UNUSED(seg))
{
    struct OccRayCtx *ctx = (struct OccRayCtx *)ap->a_uptr;
    openvdb::BoolGrid::Accessor &acc = *ctx->acc;

    for (struct partition *pp = PartHeadp->pt_forw;
	 pp != PartHeadp;
	 pp = pp->pt_forw) {
	double in_world  = ctx->ray_origin_axis + pp->pt_inhit->hit_dist;
	double out_world = ctx->ray_origin_axis + pp->pt_outhit->hit_dist;

	/* Use floor() — C cast truncates toward zero, not -inf. */
	int v_in  = (int)floor((in_world  - ctx->mdl_min_axis) / ctx->voxel_size) + 1;
	int v_out = (int)floor((out_world - ctx->mdl_min_axis) / ctx->voxel_size) + 1;

	if (v_in  < 1)                v_in  = 1;
	if (v_out >= ctx->n_axis - 1) v_out = ctx->n_axis - 2;
	if (v_in  > v_out)            continue;

	for (int v = v_in; v <= v_out; v++) {
	    openvdb::Coord c;
	    switch (ctx->axis) {
		case 0:  c = openvdb::Coord(v, ctx->idx_a_padded, ctx->idx_b_padded); break;
		case 1:  c = openvdb::Coord(ctx->idx_b_padded, v, ctx->idx_a_padded); break;
		default: c = openvdb::Coord(ctx->idx_a_padded, ctx->idx_b_padded, v); break;
	    }
	    acc.setValue(c, true);
	}
    }
    return 1;
}


/* -----------------------------------------------------------------------
 * rt_rtip_to_occupancy_grid
 * ---------------------------------------------------------------------- */
openvdb::BoolGrid::Ptr
rt_rtip_to_occupancy_grid(struct rt_i *rtip, double voxel_size,
			  int *nx, int *ny, int *nz)
{
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
    ap.a_onehit = 0;
    ap.a_uptr   = &ctx;

    /* Pass 1: +X rays (one per Y-Z column). */
    ctx.axis            = 0;
    ctx.mdl_min_axis    = rtip->mdl_min[X];
    ctx.n_axis          = *nx;
    ctx.ray_origin_axis = rtip->mdl_min[X] - voxel_size;

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

    /* Pass 2: +Y rays (one per X-Z column). */
    ctx.axis            = 1;
    ctx.mdl_min_axis    = rtip->mdl_min[Y];
    ctx.n_axis          = *ny;
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

    /* Pass 3: +Z rays (one per X-Y column). */
    ctx.axis            = 2;
    ctx.mdl_min_axis    = rtip->mdl_min[Z];
    ctx.n_axis          = *nz;
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

#endif /* BRLCAD_OPENVDB */


/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
