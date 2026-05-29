/*                    M E A S U R E . C
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
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
/** @file libbsg/measure.c */

#include "common.h"

#include <math.h>

#include "bn.h"
#include "bsg/measure.h"

int
bsg_measure_candidates(struct bsg_view *v, point_t a, point_t b,
		       struct bsg_measure_result *out)
{
    if (!out)
	return 0;

    out->mr_distance = 0.0;
    out->mr_projection = 0.0;
    out->mr_normal_alignment = 0.0;
    out->mr_valid = 0;

    vect_t ab = VINIT_ZERO;
    VSUB2(ab, b, a);
    fastf_t d = MAGNITUDE(ab);
    if (ZERO(d))
	return 0;

    out->mr_distance = d;

    if (v) {
	point_t av = VINIT_ZERO;
	point_t bv = VINIT_ZERO;
	vect_t abv = VINIT_ZERO;
	MAT4X3PNT(av, v->gv_model2view, a);
	MAT4X3PNT(bv, v->gv_model2view, b);
	VSUB2(abv, bv, av);
	out->mr_projection = MAGNITUDE(abv);

	vect_t n = VINIT_ZERO;
	VSET(n, 0.0, 0.0, 1.0);
	fastf_t norm_ab[3] = {ab[0], ab[1], ab[2]};
	VUNITIZE(norm_ab);
	out->mr_normal_alignment = fabs(VDOT(norm_ab, n));
    } else {
	out->mr_projection = d;
	out->mr_normal_alignment = 0.0;
    }

    out->mr_valid = 1;
    return 1;
}
