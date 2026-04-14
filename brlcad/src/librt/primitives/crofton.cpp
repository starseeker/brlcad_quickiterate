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
/** @file primitives/crofton.cpp
 *
 * Cauchy-Crofton sampling estimator for surface area and volume.
 *
 * This file is the single authoritative implementation of the Crofton
 * ray-sampling algorithm.  It exposes three public symbols:
 *
 *   rt_crofton_shoot()     -- core estimator given a prepared rt_i;
 *                             shared with libanalyze for code reuse
 *   rt_crofton_surf_area() -- ft_surf_area-compatible fallback for the
 *                             primitive functab (used when a primitive
 *                             lacks an analytic surface-area formula)
 *   rt_crofton_volume()    -- ft_volume-compatible fallback for the
 *                             primitive functab (used when a primitive
 *                             lacks an analytic volume formula)
 *
 * The Cauchy-Crofton integral-geometry formula relates the number of
 * times random lines pierce a surface to its area, and the total
 * length of solid chord segments to its volume:
 *
 *   SA = 4*pi*R^2 * N_crossings / (2 * N_rays)
 *   V  = pi * R^2 * total_chord / N_rays
 *
 * where R is the bounding-sphere radius, N_crossings counts every
 * entry AND exit hit event (2 per solid segment for a non-self-
 * intersecting closed surface), and total_chord is the sum of solid
 * segment lengths.
 *
 * References:
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
#include "raytrace.h"


/* ------------------------------------------------------------------ */
/* Default parameters for the functab fallbacks                        */
/* ------------------------------------------------------------------ */

/** Minimum rays per iteration when used as a generic functab fallback.
 *  Kept small so the fallback is fast for interactive use; callers
 *  that need higher accuracy should call rt_crofton_shoot() directly
 *  with a larger min_samples and/or a tighter threshold.             */
#define RT_CROFTON_DEFAULT_SAMPLES   2000u

/** Convergence threshold (%) for the functab fallback.               */
#define RT_CROFTON_DEFAULT_THRESHOLD 1.0


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
    return (double)rand() / ((double)RAND_MAX + 1.0);
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
 * Generate @p nrays chord rays from 2*nrays random points on the
 * bounding sphere.  Each ray connects one randomly chosen point to
 * another.
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

	struct crofton_worker_data *wd = &wdata[i];
	wd->ap     = a;
	wd->rays   = rays;
	wd->start  = i * per_cpu;
	wd->end    = (i == ncpus - 1) ? nrays : (i + 1) * per_cpu;
	wd->shared = shared;
	a->a_uptr  = wd;
    }

    bu_parallel(crofton_worker, (int)ncpus, (void *)wdata);

    for (size_t i = 0; i < ncpus; i++)
	bu_free(wdata[i].ap, "crofton app");
    bu_free(wdata, "crofton wdata");
    bu_free(rays,  "crofton rays");
}


/* ------------------------------------------------------------------ */
/* Public API: rt_crofton_shoot                                         */
/* ------------------------------------------------------------------ */

/**
 * Run the Cauchy-Crofton sampling estimator on an already-prepared
 * raytrace instance @p rtip.
 *
 * The caller is responsible for creating, preparing (rt_prep_parallel),
 * and freeing (rt_free_rti) @p rtip.  This function does NOT call
 * rt_free_rti.
 *
 * @param rtip         Prepared raytrace instance (rt_prep_parallel must
 *                     have been called before this function).
 * @param min_samples  Minimum rays per iteration (< 1 defaults to 1000).
 * @param threshold_pct Convergence threshold as a percentage.  Pass 0
 *                     for a single-iteration run (no convergence loop).
 * @param out_surf_area Receives the estimated surface area (mm^2).
 * @param out_volume    Receives the estimated volume (mm^3).
 * @return  0 on success, -1 on bad arguments.
 */
int
rt_crofton_shoot(struct rt_i *rtip,
		 size_t       min_samples,
		 double       threshold_pct,
		 double      *out_surf_area,
		 double      *out_volume)
{
    if (!rtip || !out_surf_area || !out_volume)
	return -1;

    if (min_samples < 1)
	min_samples = 1000;

    double R = rtip->rti_radius;
    if (R <= 0.0) {
	/* Degenerate / zero-size object */
	*out_surf_area = 0.0;
	*out_volume    = 0.0;
	return 0;
    }

    point_t center;
    VADD2SCALE(center, rtip->mdl_max, rtip->mdl_min, 0.5);

    /* ---- Initialize per-CPU resources ---- */
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
	 *   V = pi * R^2 * total_chord / N_rays
	 */
	curr_est_v = PI * R * R * shared.total_chord / (double)shared.total_rays;

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

    /* Clean each resource and NULL out its slot in rtip->rti_resources.
     * This is necessary because crofton_from_ip calls rt_free_rti(rtip)
     * after we return.  rt_free_rti → rt_clean iterates rti_resources and
     * calls rt_clean_resource (which calls rt_init_resource) on every
     * non-NULL entry.  If we free the resources array first, those entries
     * become dangling pointers and rt_init_resource reads garbage re_cpu
     * values that may exceed MAX_PSW, triggering a BU_ASSERT.
     *
     * By setting the slot to NULL we let rt_free_rti's cleanup skip it,
     * and then we can safely bu_free the resources array.                */
    for (int i = 0; i < MAX_PSW; i++) {
	if (resources[i].re_magic == RESOURCE_MAGIC) {
	    rt_clean_resource_basic(rtip, &resources[i]);
	    /* Unregister so rt_free_rti does not re-visit this slot */
	    BU_PTBL_SET(&rtip->rti_resources, i, NULL);
	}
    }
    bu_free(resources, "crofton resources");

    return 0;
}


/* ------------------------------------------------------------------ */
/* Private: build a temp in-memory DB and run Crofton on it           */
/* ------------------------------------------------------------------ */

/**
 * Create a temporary in-memory database containing only the primitive
 * described by @p ip, run the Crofton estimator with default parameters,
 * and return the results.
 *
 * The caller's @p ip is NOT consumed or freed.  We serialize it to a
 * bu_external and write that to the in-memory db, which avoids calling
 * any ifree on the caller's data.
 */
static int
crofton_from_ip(const struct rt_db_internal *ip, double *out_sa, double *out_vol)
{
    if (!ip || (!out_sa && !out_vol))
	return -1;

    /* ---- Open an in-memory database ---- */
    struct db_i *dbip = db_open_inmem();
    if (dbip == DBI_NULL) {
	bu_log("rt_crofton: db_open_inmem() failed\n");
	return -1;
    }

    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);
    if (!wdbp) {
	bu_log("rt_crofton: wdb_dbopen() failed\n");
	db_close(dbip);
	return -1;
    }

    /* ---- Serialize ip to bu_external without freeing the caller's data.
     *
     * Build a shallow wrapper around ip so that rt_db_cvt_to_external5
     * can serialize the primitive data without requiring a full deep copy.
     * We must NOT call rt_db_free_internal on this wrapper because idb_ptr
     * is owned by the caller.                                             */
    const char *scratch = "_crofton_tmp";

    struct rt_db_internal tmp_intern;
    RT_DB_INTERNAL_INIT(&tmp_intern);
    tmp_intern.idb_major_type = ip->idb_major_type;
    tmp_intern.idb_type       = ip->idb_minor_type;
    tmp_intern.idb_ptr        = ip->idb_ptr;   /* shared, not owned */
    tmp_intern.idb_meth       = ip->idb_meth;

    struct bu_external ext;
    BU_EXTERNAL_INIT(&ext);

    if (rt_db_cvt_to_external5(&ext, scratch, &tmp_intern, 1.0,
				dbip, &rt_uniresource,
				ip->idb_major_type) < 0) {
	bu_log("rt_crofton: rt_db_cvt_to_external5() failed\n");
	bu_free_external(&ext);
	db_close(dbip);
	return -1;
    }

    int eflags = db_flags_internal(&tmp_intern);
    if (wdb_export_external(wdbp, &ext, scratch,
			    eflags,
			    (unsigned char)ip->idb_minor_type) < 0) {
	bu_log("rt_crofton: wdb_export_external() failed\n");
	/* ext.ext_buf stolen by db_inmem on success; free any remainder */
	bu_free_external(&ext);
	db_close(dbip);
	return -1;
    }
    /* In the INMEM path ext_buf is stolen; this is safe to call regardless */
    bu_free_external(&ext);

    db_update_nref(dbip, &rt_uniresource);

    /* ---- Build raytrace instance ---- */
    struct rt_i *rtip = rt_new_rti(dbip);
    if (!rtip) {
	bu_log("rt_crofton: rt_new_rti() failed\n");
	db_close(dbip);
	return -1;
    }

    if (rt_gettree(rtip, scratch) < 0) {
	bu_log("rt_crofton: rt_gettree() failed for '%s'\n", scratch);
	rt_free_rti(rtip);
	db_close(dbip);
	return -1;
    }

    rt_prep_parallel(rtip, 1);

    /* ---- Run Crofton estimator ---- */
    double sa  = 0.0;
    double vol = 0.0;
    (void)rt_crofton_shoot(rtip,
			   RT_CROFTON_DEFAULT_SAMPLES,
			   RT_CROFTON_DEFAULT_THRESHOLD,
			   &sa, &vol);

    if (out_sa)  *out_sa  = sa;
    if (out_vol) *out_vol = vol;

    /* ---- Clean up ---- */
    rt_free_rti(rtip);
    /* wdb_dbopen for INMEM returns an embedded pointer inside dbip;
     * do NOT call wdb_close() here, as that would double-free dbip. */
    db_close(dbip);

    return 0;
}


/* ------------------------------------------------------------------ */
/* Public API: functab-compatible fallbacks                            */
/* ------------------------------------------------------------------ */

/**
 * Generic surface-area fallback for the primitive functab.
 *
 * Used as ft_surf_area for primitives that do not implement an analytic
 * surface-area formula.  Invokes the Cauchy-Crofton ray-sampling
 * estimator on a temporary in-memory raytrace of the primitive.
 */
void
rt_crofton_surf_area(fastf_t *area, const struct rt_db_internal *ip)
{
    if (!area || !ip)
	return;

    double sa = 0.0;
    if (crofton_from_ip(ip, &sa, NULL) < 0)
	sa = 0.0;

    *area = (fastf_t)sa;
}


/**
 * Generic volume fallback for the primitive functab.
 *
 * Used as ft_volume for primitives that do not implement an analytic
 * volume formula.  Invokes the Cauchy-Crofton ray-sampling estimator
 * on a temporary in-memory raytrace of the primitive.
 */
void
rt_crofton_volume(fastf_t *vol, const struct rt_db_internal *ip)
{
    if (!vol || !ip)
	return;

    double v = 0.0;
    if (crofton_from_ip(ip, NULL, &v) < 0)
	v = 0.0;

    *vol = (fastf_t)v;
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
