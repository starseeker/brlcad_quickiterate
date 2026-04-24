/*                        S H O O T . H
 * BRL-CAD
 *
 * Copyright (c) 1993-2026 United States Government as represented by
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

/** @addtogroup rt_shoot
 *
 * @brief
 * Ray Tracing program shot coordinator. This is the heart of LIBRT's ray-tracing capability.
 *
 * Given a ray, shoot it at all the relevant parts of the model,
 * (building the finished_segs chain), and then call rt_boolregions()
 * to build and evaluate the partition chain.  If the ray actually hit
 * anything, call the application's a_hit() routine with a pointer to
 * the partition chain, otherwise, call the application's a_miss()
 * routine.
 *
 * It is important to note that rays extend infinitely only in the
 * positive direction.  The ray is composed of all points P, where
 *
 * P = r_pt + K * r_dir
 *
 * for K ranging from 0 to +infinity.  There is no looking backwards.
 *

 * */
/** @{ */
/** @file rt/shoot.h */

#ifndef RT_SHOOT_H
#define RT_SHOOT_H

#include "common.h"
#include "vmath.h"
#include "rt/defines.h"
#include "rt/db_instance.h"
#include "rt/application.h"
#include "rt/geom.h"
#include "rt/xray.h"

__BEGIN_DECLS

/**
 * @brief
 * Shoot a ray
 *
 * Note that the direction vector r_dir must have unit length; this is
 * mandatory, and is not ordinarily checked, in the name of
 * efficiency.
 *
 * Input:  Pointer to an application structure, with these mandatory fields:
 * a_ray.r_pt ==> Starting point of ray to be fired
 * a_ray.r_dir => UNIT VECTOR with direction to fire in (dir cosines)
 * a_hit =======> Routine to call when something is hit
 * a_miss ======> Routine to call when ray misses everything
 *
 * Calls user's a_miss() or a_hit() routine as appropriate passing
 * a_hit() a list of partitions intersected.  Note that only the
 * hit_dist elements of pt_inhit and pt_outhit are computed.  To
 * compute both hit_point and hit_normal, use:
 *
 * RT_HIT_NORMAL(NULL, hitp, stp, rayp, 0);
 *
 * To compute just the hit_point, use:
 *
 * VJOIN1(hitp->hit_point, rp->r_pt, hitp->hit_dist, rp->r_dir);
 *
 * These calculations are deferred to user code to avoid needless
 * computation in other ray situations.
 *
 * Formal Return: whatever the application function returns (an int).
 *
 * NOTE: The application functions may call rt_shootray() recursively.
 * Thus, none of the local variables may be static.
 *
 * To prevent having to lock the statistics variables in a PARALLEL
 * environment, all the statistics variables have been moved into the
 * 'resource' structure, which is allocated per-CPU.
 */
RT_EXPORT extern int rt_shootray(struct application *ap);


/**
 * @brief
 * Shoot a bundle of rays
 *
 * Function for shooting a bundle of rays. Iteratively walks list of
 * rays contained in the application bundles xrays field 'b_rays'
 * passing each single ray to r_shootray().
 *
 * Input:
 *
 * bundle -  Pointer to an application_bundle structure.
 *
 * b_ap - Members in this single ray application structure should be
 * set in a similar fashion as when used with rt_shootray() with the
 * exception of a_hit() and a_miss(). Default implementations of these
 * routines are provided that simple update hit/miss counters and
 * attach the hit partitions and segments to the partition_bundle
 * structure. Users can still override this default functionality but
 * have to make sure to move the partition and segment list to the new
 * partition_bundle structure.
 *
 * b_hit() Routine to call when something is hit by the ray bundle.
 *
 * b_miss() Routine to call when ray bundle misses everything.
 *
 */
RT_EXPORT extern int rt_shootrays(struct application_bundle *bundle);


/**
 * Shoot a single ray and return the partition list. Handles callback
 * issues.
 *
 * Note that it calls malloc(), therefore should NOT be used if
 * performance matters.
 */
RT_EXPORT extern struct partition *rt_shootray_simple(struct application *ap,
						      point_t origin,
						      vect_t direction);


/**
 * PRIVATE: this is new API and should be considered private for the
 * time being.
 */
RT_EXPORT extern int rt_shootray_bundle(struct application *ap, struct xray *rays, int nrays);

/**
 * Reset all ray-shooting counters in rtip->stats to zero.
 *
 * Zeros the per-campaign statistics counters so that a new shooting
 * campaign starts from a clean baseline.  The geometry counts
* (nregions, nsolids) and space-partition statistics are not affected.
 *
 * Call this before launching workers for each new frame / campaign
 * whenever per-campaign statistics are needed.
 */
RT_EXPORT extern void rt_zero_ray_stats(struct rt_i *rtip);


/**
 * DEPRECATED - statistics are now incremented directly on rtip->stats
 * using C11 atomic operations during rt_shootray().  This function is
 * a no-op retained for source compatibility; it will be removed in a
 * future release.
 *
 * Previously this function tallied per-resource counters into the rt
 * instance structure.  That step is no longer required.
 */
DEPRECATED RT_EXPORT extern void rt_add_res_stats(struct rt_i *rtip,
				       struct resource *resp);
/**
 * DEPRECATED - statistics are now maintained on rtip->stats directly;
 * there are no per-resource stat fields to zero.  This function is a
 * no-op retained for source compatibility; it will be removed in a
 * future release.
 */
DEPRECATED RT_EXPORT extern void rt_zero_res_stats(struct resource *resp);


/* rt_res_pieces_clean() and rt_res_pieces_init() were removed in
 * Phase 6 of the struct resource removal effort.  Their replacements
 * rt_ap_pieces_clean() and rt_ap_pieces_init() are declared below.
 */

/**
 * Allocate and initialize the piece-shooting state for an application.
 *
 * This is called automatically by rt_shootray() but may also be
 * invoked explicitly when a model is known to contain piece-capable
 * solids.  If @p ap->a_pieces already exists for a different @p rtip
 * or a different piece count (e.g. after rt_reprep()), the old state
 * is freed and replaced transparently.
 *
 * Safe to call multiple times; it is a no-op when the existing state
 * already matches @p rtip and rti_nsolids_with_pieces.
 */
RT_EXPORT extern void rt_ap_pieces_init(struct application *ap, struct rt_i *rtip);

/**
 * Free the piece-shooting state attached to an application.
 *
 * This should be called when the application is no longer needed or
 * before the owning rt_i instance is destroyed.  Safe to call even
 * if @p ap->a_pieces is NULL.
 */
RT_EXPORT extern void rt_ap_pieces_clean(struct application *ap);

RT_EXPORT extern void rt_vstub(struct soltab *stp[],
			       struct xray *rp[],
			       struct seg segp[],
			       int n,
			       struct application *ap);


// Flags for rt_gen_obj_pnts
#define RT_GEN_OBJ_PNTS_SURF  0x1 /**< @brief save only the first and last hit point on a ray */
#define RT_GEN_OBJ_PNTS_GRID  0x2 /**< @brief sample using an XYZ grid based on the bounding box (default if no method flags are specified) */
#define RT_GEN_OBJ_PNTS_RAND  0x4 /**< @brief sample using Marsaglia sampling on the bounding sphere with pseudo random numbers */
#define RT_GEN_OBJ_PNTS_SOBOL 0x8 /**< @brief sample using Marsaglia sampling on the bounding sphere with Sobol' low-discrepancy-sequence generation */
/**
 * Generate points from a .g object using raytracing.  Various sampling options
 * are controlled via setting flags.
 *
 * Returns: 0 = success, -1 error */
RT_EXPORT extern int
rt_gen_obj_pnts(struct rt_pnts_internal *rpnts, fastf_t *avg_thickness, struct db_i *dbip,
       const char *obj, struct bn_tol *tol, int flags, int max_pnts, int max_time, int verbosity);


// NOTE:  For now this is exposed for testing in libged, but eventually
// it should be hidden behind the functab methods - NOT to be considered
// as public API.
RT_EXPORT extern int
rt_sample_pnts(struct bv_scene_obj *s, struct rt_db_internal *ip);

#ifdef USE_OPENCL
struct cl_hit {
    cl_double3 hit_point;
    cl_double3 hit_normal;
    cl_double3 hit_vpriv;
    cl_double hit_dist;
    cl_int hit_surfno;
};

struct cl_seg {
    struct cl_hit seg_in;
    struct cl_hit seg_out;
    cl_uint seg_sti;
};

struct cl_partition {
    struct cl_hit inhit;
    struct cl_hit outhit;
    cl_uint inseg;
    cl_uint outseg;
    cl_uint forw_pp;                /* index to the next partition */
    cl_uint back_pp;                /* index to the previous partition */
    cl_uint region_id;              /* id of the "owning" region */
    cl_char inflip;                 /* flip inhit->hit_normal */
    cl_char outflip;                /* flip outhit->hit_normal */
};

RT_EXPORT extern void
clt_frame(void *pixels, uint8_t o[2], int cur_pixel, int last_pixel,
	  int width, int ibackground[3], int inonbackground[3],
	  double airdensity, double haze[3], fastf_t gamma,
	  mat_t view2model, fastf_t cell_width, fastf_t cell_height,
	  fastf_t aspect, int lightmodel, int a_no_booleans);
#endif



__END_DECLS

#endif /* RT_SHOOT_H */
/** @} */
/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
