/*                        D I F F . C P P
 * BRL-CAD
 *
 * Copyright (c) 2024-2026 United States Government as represented by
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
/** @file libicv/diff.cpp
 *
 * Higher-level image diffing utilities:
 *
 *  icv_diff_render_info  – compare embedded render metadata between two images
 *  icv_diff_nirt_shots   – generate nirt shotline commands for differing pixels;
 *                          uses render_info from whichever image has it
 */

#include "common.h"

#include <cstring>
#include <cmath>
#include <cstdio>

#include "icv.h"
#include "bio.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bn/mat.h"
#include "vmath.h"


/*
 * icv_diff_render_info
 *
 * Compare the icv_render_info embedded in img1 and img2.
 */
extern "C" int
icv_diff_render_info(const icv_image_t *img1, const icv_image_t *img2, struct bu_vls *out_msgs)
{
    const struct icv_render_info *r1 = img1 ? img1->render_info : NULL;
    const struct icv_render_info *r2 = img2 ? img2->render_info : NULL;

    if (!r1 && !r2)
	return -1;   /* neither image has metadata */

    int differs = 0;

    /* db filename */
    {
	const char *f1 = r1 ? r1->db_filename : NULL;
	const char *f2 = r2 ? r2->db_filename : NULL;
	if (!f1 && !f2) {
	    /* both absent – fine */
	} else if (!f1) {
	    if (out_msgs) bu_vls_printf(out_msgs, "db_filename: (none) vs '%s'\n", f2);
	    differs = 1;
	} else if (!f2) {
	    if (out_msgs) bu_vls_printf(out_msgs, "db_filename: '%s' vs (none)\n", f1);
	    differs = 1;
	} else if (!BU_STR_EQUAL(f1, f2)) {
	    if (out_msgs) bu_vls_printf(out_msgs, "db_filename: '%s' vs '%s'\n", f1, f2);
	    differs = 1;
	} else {
	    if (out_msgs) bu_vls_printf(out_msgs, "db_filename: '%s' (same)\n", f1);
	}
    }

    /* objects */
    {
	const char *o1 = r1 ? r1->objects : NULL;
	const char *o2 = r2 ? r2->objects : NULL;
	if (!o1 && !o2) {
	    /* both absent – fine */
	} else if (!o1) {
	    if (out_msgs) bu_vls_printf(out_msgs, "objects: (none) vs '%s'\n", o2);
	    differs = 1;
	} else if (!o2) {
	    if (out_msgs) bu_vls_printf(out_msgs, "objects: '%s' vs (none)\n", o1);
	    differs = 1;
	} else if (!BU_STR_EQUAL(o1, o2)) {
	    if (out_msgs) bu_vls_printf(out_msgs, "objects: '%s' vs '%s'\n", o1, o2);
	    differs = 1;
	} else {
	    if (out_msgs) bu_vls_printf(out_msgs, "objects: '%s' (same)\n", o1);
	}
    }

    /* Camera parameters – only check when both images carry camera data */
    if (r1 && r2) {
	const double tol = 1e-15;   /* strict: these should round-trip exactly */
	int cam_diff = 0;

	/* viewrotscale */
	for (int i = 0; i < 16; i++) {
	    if (fabs(r1->viewrotscale[i] - r2->viewrotscale[i]) > tol) {
		cam_diff = 1;
		break;
	    }
	}

	/* eye_model */
	for (int i = 0; i < 3; i++) {
	    if (fabs(r1->eye_model[i] - r2->eye_model[i]) > tol)
		cam_diff = 1;
	}

	if (fabs(r1->viewsize   - r2->viewsize)   > tol) cam_diff = 1;
	if (fabs(r1->aspect     - r2->aspect)     > tol) cam_diff = 1;
	if (fabs(r1->perspective - r2->perspective) > tol) cam_diff = 1;

	if (cam_diff) {
	    differs = 1;
	    if (out_msgs) {
		bu_vls_printf(out_msgs, "camera: DIFFERS\n");
		bu_vls_printf(out_msgs,
			      "  img1 eye_model: (%.17g, %.17g, %.17g)\n",
			      r1->eye_model[0], r1->eye_model[1], r1->eye_model[2]);
		bu_vls_printf(out_msgs,
			      "  img2 eye_model: (%.17g, %.17g, %.17g)\n",
			      r2->eye_model[0], r2->eye_model[1], r2->eye_model[2]);
		bu_vls_printf(out_msgs,
			      "  img1 viewsize=%.17g  img2 viewsize=%.17g\n",
			      r1->viewsize, r2->viewsize);
		bu_vls_printf(out_msgs,
			      "  img1 perspective=%.17g  img2 perspective=%.17g\n",
			      r1->perspective, r2->perspective);
	    }
	} else {
	    if (out_msgs) bu_vls_printf(out_msgs, "camera: identical\n");
	}
    } else if (r1 && !r2) {
	if (out_msgs) bu_vls_printf(out_msgs, "camera: img1 has camera data, img2 does not\n");
	differs = 1;
    } else if (!r1 && r2) {
	if (out_msgs) bu_vls_printf(out_msgs, "camera: img2 has camera data, img1 does not\n");
	differs = 1;
    }

    return differs;
}


/*
 * icv_diff_nirt_shots
 *
 * For each pixel that differs between img1 and img2, reconstruct the
 * world-space ray from whichever image has render metadata and write
 * nirt commands.  If only one image has render_info, that metadata is
 * used; we assume the views match since we cannot prove otherwise.
 *
 * Ray reconstruction mirrors BRL-CAD rt/grid.c grid_setup() for the
 * orthographic case (rt_perspective == 0):
 *
 *   1. Rebuild model2view from Viewrotscale + toEye(eye_model)
 *   2. Invert to get view2model
 *   3. dx_model = MAT3X3VEC(view2model, (1,0,0)) * cell_width
 *   4. dy_model = MAT3X3VEC(view2model, (0,1,0)) * cell_height
 *   5. viewbase_model = MAT4X3PNT(view2model, (-1, -1/aspect, 0))
 *   6. For pixel (col, row): ray_pt = viewbase + col*dx + row*dy
 *   7. ray_dir = MAT4X3VEC(view2model, (0, 0, -1)), normalised
 *
 * For perspective, a diverging ray is computed similarly.
 */
extern "C" int
icv_diff_nirt_shots(const icv_image_t *img1, const icv_image_t *img2, FILE *nirt_out)
{
    if (!img1 || !img2 || !nirt_out)
	return -1;

    if (img1->width != img2->width || img1->height != img2->height) {
	bu_log("icv_diff_nirt_shots: images must be the same size\n");
	return -1;
    }

    /* Use whichever image has render_info; prefer img1 when both have it */
    const struct icv_render_info *ri = img1->render_info;
    if (!ri)
	ri = img2->render_info;
    if (!ri) {
	bu_log("icv_diff_nirt_shots: neither image has render_info – cannot compute rays\n");
	return -1;
    }
    if (ri->viewsize <= 0.0) {
	bu_log("icv_diff_nirt_shots: render_info has invalid viewsize (%g)\n", ri->viewsize);
	return -1;
    }

    const size_t width  = img1->width;
    const size_t height = img1->height;
    const double aspect = (ri->aspect > 0.0) ? ri->aspect : ((double)width / (double)height);
    const double viewsize = ri->viewsize;

    /* Reconstruct view2model from Viewrotscale + eye translation */
    mat_t Viewrotscale;
    MAT_COPY(Viewrotscale, ri->viewrotscale);
    Viewrotscale[15] = 0.5 * viewsize;   /* Viewscale element */

    mat_t toEye;
    MAT_IDN(toEye);
    MAT_DELTAS_VEC_NEG(toEye, ri->eye_model);

    mat_t model2view, view2model;
    bn_mat_mul(model2view, Viewrotscale, toEye);
    bn_mat_inv(view2model, model2view);

    const double cell_width  = viewsize / (double)width;
    const double cell_height = (aspect > 0.0)
	? viewsize / ((double)height * aspect)
	: viewsize / (double)height;

    /* dx_model, dy_model – rotate only, then scale */
    vect_t dx_model, dy_model;
    {
	vect_t temp;
	VSET(temp, 1, 0, 0);
	MAT3X3VEC(dx_model, view2model, temp);
	VSCALE(dx_model, dx_model, cell_width);

	VSET(temp, 0, 1, 0);
	MAT3X3VEC(dy_model, view2model, temp);
	VSCALE(dy_model, dy_model, cell_height);
    }

    /* Precompute the perspective zoomout factor (0 for orthographic) */
    const double zoomout = (ri->perspective > 0.0)
	? 1.0 / tan(DEG2RAD * ri->perspective / 2.0)
	: 0.0;

    /* viewbase_model – lower-left corner of the view plane */
    point_t viewbase_model;
    {
	vect_t temp;
	if (ri->perspective > 0.0) {
	    VSET(temp, -1.0, -1.0 / aspect, -zoomout);
	} else {
	    VSET(temp, -1.0, -1.0 / aspect, 0.0);
	}
	MAT4X3PNT(viewbase_model, view2model, temp);
    }

    /* Ray direction (orthographic – same for all pixels) */
    vect_t ray_dir;
    {
	vect_t temp;
	VSET(temp, 0.0, 0.0, -1.0);
	MAT4X3VEC(ray_dir, view2model, temp);
	VUNITIZE(ray_dir);
    }

    /* Get uint8 pixel data for comparison */
    unsigned char *d1 = icv_data2uchar(img1);
    unsigned char *d2 = icv_data2uchar(img2);

    /* Header comment */
    fprintf(nirt_out, "# nirt shotlines for differing pixels\n");
    if (ri->db_filename)
	fprintf(nirt_out, "# database: %s\n", ri->db_filename);
    if (ri->objects)
	fprintf(nirt_out, "# objects:  %s\n", ri->objects);
    fprintf(nirt_out, "#\n");
    fprintf(nirt_out, "# Usage: nirt -f <this_file> %s %s\n",
	    ri->db_filename ? ri->db_filename : "model.g",
	    ri->objects     ? ri->objects     : "[objects...]");
    fprintf(nirt_out, "#\n");

    /* Set the ray direction once (same for all pixels in orthographic) */
    fprintf(nirt_out, "dir %.17g %.17g %.17g\n",
	    ray_dir[X], ray_dir[Y], ray_dir[Z]);
    fprintf(nirt_out, "\n");

    int ndiff = 0;

    for (size_t row = 0; row < height; row++) {
	for (size_t col = 0; col < width; col++) {
	    size_t idx = row * width + col;
	    int r1 = d1[idx*3+0], g1 = d1[idx*3+1], b1 = d1[idx*3+2];
	    int r2 = d2[idx*3+0], g2 = d2[idx*3+1], b2 = d2[idx*3+2];

	    if (r1 == r2 && g1 == g2 && b1 == b2)
		continue;   /* pixel matches */

	    /* Compute ray origin for this pixel */
	    point_t ray_pt;

	    if (ri->perspective > 0.0) {
		/* Perspective: diverging rays from a single eye point */
		vect_t temp;
		VSET(temp, -1.0 + 2.0 * (col + 0.5) / (double)width,
		           (-1.0 / aspect) + 2.0 * (row + 0.5) / ((double)height * aspect),
		           -zoomout);
		vect_t world_dir;
		MAT4X3VEC(world_dir, view2model, temp);
		VUNITIZE(world_dir);
		/* Eye point is the ray origin; direction is per-pixel */
		VMOVE(ray_pt, ri->eye_model);
		fprintf(nirt_out, "# pixel col=%zu row=%zu  rgb1=(%d,%d,%d) rgb2=(%d,%d,%d)\n",
			col, row, r1, g1, b1, r2, g2, b2);
		fprintf(nirt_out, "xyz %.17g %.17g %.17g\n",
			ray_pt[X], ray_pt[Y], ray_pt[Z]);
		fprintf(nirt_out, "dir %.17g %.17g %.17g\n",
			world_dir[X], world_dir[Y], world_dir[Z]);
		fprintf(nirt_out, "s\n\n");
	    } else {
		/* Orthographic */
		VJOIN2(ray_pt, viewbase_model,
		       (double)col, dx_model,
		       (double)row, dy_model);
		fprintf(nirt_out, "# pixel col=%zu row=%zu  rgb1=(%d,%d,%d) rgb2=(%d,%d,%d)\n",
			col, row, r1, g1, b1, r2, g2, b2);
		fprintf(nirt_out, "xyz %.17g %.17g %.17g\n",
			ray_pt[X], ray_pt[Y], ray_pt[Z]);
		fprintf(nirt_out, "s\n\n");
	    }

	    ndiff++;
	}
    }

    bu_free(d1, "icv_diff_nirt d1");
    bu_free(d2, "icv_diff_nirt d2");

    return ndiff;
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
