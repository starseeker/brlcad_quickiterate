/*                   R E N D E R _ R U N . C P P
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
/** @file librender/render_run.cpp
 *
 * Core synchronous rendering engine for librender.
 *
 * render_run() performs a complete scanline raytrace of the geometry
 * loaded in a render_ctx_t, using the parameters in a render_opts_t.
 * It delivers pixel data one scanline at a time via a user-supplied
 * callback, enabling incremental (progressive) display.
 *
 * ### Ray-grid setup
 *
 * The view grid replicates rt/grid.c's scanline layout exactly:
 *
 *   cell_width  = viewsize / width
 *   cell_height = viewsize / (height * aspect)
 *
 *   dx_model    = MAT3X3VEC(view2model, (1, 0, 0)) * cell_width
 *   dy_model    = MAT3X3VEC(view2model, (0, 1, 0)) * cell_height
 *
 * For orthographic (perspective_deg == 0):
 *   viewbase_model = MAT4X3PNT(view2model, (-1, -1/aspect, 0))
 *   pixel (x,y) ray:  origin = viewbase + x*dx + y*dy
 *                     dir    = MAT4X3VEC(view2model, (0,0,-1)) [uniform]
 *
 * For perspective (perspective_deg > 0):
 *   zoomout        = 1 / tan(perspective_deg/2 * DEG2RAD)
 *   viewbase_model = MAT4X3PNT(view2model, (-1, -1/aspect, -zoomout))
 *   pixel (x,y) ray:  origin = eye_model
 *                     dir    = (viewbase + x*dx + y*dy) - eye_model
 *
 * ### Lighting models
 *
 *   RENDER_LIGHT_NORMALS  Surface normals → RGB (no liboptical needed).
 *   RENDER_LIGHT_DIFFUSE  N·L diffuse from eye, region colour.
 *   RENDER_LIGHT_FULL     viewshade() from liboptical (full Phong).
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/parallel.h"
#include "vmath.h"
#include "raytrace.h"
#include "optical.h"
#include "optical/light.h"
#include "optical/shadework.h"

#include "render.h"
#include "./render_private.h"


/* ================================================================== */
/* Per-pixel application state passed through ap->a_uptr              */
/* ================================================================== */

struct pixel_state {
    struct render_state *rs;
    int                  cpu;   /* which CPU / thread slot */
};


/* ================================================================== */
/* Hit and miss callbacks                                               */
/* ================================================================== */

static int
render_hit(struct application *ap,
	   struct partition   *PartHeadp,
	   struct seg         *UNUSED(segs))
{
    struct pixel_state   *ps  = (struct pixel_state *)ap->a_uptr;
    const struct render_opts *opts = ps->rs->opts;
    struct partition     *pp;

    /* Should never happen when a_onehit==-1, but be safe. */
    if (PartHeadp->pt_forw == PartHeadp) {
	VMOVE(ap->a_color, ps->rs->opts->background);
	return 0;
    }

    pp = PartHeadp->pt_forw;

    switch (opts->lightmodel) {

    case RENDER_LIGHT_NORMALS: {
	struct hit    *hitp = pp->pt_inhit;
	struct soltab *stp  = pp->pt_inseg->seg_stp;
	vect_t normal;
	RT_HIT_NORMAL(normal, hitp, stp, &ap->a_ray, pp->pt_inflip);
	/* Map normal (-1..1) to colour (0..1). */
	ap->a_color[0] = 0.5 + 0.5 * normal[0];
	ap->a_color[1] = 0.5 + 0.5 * normal[1];
	ap->a_color[2] = 0.5 + 0.5 * normal[2];
	break;
    }

    case RENDER_LIGHT_FULL: {
	/* Full Phong model via liboptical.
	 * Requires that render_ctx_create() has called mlib_setup() for
	 * each region and that light_init() was called before the frame. */
	struct shadework sw;
	memset(&sw, 0, sizeof(sw));
	(void)viewshade(ap, pp, &sw);
	VMOVE(ap->a_color, sw.sw_color);
	break;
    }

    default:
    case RENDER_LIGHT_DIFFUSE: {
	/* Simple N·L diffuse from the eye direction, using the region's
	 * explicit colour (or grey when none is set). */
	struct hit    *hitp = pp->pt_inhit;
	struct soltab *stp  = pp->pt_inseg->seg_stp;
	vect_t normal, ldir;
	double diffuse;
	double cr = 0.5, cg = 0.5, cb = 0.5;  /* default grey */
	const struct region *regp = pp->pt_regionp;
	static const double ambient = 0.2;

	RT_HIT_NORMAL(normal, hitp, stp, &ap->a_ray, pp->pt_inflip);
	VREVERSE(ldir, ap->a_ray.r_dir);
	diffuse = fabs(VDOT(normal, ldir));

	if (regp && regp->reg_mater.ma_color_valid) {
	    cr = regp->reg_mater.ma_color[0];
	    cg = regp->reg_mater.ma_color[1];
	    cb = regp->reg_mater.ma_color[2];
	}

	{
	    double lit = ambient + (1.0 - ambient) * diffuse;
	    ap->a_color[0] = cr * lit;
	    ap->a_color[1] = cg * lit;
	    ap->a_color[2] = cb * lit;
	}
	break;
    }

    } /* switch lightmodel */

    return 1;
}


static int
render_miss(struct application *ap)
{
    struct pixel_state *ps = (struct pixel_state *)ap->a_uptr;
    VMOVE(ap->a_color, ps->rs->opts->background);
    return 0;
}


/* ================================================================== */
/* Worker: render a horizontal band of scanlines                       */
/* ================================================================== */

static void
render_worker(int cpu, void *arg)
{
    struct render_state *rs       = (struct render_state *)arg;
    const struct render_opts *opts = rs->opts;
    const struct render_ctx  *ctx  = rs->ctx;
    int                 width     = opts->width;
    int                 height    = opts->height;
    struct application  ap;
    struct pixel_state  ps;
    unsigned char      *scanbuf;
    int y, x;

    RT_APPLICATION_INIT(&ap);
    ap.a_rt_i    = ctx->rtip;
    ap.a_resource = &ctx->res[cpu];
    ap.a_hit     = render_hit;
    ap.a_miss    = render_miss;
    ap.a_onehit  = -1;   /* stop at first non-air hit */
    ap.a_uptr    = (void *)&ps;

    ps.rs  = rs;
    ps.cpu = cpu;

    /* each thread gets its own scanline buffer */
    scanbuf = rs->scanbufs[cpu];

    /* Partition scanlines across threads using a simple interleaved scheme */
    for (y = cpu; y < height; y += rs->nthreads) {
	int sl_done;

	for (x = 0; x < width; x++) {
	    point_t pt;
	    unsigned char r, g, b;
	    double fr, fg, fb;

	    /* Compute model-space point for pixel (x, y) */
	    VJOIN2(pt, rs->viewbase_model,
		   (fastf_t)x, rs->dx_model,
		   (fastf_t)y, rs->dy_model);

	    if (opts->perspective_deg > 0.0) {
		/* Perspective: ray from eye through pixel on the image plane. */
		VSUB2(ap.a_ray.r_dir, pt, opts->eye_model);
		VUNITIZE(ap.a_ray.r_dir);
		VMOVE(ap.a_ray.r_pt, opts->eye_model);
	    } else {
		/* Orthographic: uniform direction, origin at image plane. */
		VMOVE(ap.a_ray.r_pt,  pt);
		VMOVE(ap.a_ray.r_dir, rs->ortho_dir);
	    }

	    ap.a_x = x;
	    ap.a_y = y;

	    rt_shootray(&ap);

	    /* Clamp colour 0.0–1.0, convert to 8-bit. */
	    fr = ap.a_color[0];  if (fr < 0.0) fr = 0.0; if (fr > 1.0) fr = 1.0;
	    fg = ap.a_color[1];  if (fg < 0.0) fg = 0.0; if (fg > 1.0) fg = 1.0;
	    fb = ap.a_color[2];  if (fb < 0.0) fb = 0.0; if (fb > 1.0) fb = 1.0;
	    r = (unsigned char)(fr * 255.0 + 0.5);
	    g = (unsigned char)(fg * 255.0 + 0.5);
	    b = (unsigned char)(fb * 255.0 + 0.5);

	    scanbuf[x * 3 + 0] = r;
	    scanbuf[x * 3 + 1] = g;
	    scanbuf[x * 3 + 2] = b;
	}

	/* Deliver the completed scanline. */
	if (rs->pxcb)
	    rs->pxcb(rs->ud, 0, y, width, scanbuf);

	/* Progress reporting (semaphore-protected counter). */
	bu_semaphore_acquire(BU_SEM_GENERAL);
	sl_done = ++rs->scanlines_done;
	bu_semaphore_release(BU_SEM_GENERAL);

	if (rs->progcb)
	    rs->progcb(rs->ud, sl_done, rs->scanlines_total);
    }
}


/* ================================================================== */
/* render_run: public entry point                                       */
/* ================================================================== */

int
render_run(struct render_ctx       *ctx,
	   const struct render_opts *opts,
	   render_pixel_cb           pxcb,
	   render_progress_cb        progcb,
	   render_done_cb            donecb,
	   void                     *ud)
{
    struct render_state rs;
    double cell_width, cell_height, aspect;
    vect_t temp;
    int nthreads, i;

    /* ── validate arguments ─────────────────────────────────────────── */
    if (!ctx || !opts || !pxcb) {
	bu_log("render_run: invalid arguments\n");
	return BRLCAD_ERROR;
    }
    if (!opts->view_set) {
	bu_log("render_run: view parameters not set (call render_opts_set_view)\n");
	return BRLCAD_ERROR;
    }

    /* ── compute grid layout (mirrors rt/grid.c::grid_setup) ─────────── */
    aspect = (opts->aspect > 0.0) ? opts->aspect : 1.0;
    cell_width  = opts->viewsize / opts->width;
    cell_height = opts->viewsize / (opts->height * aspect);

    /* dx_model: model-space vector for one pixel step to the right */
    VSET(temp, 1.0 / opts->viewsize, 0.0, 0.0);
    MAT3X3VEC(rs.dx_model, opts->view2model, temp);
    VSCALE(rs.dx_model, rs.dx_model, cell_width);

    /* dy_model: model-space vector for one pixel step upward */
    VSET(temp, 0.0, 1.0 / opts->viewsize, 0.0);
    MAT3X3VEC(rs.dy_model, opts->view2model, temp);
    VSCALE(rs.dy_model, rs.dy_model, cell_height);

    if (opts->perspective_deg > 0.0) {
	/* Perspective: viewbase at the image plane behind the eye. */
	double zoomout = 1.0 / tan(DEG2RAD * opts->perspective_deg * 0.5);
	VSET(temp, -1.0, -1.0 / aspect, -zoomout);
    } else {
	/* Orthographic: viewbase at the image plane (z=0 in view space). */
	VSET(temp, -1.0, -1.0 / aspect, 0.0);

	/* Compute uniform ray direction (-Z axis of view, in model space). */
	VSET(temp, 0.0, 0.0, -1.0);
	MAT4X3VEC(rs.ortho_dir, opts->view2model, temp);
	VUNITIZE(rs.ortho_dir);

	/* Restore temp for viewbase_model computation. */
	VSET(temp, -1.0, -1.0 / aspect, 0.0);
    }
    MAT4X3PNT(rs.viewbase_model, opts->view2model, temp);

    /* ── set up render state ─────────────────────────────────────────── */
    memset(&rs, 0, sizeof(rs));
    /* Re-populate after memset (memset zeroed things we just computed). */
    {
	double cell_w = opts->viewsize / opts->width;
	double cell_h = opts->viewsize / (opts->height * aspect);

	VSET(temp, 1.0 / opts->viewsize, 0.0, 0.0);
	MAT3X3VEC(rs.dx_model, opts->view2model, temp);
	VSCALE(rs.dx_model, rs.dx_model, cell_w);

	VSET(temp, 0.0, 1.0 / opts->viewsize, 0.0);
	MAT3X3VEC(rs.dy_model, opts->view2model, temp);
	VSCALE(rs.dy_model, rs.dy_model, cell_h);

	if (opts->perspective_deg > 0.0) {
	    double zo = 1.0 / tan(DEG2RAD * opts->perspective_deg * 0.5);
	    VSET(temp, -1.0, -1.0 / aspect, -zo);
	} else {
	    VSET(temp, 0.0, 0.0, -1.0);
	    MAT4X3VEC(rs.ortho_dir, opts->view2model, temp);
	    VUNITIZE(rs.ortho_dir);
	    VSET(temp, -1.0, -1.0 / aspect, 0.0);
	}
	MAT4X3PNT(rs.viewbase_model, opts->view2model, temp);
    }

    rs.ctx    = ctx;
    rs.opts   = opts;
    rs.pxcb   = pxcb;
    rs.progcb = progcb;
    rs.donecb = donecb;
    rs.ud     = ud;

    rs.scanlines_done  = 0;
    rs.scanlines_total = opts->height;

    /* ── thread count ────────────────────────────────────────────────── */
    nthreads = opts->nthreads;
    if (nthreads <= 0)
	nthreads = bu_avail_cpus();
    if (nthreads > MAX_PSW)
	nthreads = MAX_PSW;
    if (nthreads < 1)
	nthreads = 1;
    rs.nthreads = nthreads;

    /* ── allocate per-thread scanline buffers ────────────────────────── */
    rs.scanbufs = (unsigned char **)bu_calloc(
	nthreads, sizeof(unsigned char *), "render scanbufs");
    for (i = 0; i < nthreads; i++)
	rs.scanbufs[i] = (unsigned char *)bu_calloc(
	    opts->width * 3, sizeof(unsigned char),
	    "render scanbuf row");

    /* ── (FULL mode) set up lights before the frame ──────────────────── */
    if (opts->lightmodel == RENDER_LIGHT_FULL) {
	struct application ap;
	RT_APPLICATION_INIT(&ap);
	ap.a_rt_i    = ctx->rtip;
	ap.a_resource = &ctx->res[0];
	light_init(&ap);
    }

    /* ── fire the workers ────────────────────────────────────────────── */
    if (nthreads == 1) {
	render_worker(0, &rs);
    } else {
	bu_parallel(render_worker, nthreads, &rs);
    }

    /* ── (FULL mode) clean up lights ─────────────────────────────────── */
    if (opts->lightmodel == RENDER_LIGHT_FULL)
	light_cleanup();

    /* ── release scanline buffers ────────────────────────────────────── */
    for (i = 0; i < nthreads; i++)
	bu_free(rs.scanbufs[i], "render scanbuf row");
    bu_free(rs.scanbufs, "render scanbufs");

    /* ── fire completion callback ─────────────────────────────────────── */
    if (donecb)
	donecb(ud, 1 /* ok */);

    return BRLCAD_OK;
}


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
