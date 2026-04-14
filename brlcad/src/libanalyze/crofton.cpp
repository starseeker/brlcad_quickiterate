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
 * Cauchy-Crofton sampling estimator for surface area and volume --
 * libanalyze public API.
 *
 * The core ray-shooting logic lives in librt (primitives/crofton.cpp)
 * and is exposed as rt_crofton_shoot().  This file provides the
 * higher-level analyze_crofton_sample() entry point that accepts a
 * db_i pointer and an object name, prepares a raytrace instance, and
 * delegates to rt_crofton_shoot() for the iterative convergence loop.
 *
 * Keeping the core algorithm in librt allows librt primitives that
 * lack analytic surface-area or volume formulas to use the same
 * implementation as a generic functab fallback (rt_crofton_surf_area /
 * rt_crofton_volume), while libanalyze callers that need both values
 * in a single raytrace pass (or need access to diagnostic messages)
 * continue to use analyze_crofton_sample().
 *
 * Cauchy-Crofton formula:
 *   SA = 4*pi*R^2 * N_crossings / (2 * N_rays)
 *   V  = pi * R^2 * total_chord / N_rays
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
#include <time.h>

#include "bu/log.h"
#include "bu/vls.h"
#include "raytrace.h"
#include "analyze.h"


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

    rt_prep_parallel(rtip, 1);

    if (msgs)
	bu_vls_printf(msgs, "analyze_crofton_sample: sampling '%s' "
		      "(min_samples=%zu threshold=%.4g%%)\n",
		      obj, min_samples, threshold_pct);

    /* ---- Delegate to the shared librt core ---- */
    int ret = rt_crofton_shoot(rtip, min_samples, threshold_pct,
			       out_surf_area, out_volume);

    if (ret < 0 && msgs)
	bu_vls_printf(msgs, "analyze_crofton_sample: rt_crofton_shoot failed\n");

    rt_free_rti(rtip);
    return ret;
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
