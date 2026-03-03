/*                    R E N D E R _ C T X . C
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
/** @file librender/render_ctx.c
 *
 * Context management for librender.
 *
 * A render_ctx_t encapsulates:
 *  - An open BRL-CAD geometry database (struct rt_i)
 *  - Pre-prepped (BVH-built) geometry for the requested objects
 *  - Per-thread ray-tracing resource slots
 *  - (When RENDER_LIGHT_FULL is used) liboptical shader head pointer
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/parallel.h"    /* MAX_PSW */
#include "bu/str.h"
#include "raytrace.h"
#include "optical.h"

#include "render.h"
#include "./render_private.h"


struct render_ctx *
render_ctx_create(const char *dbfile, int nobjs, const char **objs)
{
    struct render_ctx *ctx;
    struct rt_i *rtip;
    char title[MAXPATHLEN];
    int i;

    if (!dbfile || !bu_file_exists(dbfile, NULL)) {
	bu_log("render_ctx_create: database '%s' not found\n",
	       dbfile ? dbfile : "(null)");
	return NULL;
    }
    if (nobjs < 1 || !objs) {
	bu_log("render_ctx_create: at least one object name required\n");
	return NULL;
    }

    /* ── open the geometry database ─────────────────────────────────── */
    rtip = rt_dirbuild(dbfile, title, sizeof(title));
    if (rtip == RTI_NULL) {
	bu_log("render_ctx_create: rt_dirbuild('%s') failed\n", dbfile);
	return NULL;
    }

    /* ── load the requested tree-tops ───────────────────────────────── */
    for (i = 0; i < nobjs; i++) {
	if (rt_gettree(rtip, objs[i]) < 0) {
	    bu_log("render_ctx_create: failed to load '%s'\n", objs[i]);
	    rt_free_rti(rtip);
	    return NULL;
	}
    }

    /* ── prep (build BVH / BSP tree) ───────────────────────────────── */
    rt_prep_parallel(rtip, bu_avail_cpus());

    /* ── allocate and populate the context ─────────────────────────── */
    BU_GET(ctx, struct render_ctx);
    ctx->rtip     = rtip;
    ctx->dbfile   = bu_strdup(dbfile);
    ctx->mfHead   = MF_NULL;

    /* allocate per-CPU resource slots */
    ctx->nres = MAX_PSW;
    ctx->res  = (struct resource *)bu_calloc(
	ctx->nres, sizeof(struct resource), "render_ctx resources");

    for (i = 0; i < ctx->nres; i++)
	rt_init_resource(&ctx->res[i], i, rtip);

    /* ── (optional) init liboptical shaders for FULL lighting ───────── */
    optical_shader_init(&ctx->mfHead);

    {
	/* Set up material shaders for every region now that geometry is prepped. */
	struct region *regp;
	for (BU_LIST_FOR(regp, region, &(rtip->HeadRegion)))
	    (void)mlib_setup(&ctx->mfHead, regp, rtip);
    }

    return ctx;
}


void
render_ctx_destroy(struct render_ctx *ctx)
{
    int i;

    if (!ctx)
	return;

    /* free per-thread resources */
    for (i = 0; i < ctx->nres; i++)
	rt_clean_resource(ctx->rtip, &ctx->res[i]);
    bu_free(ctx->res, "render_ctx resources");

    /* clean up optical materials */
    {
	struct region *regp;
	for (BU_LIST_FOR(regp, region, &(ctx->rtip->HeadRegion)))
	    mlib_free(regp);
    }

    if (ctx->rtip)
	rt_free_rti(ctx->rtip);

    bu_free(ctx->dbfile, "render_ctx dbfile");
    BU_PUT(ctx, struct render_ctx);
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
