/*                     C R O F T O N . C P P
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @file libanalyze/crofton.cpp
 *
 * Cauchy-Crofton sampling estimator for surface area and volume.
 *
 * The method generates pairs of uniformly random points on the bounding
 * sphere and shoots a ray from each pair through the geometry.  The
 * Cauchy-Crofton integral-geometry formula then gives:
 *
 *   SA = 4*pi*R^2 * N_crossings / (2 * N_rays)
 *   V  = pi * R  * total_chord / N_rays
 *
 * where N_crossings counts every entry AND exit hit event (2 per solid
 * segment for a non-self-intersecting surface), total_chord is the sum
 * of solid segment lengths, and R is the bounding-sphere radius.
 *
 * Reference:
 *   Li et al. (2003), "Using low-discrepancy sequences and the Crofton
 *   formula to compute surface areas of geometric models",
 *   Computer-Aided Design 35, 771-782.
 *
 *   Liu et al. (2010), "Surface area estimation of digitized 3D objects
 *   using quasi-Monte Carlo methods", Pattern Recognition 43, 3900-3909.
 */

#include "common.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "vmath.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/parallel.h"
#include "bu/vls.h"
#include "raytrace.h"
#include "analyze.h"


/* ------------------------------------------------------------------ */
/* Internal types                                                       */
/* ------------------------------------------------------------------ */

struct crofton_ray {
    point_t r_pt;
    vect_t  r_dir;
};

struct crofton_shared {
    /* Accumulated across all rays/threads */
    size_t  total_crossings; /* in+out hit events */
    double  total_chord;     /* solid segment length sum (mm) */
    size_t  total_rays;      /* rays fired (hits + misses) */
    /* Synchronisation */
    int     sem_stats;
};

struct crofton_worker_data {
    struct application    *ap;        /* per-CPU application struct */
    struct crofton_ray    *rays;      /* shared ray array (read-only) */
    size_t                 start;
    size_t                 end;
    struct crofton_shared *shared;
};


/* ------------------------------------------------------------------ */
/* Hit / miss callbacks                                                 */
/* ------------------------------------------------------------------ */

static int
crofton_hit(struct application *ap, struct partition *PartHeadp, struct seg *UNUSED(segs))
{
    struct crofton_worker_data *wd = (struct crofton_worker_data *)ap->a_uptr;
    struct crofton_shared      *sh = wd->shared;

    size_t crossings = 0;
    double chord     = 0.0;

    struct partition *pp;
    for (pp = PartHeadp->pt_forw; pp != PartHeadp; pp = pp->pt_forw) {
	/* Each partition contributes an in-hit and an out-hit */
	crossings += 2;
	chord += pp->pt_outhit->hit_dist - pp->pt_inhit->hit_dist;
    }

    bu_semaphore_acquire(sh->sem_stats);
    sh->total_crossings += crossings;
    sh->total_chord     += chord;
    sh->total_rays      += 1;
    bu_semaphore_release(sh->sem_stats);

    return 1;
}


static int
crofton_miss(struct application *ap)
{
    struct crofton_worker_data *wd = (struct crofton_worker_data *)ap->a_uptr;
    struct crofton_shared      *sh = wd->shared;

    bu_semaphore_acquire(sh->sem_stats);
    sh->total_rays += 1;
    bu_semaphore_release(sh->sem_stats);

    return 0;
}


/* ------------------------------------------------------------------ */
/* Parallel worker                                                      */
/* ------------------------------------------------------------------ */

static void
crofton_worker(int id, void *data)
{
    struct crofton_worker_data *wd = &((struct crofton_worker_data *)data)[id - 1];
    struct application         *ap = wd->ap;
    struct crofton_ray         *rays = wd->rays;

    for (size_t i = wd->start; i < wd->end; i++) {
	VMOVE(ap->a_ray.r_pt,  rays[i].r_pt);
	VMOVE(ap->a_ray.r_dir, rays[i].r_dir);
	/* r_dir is already unit-length (set during ray generation) */
	rt_shootray(ap);
    }
}


/* ------------------------------------------------------------------ */
/* Point / ray generation                                               */
/* ------------------------------------------------------------------ */

static double
crofton_rand01(void)
{
    return rand() / (RAND_MAX + 1.0);
}


static void
random_point_on_sphere(double radius, const point_t center, point_t out)
{
    double theta = 2.0 * M_PI * crofton_rand01();
    double phi   = acos(2.0 * crofton_rand01() - 1.0);
    double sp    = sin(phi);
    out[X] = center[X] + radius * sp * cos(theta);
    out[Y] = center[Y] + radius * sp * sin(theta);
    out[Z] = center[Z] + radius * cos(phi);
}


/**
 * Generate @p nrays chord rays from @p npts random points on the
 * bounding sphere.  Each ray connects one randomly chosen point to
 * another.
 *
 * npts must be even, and nrays == npts/2.
 */
static void
generate_rays(struct crofton_ray *rays, size_t nrays,
	      double radius, const point_t center)
{
    size_t npts = nrays * 2;

    point_t *pts = (point_t *)bu_calloc(npts, sizeof(point_t), "crofton pts");
    for (size_t i = 0; i < npts; i++)
	random_point_on_sphere(radius, center, pts[i]);

    /* Shuffle so pairing is random */
    for (size_t i = npts - 1; i > 0; i--) {
	size_t j = (size_t)(crofton_rand01() * (i + 1));
	if (j > i) j = i;
	point_t tmp;
	VMOVE(tmp, pts[i]);
	VMOVE(pts[i], pts[j]);
	VMOVE(pts[j], tmp);
    }

    for (size_t i = 0; i < nrays; i++) {
	VMOVE(rays[i].r_pt, pts[i * 2]);
	VSUB2(rays[i].r_dir, pts[i * 2 + 1], pts[i * 2]);
	VUNITIZE(rays[i].r_dir);
    }

    bu_free(pts, "crofton pts");
}


/* ------------------------------------------------------------------ */
/* One iteration: fire nrays, accumulate into shared                   */
/* ------------------------------------------------------------------ */

static void
do_one_iteration(struct application *ap_template,
		 struct resource    *resources,
		 size_t              nrays,
		 double              radius,
		 const point_t       center,
		 struct crofton_shared *shared)
{
    struct crofton_ray *rays = (struct crofton_ray *)bu_calloc(
	nrays, sizeof(struct crofton_ray), "crofton rays");

    generate_rays(rays, nrays, radius, center);

    size_t ncpus = bu_avail_cpus();
    if (ncpus < 1) ncpus = 1;

    struct crofton_worker_data *wdata = (struct crofton_worker_data *)bu_calloc(
	ncpus, sizeof(struct crofton_worker_data), "crofton wdata");

    size_t per_cpu = nrays / ncpus;

    for (size_t i = 0; i < ncpus; i++) {
	struct application *a = (struct application *)bu_calloc(
	    1, sizeof(struct application), "crofton app");
	*a = *ap_template;                  /* struct copy */
	a->a_resource = &resources[i];

	/* Attach the worker data pointer for the hit/miss callbacks */
	struct crofton_worker_data *wd = &wdata[i];
	wd->ap     = a;
	wd->rays   = rays;
	wd->start  = i * per_cpu;
	wd->end    = (i == ncpus - 1) ? nrays : (i + 1) * per_cpu;
	wd->shared = shared;
	a->a_uptr  = wd;
    }

    bu_log("DEBUG do_one_iteration: nrays=%zu ncpus=%zu\n", nrays, ncpus);
    bu_parallel(crofton_worker, (int)ncpus, (void *)wdata);
    bu_log("DEBUG do_one_iteration: parallel done\n");

    for (size_t i = 0; i < ncpus; i++) {
	bu_log("DEBUG cleanup: freeing wdata[%zu].ap=%p\n", i, (void *)wdata[i].ap);
	bu_free(wdata[i].ap, "crofton app");
    }
    bu_log("DEBUG cleanup: freeing wdata\n");
    bu_free(wdata, "crofton wdata");
    bu_log("DEBUG cleanup: freeing rays\n");
    bu_free(rays,  "crofton rays");
    bu_log("DEBUG cleanup: done\n");
}


/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int
analyze_crofton_sample(struct db_i   *dbip,
		       const char    *obj,
		       double         threshold_pct,
		       size_t         min_samples,
		       double        *out_surf_area,
		       double        *out_volume,
		       struct bu_vls *msgs)
{
    if (!dbip || !obj || !out_surf_area || !out_volume)
	return -1;

    if (min_samples < 1)
	min_samples = 1000;

    /* Seed the random number generator with the current time so
     * successive calls give different samples.                    */
    srand((unsigned int)time(NULL));

    /* ---- Build a raytrace instance for the object ---- */
    struct rt_i *rtip = rt_new_rti(dbip);
    if (!rtip) {
	if (msgs)
	    bu_vls_printf(msgs, "analyze_crofton_sample: rt_new_rti failed\n");
	return -1;
    }

    if (rt_gettree(rtip, obj) < 0) {
	if (msgs)
	    bu_vls_printf(msgs, "analyze_crofton_sample: rt_gettree failed for '%s'\n", obj);
	rt_free_rti(rtip);
	return -1;
    }

    bu_log("DEBUG: rt_gettree OK, calling rt_prep_parallel\n");
    rt_prep_parallel(rtip, 1);
    bu_log("DEBUG: rt_prep_parallel done, R=%g\n", rtip->rti_radius);

    double R = rtip->rti_radius;
    if (R <= 0.0) {
	/* Degenerate / zero-size object */
	*out_surf_area = 0.0;
	*out_volume    = 0.0;
	rt_free_rti(rtip);
	return 0;
    }

    point_t center;
    VADD2SCALE(center, rtip->mdl_max, rtip->mdl_min, 0.5);

    /* ---- Initialize per-CPU resources ---- */
    size_t ncpus = bu_avail_cpus();
    if (ncpus < 1) ncpus = 1;

    struct resource *resources = (struct resource *)bu_calloc(
	MAX_PSW, sizeof(struct resource), "crofton resources");
    for (int i = 0; i < MAX_PSW; i++)
	rt_init_resource(&resources[i], i, rtip);

    /* ---- Set up application template ---- */
    struct application ap;
    RT_APPLICATION_INIT(&ap);
    ap.a_rt_i        = rtip;
    ap.a_hit         = crofton_hit;
    ap.a_miss        = crofton_miss;
    ap.a_overlap     = NULL;
    ap.a_multioverlap = NULL;
    ap.a_logoverlap  = rt_silent_logoverlap;
    ap.a_resource    = resources;
    ap.a_onehit      = 0;    /* collect all partitions */

    /* ---- Shared accumulator ---- */
    struct crofton_shared shared;
    memset(&shared, 0, sizeof(shared));
    shared.sem_stats = bu_semaphore_register("CROFTON_STATS");

    bu_log("DEBUG: setup complete, starting iterations, curr_rays=%zu R=%g\n",
	   min_samples, R);

    /* ---- Iterative convergence loop ---- */
    double prev2_est_sa = -2.0, prev1_est_sa = -1.0, curr_est_sa = 0.0;
    double prev2_est_v  = -2.0, prev1_est_v  = -1.0, curr_est_v  = 0.0;
    size_t iteration = 0;
    size_t curr_rays = min_samples;

    const double FOUR_PI = 4.0 * M_PI;
    const double PI      = M_PI;

    do {
	if (threshold_pct > 0.0 && iteration > 0) {
	    /* Grow sample count each iteration to avoid aliasing patterns */
	    double factor = pow(1.5, (double)iteration);
	    curr_rays = (size_t)(min_samples * factor);
	    if (curr_rays < min_samples)
		curr_rays = min_samples;
	}

	do_one_iteration(&ap, resources, curr_rays, R, center, &shared);
	iteration++;

	if (shared.total_rays == 0)
	    break;

	/* Cauchy-Crofton surface area:
	 *   SA = 4*pi*R^2 * N_crossings / (2 * N_rays)
	 */
	curr_est_sa = FOUR_PI * R * R
	    * (double)shared.total_crossings
	    / (2.0 * (double)shared.total_rays);

	/* Volume via kinematic measure:
	 *   V = pi * R * total_chord / N_rays
	 */
	curr_est_v = PI * R * shared.total_chord / (double)shared.total_rays;

	if (msgs) {
	    bu_vls_printf(msgs,
		"  iter %zu: rays=%zu  SA=%.4g  V=%.4g\n",
		iteration,
		(size_t)shared.total_rays,
		curr_est_sa, curr_est_v);
	}

	/* Check convergence after at least 3 iterations */
	if (threshold_pct > 0.0 && iteration >= 3) {
	    double pct_sa_cur  = 0.0, pct_sa_prev = 0.0;
	    double pct_v_cur   = 0.0, pct_v_prev  = 0.0;

	    if (prev1_est_sa > 0.0)
		pct_sa_cur  = fabs(curr_est_sa  - prev1_est_sa)  / prev1_est_sa * 100.0;
	    if (prev2_est_sa > 0.0)
		pct_sa_prev = fabs(prev1_est_sa - prev2_est_sa) / prev2_est_sa * 100.0;
	    if (prev1_est_v > 0.0)
		pct_v_cur   = fabs(curr_est_v   - prev1_est_v)   / prev1_est_v  * 100.0;
	    if (prev2_est_v > 0.0)
		pct_v_prev  = fabs(prev1_est_v  - prev2_est_v)  / prev2_est_v  * 100.0;

	    if (pct_sa_cur  <= threshold_pct && pct_sa_prev  <= threshold_pct &&
		pct_v_cur   <= threshold_pct && pct_v_prev   <= threshold_pct) {
		break;
	    }
	}

	prev2_est_sa = prev1_est_sa;
	prev1_est_sa = curr_est_sa;
	prev2_est_v  = prev1_est_v;
	prev1_est_v  = curr_est_v;

    } while (threshold_pct > 0.0);

    *out_surf_area = curr_est_sa;
    *out_volume    = curr_est_v;

    bu_free(resources, "crofton resources");
    rt_free_rti(rtip);
    return 0;
}


/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
