/*                      E N G I N E . C P P
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
/** @file libanalyze/engine.cpp
 *
 * Internal C++17 engine layer for libanalyze (Phase A).
 *
 * This file implements the types and functions declared in engine.h:
 *
 *   analyze::AnalyzeRequest::from_config()
 *   analyze::apply_request_to_state()
 *   analyze::run()
 *   analyze_engine_run()  [C linkage]
 *
 * It also hosts the capture callbacks (ar_capture_ctx / ar_*_cb) that
 * collect detected issues into struct analyze_results during a
 * perform_raytracing() call.  These were previously static functions in
 * api.c; moving them here is a pure structural change with no effect on
 * observable behaviour.
 *
 * Phase A: structural isolation only — algorithms and behaviour are
 * identical to the code that was inlined in api.c::analyze_run().
 */

#include "common.h"

#include <cstring>
#include <cstdio>

#include "raytrace.h"
#include "vmath.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/parallel.h"

#include "analyze.h"
#include "./analyze_private.h"
#include "./engine.h"


/* ======================================================================
 * Capture context and per-event callbacks.
 *
 * These are registered with current_state before calling
 * perform_raytracing() and accumulate detected issues into
 * struct analyze_results.  They may be invoked from multiple worker
 * threads concurrently; all mutations of the shared result tables are
 * serialised under ctx->sem.
 *
 * Moved verbatim from api.c — no algorithm change.
 * ====================================================================== */

struct ar_capture_ctx {
    struct analyze_results *res;
    int sem; /**< bu_semaphore protecting all list mutations */

    /* Presentation-layer render hooks copied from analyze_config. */
    analyze_overlap_render_fn    overlap_render;
    void                        *overlap_render_data;
    analyze_gap_render_fn        gap_render;
    void                        *gap_render_data;
    analyze_adj_air_render_fn    adj_air_render;
    void                        *adj_air_render_data;
    analyze_exp_air_render_fn    exp_air_render;
    void                        *exp_air_render_data;
    analyze_unconf_air_render_fn unconf_air_render;
    void                        *unconf_air_render_data;
};


/**
 * ar_find_or_insert - locate an existing record by region name(s) or append
 * a new one.
 *
 * For single-region tables (exp_air, first_air, last_air) pass name2 == NULL.
 */
static struct analyze_overlap_record *
ar_find_or_insert(struct bu_ptbl *tbl, const char *name1, const char *name2)
{
    size_t i;
    struct analyze_overlap_record *rec;

    for (i = 0; i < BU_PTBL_LEN(tbl); i++) {
	rec = (struct analyze_overlap_record *)BU_PTBL_GET(tbl, i);
	if (!BU_STR_EQUAL(rec->region1, name1))
	    continue;
	if (!name2 && !rec->region2)
	    return rec;
	if (name2 && rec->region2 && BU_STR_EQUAL(rec->region2, name2))
	    return rec;
    }

    BU_ALLOC(rec, struct analyze_overlap_record);
    rec->region1  = bu_strdup(name1);
    rec->region2  = name2 ? bu_strdup(name2) : NULL;
    rec->count    = 0;
    rec->max_dist = 0.0;
    VSETALL(rec->coord, 0.0);
    bu_ptbl_ins(tbl, (long *)rec);
    return rec;
}


static void
ar_overlap_cb(const struct xray *ray, const struct partition *pp,
	      const struct region *reg1, const struct region *reg2,
	      double depth, void *data)
{
    struct ar_capture_ctx *ctx = (struct ar_capture_ctx *)data;
    struct analyze_overlap_record *rec;
    point_t ihit, ohit;

    VJOIN1(ihit, ray->r_pt, pp->pt_inhit->hit_dist,  ray->r_dir);
    VJOIN1(ohit, ray->r_pt, pp->pt_outhit->hit_dist, ray->r_dir);

    /* Per-segment render hook fires before per-pair deduplication. */
    if (ctx->overlap_render)
	ctx->overlap_render(reg1->reg_name, reg2->reg_name,
			    depth, ihit, ohit, ctx->overlap_render_data);

    bu_semaphore_acquire(ctx->sem);
    rec = ar_find_or_insert(&ctx->res->overlaps,
			    reg1->reg_name, reg2->reg_name);
    rec->count++;
    if (depth > rec->max_dist) {
	rec->max_dist = depth;
	VMOVE(rec->coord, ihit);
    }
    bu_semaphore_release(ctx->sem);
}


static void
ar_gap_cb(const struct xray *ray, const struct partition *pp,
	  double gap_dist, point_t pt, void *data)
{
    struct ar_capture_ctx *ctx = (struct ar_capture_ctx *)data;
    struct analyze_overlap_record *rec;
    /* pt is the entry point of the region after the gap;
     * pp->pt_back is the region before the gap (if any). */
    const char *name_after  = (pp && pp->pt_regionp) ? pp->pt_regionp->reg_name : "";
    const char *name_before = (pp && pp->pt_back && pp->pt_back->pt_regionp)
	? pp->pt_back->pt_regionp->reg_name : "";

    if (ctx->gap_render) {
	point_t gap_start;
	VJOIN1(gap_start, pt, -gap_dist, ray->r_dir);
	ctx->gap_render(name_after, name_before, gap_dist,
			gap_start, pt, ctx->gap_render_data);
    }

    bu_semaphore_acquire(ctx->sem);
    rec = ar_find_or_insert(&ctx->res->gaps, name_after, name_before);
    rec->count++;
    if (gap_dist > rec->max_dist) {
	rec->max_dist = gap_dist;
	VMOVE(rec->coord, pt);
    }
    bu_semaphore_release(ctx->sem);
}


static void
ar_exp_air_cb(const struct partition *pp, point_t last_out,
	      point_t pt, point_t opt, void *data)
{
    struct ar_capture_ctx *ctx = (struct ar_capture_ctx *)data;
    struct analyze_overlap_record *rec;
    const char *name = (pp && pp->pt_regionp) ? pp->pt_regionp->reg_name : "";
    double thickness = (pp) ? pp->pt_outhit->hit_dist - pp->pt_inhit->hit_dist : 0.0;

    if (ctx->exp_air_render)
	ctx->exp_air_render(name, pt, opt, ctx->exp_air_render_data);

    bu_semaphore_acquire(ctx->sem);
    rec = ar_find_or_insert(&ctx->res->exp_air, name, NULL);
    rec->count++;
    if (thickness > rec->max_dist) {
	rec->max_dist = thickness;
	VMOVE(rec->coord, last_out);
    }
    bu_semaphore_release(ctx->sem);
}


static void
ar_adj_air_cb(const struct xray *ray, const struct partition *pp,
	      point_t pt, void *data)
{
    struct ar_capture_ctx *ctx = (struct ar_capture_ctx *)data;
    struct analyze_overlap_record *rec;
    /* Current region is air; back region is the adjacent solid. */
    const char *name_air   = (pp && pp->pt_regionp) ? pp->pt_regionp->reg_name : "";
    const char *name_solid = (pp && pp->pt_back && pp->pt_back->pt_regionp)
	? pp->pt_back->pt_regionp->reg_name : "";

    if (ctx->adj_air_render) {
	double thickness = pp->pt_outhit->hit_dist - pp->pt_inhit->hit_dist;
	point_t out_pt;
	VJOIN1(out_pt, pt, thickness * 0.25, ray->r_dir);
	ctx->adj_air_render(name_solid, name_air, pt, out_pt,
			    ctx->adj_air_render_data);
    }

    bu_semaphore_acquire(ctx->sem);
    rec = ar_find_or_insert(&ctx->res->adj_air, name_solid, name_air);
    rec->count++;
    VMOVE(rec->coord, pt);
    bu_semaphore_release(ctx->sem);
}


static void
ar_first_air_cb(const struct xray *UNUSED(ray), const struct partition *pp,
		void *data)
{
    struct ar_capture_ctx *ctx = (struct ar_capture_ctx *)data;
    struct analyze_overlap_record *rec;
    const char *name = (pp && pp->pt_regionp) ? pp->pt_regionp->reg_name : "";

    bu_semaphore_acquire(ctx->sem);
    rec = ar_find_or_insert(&ctx->res->first_air, name, NULL);
    rec->count++;
    bu_semaphore_release(ctx->sem);
}


static void
ar_last_air_cb(const struct xray *UNUSED(ray), const struct partition *pp,
	       void *data)
{
    struct ar_capture_ctx *ctx = (struct ar_capture_ctx *)data;
    struct analyze_overlap_record *rec;
    const char *name = (pp && pp->pt_regionp) ? pp->pt_regionp->reg_name : "";

    bu_semaphore_acquire(ctx->sem);
    rec = ar_find_or_insert(&ctx->res->last_air, name, NULL);
    rec->count++;
    bu_semaphore_release(ctx->sem);
}


static void
ar_unconf_air_cb(const struct xray *ray,
		 const struct partition *in_p, const struct partition *out_p,
		 void *data)
{
    struct ar_capture_ctx *ctx = (struct ar_capture_ctx *)data;
    struct analyze_overlap_record *rec;
    const char *name_in  = (in_p  && in_p->pt_regionp)  ? in_p->pt_regionp->reg_name  : "";
    const char *name_out = (out_p && out_p->pt_regionp)  ? out_p->pt_regionp->reg_name : "";
    double depth = (in_p && out_p) ?
	in_p->pt_inhit->hit_dist - out_p->pt_outhit->hit_dist : 0.0;

    if (ctx->unconf_air_render && ray) {
	point_t ihit, ohit;
	VJOIN1(ihit, ray->r_pt, in_p->pt_inhit->hit_dist,   ray->r_dir);
	VJOIN1(ohit, ray->r_pt, out_p->pt_outhit->hit_dist, ray->r_dir);
	ctx->unconf_air_render(name_in, name_out, ihit, ohit,
			       ctx->unconf_air_render_data);
    }

    bu_semaphore_acquire(ctx->sem);
    rec = ar_find_or_insert(&ctx->res->unconf_air, name_in, name_out);
    rec->count++;
    if (depth > rec->max_dist) {
	rec->max_dist = depth;
	if (ray && in_p)
	    VJOIN1(rec->coord, ray->r_pt, in_p->pt_inhit->hit_dist, ray->r_dir);
    }
    bu_semaphore_release(ctx->sem);
}


/* ======================================================================
 * C++17 engine implementation
 * ====================================================================== */

namespace analyze {

/**
 * Build an AnalyzeRequest from a public analyze_config.
 *
 * When cfg == NULL all fields are left at their C++ default values,
 * which mirror analyze_current_state_init() defaults.
 *
 * This function consolidates the mapping logic that was previously
 * inlined inside analyze_run() in api.c.
 */
AnalyzeRequest
AnalyzeRequest::from_config(const struct analyze_config *cfg, int analysis_flags)
{
    AnalyzeRequest req;
    req.flags = analysis_flags;

    if (!cfg)
	return req; /* all defaults */

    req.sampler = cfg->sampler;
    if (cfg->num_views > 0)
	req.num_views = cfg->num_views;
    req.azimuth_deg   = cfg->azimuth_deg;
    req.elevation_deg = cfg->elevation_deg;
    if (cfg->grid_spacing > 0.0)
	req.grid_spacing = cfg->grid_spacing;
    if (cfg->grid_spacing_min > 0.0)
	req.grid_spacing_min = cfg->grid_spacing_min;
    if (cfg->aspect > 0.0)
	req.aspect = cfg->aspect;

    req.grid_width  = cfg->grid_width;
    req.grid_height = cfg->grid_height;

    req.quiet_missed = cfg->quiet_missed;
    if (cfg->samples_per_model_axis > 0.0)
	req.samples_per_model_axis = cfg->samples_per_model_axis;

    req.view_size = cfg->view_size;
    VMOVE(req.view_eye,  cfg->view_eye);
    HMOVE(req.view_quat, cfg->view_quat);

    req.overlap_tol = cfg->overlap_tol;
    if (cfg->volume_tol >= 0.0)
	req.volume_tol = cfg->volume_tol;
    if (cfg->mass_tol >= 0.0)
	req.mass_tol = cfg->mass_tol;
    if (cfg->surf_area_tol >= 0.0)
	req.surf_area_tol = cfg->surf_area_tol;

    req.density_file = cfg->density_file;

    req.use_air = cfg->use_air;
    if (cfg->ncpu > 0)
	req.ncpu = cfg->ncpu;
    if (cfg->required_hits > 0)
	req.required_hits = cfg->required_hits;

    req.verbose = cfg->verbose;
    req.log_str = cfg->log_str;

    if (cfg->timeout_ms > 0)
	req.timeout_ms = cfg->timeout_ms;
    req.required_digits = cfg->required_digits;

    if (cfg->n_crofton_rays > 0)
	req.n_crofton_rays = cfg->n_crofton_rays;

    req.volume_plot_file = cfg->volume_plot_file;

    req.overlap_render      = cfg->overlap_render;
    req.overlap_render_data = cfg->overlap_render_data;
    req.gap_render          = cfg->gap_render;
    req.gap_render_data     = cfg->gap_render_data;
    req.adj_air_render      = cfg->adj_air_render;
    req.adj_air_render_data = cfg->adj_air_render_data;
    req.exp_air_render      = cfg->exp_air_render;
    req.exp_air_render_data = cfg->exp_air_render_data;
    req.unconf_air_render      = cfg->unconf_air_render;
    req.unconf_air_render_data = cfg->unconf_air_render_data;

    return req;
}


/**
 * apply_request_to_state - populate a current_state from an AnalyzeRequest.
 *
 * This is the single authoritative mapping from the typed request to the
 * legacy current_state.  Phase B will shrink this function as sampler
 * strategies assume more responsibility; for now it is an exact
 * equivalent of the mapping code that was inlined in analyze_run().
 */
void
apply_request_to_state(const AnalyzeRequest &req, struct current_state *state)
{
    state->sampler = req.sampler;
    if (req.num_views > 0)
	state->num_views = req.num_views;
    state->azimuth_deg   = req.azimuth_deg;
    state->elevation_deg = req.elevation_deg;
    if (req.grid_spacing > 0.0)
	state->gridSpacing = req.grid_spacing;
    if (req.grid_spacing_min > 0.0)
	state->gridSpacingLimit = req.grid_spacing_min;
    if (req.aspect > 0.0)
	state->aspect = req.aspect;
    if (req.grid_width > 0 || req.grid_height > 0) {
	state->grid_size_flag = 1;
	state->grid_width  = (fastf_t)req.grid_width;
	state->grid_height = (fastf_t)(req.grid_height > 0 ? req.grid_height
							   : req.grid_width);
    }
    if (req.quiet_missed)
	state->quiet_missed_report = 1;
    if (req.samples_per_model_axis > 0.0)
	state->samples_per_model_axis = req.samples_per_model_axis;

    if (req.sampler == ANALYZE_SAMPLER_VIEW_PLANE && req.view_size > 0.0) {
	point_t eye;
	quat_t  quat;
	VMOVE(eye,  req.view_eye);
	HMOVE(quat, req.view_quat);
	analyze_set_view_information(state, req.view_size, &eye, &quat);
    }

    state->overlap_tolerance = req.overlap_tol;
    if (req.volume_tol >= 0.0)
	state->volume_tolerance = req.volume_tol;
    if (req.mass_tol >= 0.0)
	state->mass_tolerance = req.mass_tol;
    if (req.surf_area_tol >= 0.0)
	state->sa_tolerance = req.surf_area_tol;

    if (req.density_file)
	state->densityFileName = (char *)req.density_file;

    state->use_air = req.use_air;
    if (req.ncpu > 0)
	state->ncpu = req.ncpu;
    if (req.required_hits > 0)
	state->required_number_hits = req.required_hits;

    state->verbose = req.verbose;
    if (req.log_str) {
	if (req.verbose)
	    analyze_enable_verbose(state, req.log_str);
	else
	    analyze_enable_debug(state, req.log_str);
    }

    if (req.timeout_ms > 0)
	state->timeout_ms = req.timeout_ms;
    state->required_digits = req.required_digits;

    if (req.n_crofton_rays > 0)
	state->crofton_n_rays = req.n_crofton_rays;

    if (req.volume_plot_file)
	analyze_set_volume_plotfile(state, req.volume_plot_file);
}


/**
 * run - execute the full analysis pipeline from a typed AnalyzeRequest.
 *
 * This is the single execution choke-point that both analyze_run() (via
 * the analyze_engine_run() C wrapper) and, in a later phase, the legacy
 * perform_raytracing() compatibility path will converge on.
 *
 * Steps:
 *   1. Allocate struct analyze_results and initialise all bu_ptbl lists.
 *   2. Create current_state via analyze_current_state_init().
 *   3. Populate it with apply_request_to_state().
 *   4. Register capture callbacks for every requested issue type.
 *   5. Invoke perform_raytracing().
 *   6. Harvest scalar totals and per-object / per-region arrays.
 *   7. Free the current_state and return the result.
 *
 * Returns NULL on error.
 */
struct analyze_results *
run(const AnalyzeRequest &req, struct db_i *dbip, char *names[], int num_names)
{
    struct analyze_results *res;
    struct current_state   *state;
    struct ar_capture_ctx   ctx;
    int i;
    int ret;
    const int flags = req.flags;

    /* ------------------------------------------------------------------
     * Allocate result container.
     * ------------------------------------------------------------------ */
    BU_ALLOC(res, struct analyze_results);
    memset(res, 0, sizeof(*res));
    bu_ptbl_init(&res->overlaps,   8, "ar overlaps");
    bu_ptbl_init(&res->gaps,       8, "ar gaps");
    bu_ptbl_init(&res->adj_air,    8, "ar adj_air");
    bu_ptbl_init(&res->exp_air,    8, "ar exp_air");
    bu_ptbl_init(&res->first_air,  8, "ar first_air");
    bu_ptbl_init(&res->last_air,   8, "ar last_air");
    bu_ptbl_init(&res->unconf_air, 8, "ar unconf_air");

    /* ------------------------------------------------------------------
     * Create and configure current_state from the request.
     * ------------------------------------------------------------------ */
    state = analyze_current_state_init();
    apply_request_to_state(req, state);

    /* ------------------------------------------------------------------
     * Set up capture context and register issue-collection callbacks.
     * ------------------------------------------------------------------ */
    ctx.res = res;
    ctx.sem = bu_semaphore_register("analyze_run_results_sem");
    ctx.overlap_render         = req.overlap_render;
    ctx.overlap_render_data    = req.overlap_render_data;
    ctx.gap_render             = req.gap_render;
    ctx.gap_render_data        = req.gap_render_data;
    ctx.adj_air_render         = req.adj_air_render;
    ctx.adj_air_render_data    = req.adj_air_render_data;
    ctx.exp_air_render         = req.exp_air_render;
    ctx.exp_air_render_data    = req.exp_air_render_data;
    ctx.unconf_air_render      = req.unconf_air_render;
    ctx.unconf_air_render_data = req.unconf_air_render_data;

    if (flags & ANALYZE_OVERLAPS)
	analyze_register_overlaps_callback(state,  ar_overlap_cb,    &ctx);
    if (flags & ANALYZE_GAP)
	analyze_register_gaps_callback(    state,  ar_gap_cb,        &ctx);
    if (flags & ANALYZE_EXP_AIR)
	analyze_register_exp_air_callback( state,  ar_exp_air_cb,    &ctx);
    if (flags & ANALYZE_ADJ_AIR)
	analyze_register_adj_air_callback( state,  ar_adj_air_cb,    &ctx);
    if (flags & ANALYZE_FIRST_AIR)
	analyze_register_first_air_callback(state, ar_first_air_cb,  &ctx);
    if (flags & ANALYZE_LAST_AIR)
	analyze_register_last_air_callback( state, ar_last_air_cb,   &ctx);
    if (flags & ANALYZE_UNCONF_AIR)
	analyze_register_unconf_air_callback(state, ar_unconf_air_cb, &ctx);

    /* ------------------------------------------------------------------
     * Run the analysis.
     * ------------------------------------------------------------------ */
    ret = perform_raytracing(state, dbip, names, num_names, flags);
    if (ret != ANALYZE_OK) {
	analyze_free_current_state(state);
	analyze_results_free(res);
	return NULL;
    }

    /* Record the last grid spacing actually used for triple/rotated samplers.
     * perform_raytracing() halves gridSpacing once more after the final pass,
     * so the last-used value is gridSpacing * 2.  Crofton has no iterative
     * grid: leave final_grid_spacing at 0. */
    if (state->sampler != ANALYZE_SAMPLER_CROFTON && state->gridSpacing > 0.0)
	res->final_grid_spacing = state->gridSpacing * 2.0;

    res->sampler_type  = state->sampler;
    res->is_stochastic = ((flags & ~ANALYZE_BOX) != 0) ? 1 : 0;

    /* Bounding box via a separate lightweight rt_prep pass. */
    if (flags & ANALYZE_BOX)
	analyze_bbox(dbip, names, num_names, res->bbox_min, res->bbox_max);

    /* ------------------------------------------------------------------
     * Harvest scalar totals.
     * ------------------------------------------------------------------ */
    if (flags & ANALYZE_VOLUME)
	res->total_volume    = analyze_total_volume(state);
    if (flags & ANALYZE_MASS)
	res->total_mass      = analyze_total_mass(state);
    if (flags & ANALYZE_SURF_AREA)
	res->total_surf_area = analyze_total_surf_area(state);
    if (flags & ANALYZE_CENTROIDS)
	analyze_total_centroid(state, res->centroid);
    if (flags & ANALYZE_MOMENTS)
	analyze_moments_total(state, res->moments_of_inertia);

    /* ------------------------------------------------------------------
     * Harvest per-input-object results.
     * ------------------------------------------------------------------ */
    if (num_names > 0) {
	res->objects = (struct analyze_object_result *)bu_calloc(
		(size_t)num_names,
		sizeof(struct analyze_object_result),
		"ar object results");
	res->n_objects = (size_t)num_names;

	for (i = 0; i < num_names; i++) {
	    res->objects[i].name = bu_strdup(names[i]);
	    if (flags & ANALYZE_VOLUME)
		res->objects[i].volume    = analyze_volume(state, names[i]);
	    if (flags & ANALYZE_MASS)
		res->objects[i].mass      = analyze_mass(state, names[i]);
	    if (flags & ANALYZE_SURF_AREA)
		res->objects[i].surf_area = analyze_surf_area(state, names[i]);
	    if (flags & ANALYZE_CENTROIDS)
		analyze_centroid(state, names[i], res->objects[i].centroid);
	    if (flags & ANALYZE_MOMENTS)
		analyze_moments(state, names[i], res->objects[i].moments_of_inertia);
	    if (flags & ANALYZE_BOX) {
		char *single[2];
		single[0] = names[i];
		single[1] = NULL;
		analyze_bbox(dbip, single, 1,
			     res->objects[i].bbox_min,
			     res->objects[i].bbox_max);
	    }
	}
    }

    /* ------------------------------------------------------------------
     * Harvest per-region results.
     * ------------------------------------------------------------------ */
    {
	int num_regions = analyze_get_num_regions(state);
	if (num_regions > 0) {
	    res->regions = (struct analyze_region_result *)bu_calloc(
		    (size_t)num_regions,
		    sizeof(struct analyze_region_result),
		    "ar region results");
	    res->n_regions = (size_t)num_regions;

	    for (i = 0; i < num_regions; i++) {
		char  *rname    = NULL;
		double vol      = 0.0, mass = 0.0, sa = 0.0;
		double dummy_hi = 0.0, dummy_lo = 0.0;

		if (flags & ANALYZE_VOLUME)
		    analyze_volume_region(state, i, &rname, &vol,
					  &dummy_hi, &dummy_lo);
		if (flags & ANALYZE_MASS)
		    analyze_mass_region(state, i, &rname, &mass,
					&dummy_hi, &dummy_lo);
		if (flags & ANALYZE_SURF_AREA)
		    analyze_surf_area_region(state, i, &rname, &sa,
					     &dummy_hi, &dummy_lo);

		/* Fall back to reg_tbl name if no getter set rname. */
		if (!rname && state->reg_tbl[i].r_name)
		    rname = state->reg_tbl[i].r_name;

		res->regions[i].name      = rname ? bu_strdup(rname) : bu_strdup("");
		res->regions[i].volume    = vol;
		res->regions[i].mass      = mass;
		res->regions[i].surf_area = sa;
		res->regions[i].hits      = state->reg_tbl[i].hits;
	    }
	}
    }

    analyze_free_current_state(state);
    return res;
}

} /* namespace analyze */


/* ======================================================================
 * C linkage wrapper — callable from api.c and other plain-C files.
 * ====================================================================== */

extern "C" struct analyze_results *
analyze_engine_run(const struct analyze_config *cfg, struct db_i *dbip,
		   char *names[], int num_names, int flags)
{
    analyze::AnalyzeRequest req =
	analyze::AnalyzeRequest::from_config(cfg, flags);
    return analyze::run(req, dbip, names, num_names);
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
