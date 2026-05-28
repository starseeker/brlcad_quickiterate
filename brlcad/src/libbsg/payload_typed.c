/*             P A Y L O A D _ T Y P E D . C
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
/** @file libbsg/payload_typed.c
 *
 * Phase D1 (drawing_modernization): typed payload object model.
 *
 * Implements the typed payload lifecycle helpers plus the concrete payload
 * builders currently used by the drawing modernization work.
 */

#include "common.h"

#include <string.h>

#include "bg/polygon.h"
#include "bu/list.h"
#include "bu/malloc.h"
#include "bu/vls.h"
#include "bsg/defines.h"
#include "bsg/faceplate.h"
#include "bsg/lod.h"
#include "bsg/polygon.h"
#include "bsg/payload_typed.h"
#include "bsg/vlist.h"

static unsigned long long
_typed_payload_flags(bsg_payload_type type)
{
    switch (type) {
	case BSG_PL_VLIST:
	case BSG_PL_LINE_SET:
	case BSG_PL_TEXT:
	case BSG_PL_HUD_TEXT:
	case BSG_PL_POLYGON:
	case BSG_PL_IMAGE:
	case BSG_PL_FRAMEBUFFER:
	case BSG_PL_AXES:
	case BSG_PL_GRID:
	case BSG_PL_ANNOTATION:
	    return BSG_PAYLOAD_VLIST;
	case BSG_PL_MESH:
	    return BSG_PAYLOAD_MESH;
	case BSG_PL_CSG:
	    return BSG_PAYLOAD_CSG;
	case BSG_PL_BREP:
	    return BSG_PAYLOAD_BREP;
	default:
	    return 0;
    }
}

static int
_no_bounds(struct bsg_payload *UNUSED(pl), point_t *UNUSED(bmin), point_t *UNUSED(bmax))
{
    return 0;
}

static int
_no_export(struct bsg_payload *UNUSED(pl), struct bu_vls *UNUSED(out))
{
    return 0;
}

static int
_no_backend_prepare(struct bsg_payload *UNUSED(pl), void *UNUSED(backend_ctx))
{
    return 0;
}

static void
_payload_defaults(struct bsg_payload *pl)
{
    if (!pl)
	return;
    pl->pl_bounds = _no_bounds;
    pl->pl_export = _no_export;
    pl->pl_backend_prepare = _no_backend_prepare;
}


/* -----------------------------------------------------------------------
 * Core payload lifecycle
 * ----------------------------------------------------------------------- */

struct bsg_payload *
bsg_payload_create(bsg_payload_type type)
{
    struct bsg_payload *pl;
    BU_GET(pl, struct bsg_payload);
    memset(pl, 0, sizeof(*pl));
    pl->pl_type     = type;
    pl->pl_revision = 0;
    _payload_defaults(pl);
    return pl;
}


void
bsg_payload_free(struct bsg_payload *pl)
{
    if (!pl)
	return;
    if (pl->pl_free)
	pl->pl_free(pl);
    else
	BU_PUT(pl, struct bsg_payload);
}


void
bsg_payload_bump_revision(struct bsg_payload *pl)
{
    if (!pl)
	return;
    pl->pl_revision++;
}


/* -----------------------------------------------------------------------
 * Node ↔ payload binding
 * ----------------------------------------------------------------------- */

void
bsg_node_set_payload(bsg_node *node, struct bsg_payload *pl)
{
    if (!node)
	return;

    /* Free any existing payload */
    if (node->pl)
	bsg_payload_free(node->pl);

    node->pl = pl;
    node->s_type_flags &= ~BSG_PAYLOAD_MASK;
    if (pl)
	node->s_type_flags |= _typed_payload_flags(pl->pl_type);
}


struct bsg_payload *
bsg_node_get_payload(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->pl;
}


/* -----------------------------------------------------------------------
 * Typed payload update dispatch
 * ----------------------------------------------------------------------- */

void
bsg_payload_update(bsg_node *node, struct bsg_view *v)
{
    if (!node || !node->pl)
	return;
    struct bsg_payload *pl = node->pl;
    if (pl->pl_update)
	pl->pl_update(pl, v);
}


/* -----------------------------------------------------------------------
 * VLIST payload (node-owned geometry) — Phase D1
 * ----------------------------------------------------------------------- */

static void
_vlist_payload_free(struct bsg_payload *pl)
{
    if (!pl)
	return;
    if (pl->pl.vlist)
	BU_PUT(pl->pl.vlist, struct bsg_payload_vlist);
    BU_PUT(pl, struct bsg_payload);
}

static int
_vlist_payload_bounds(struct bsg_payload *pl, point_t *bmin, point_t *bmax)
{
    if (!pl || !pl->pl.vlist || !pl->pl.vlist->vlist)
	return 0;

    size_t length = 0;
    int dispmode = 0;
    return bsg_vlist_bbox(pl->pl.vlist->vlist, bmin, bmax, &length, &dispmode);
}

static int
_vlist_payload_export(struct bsg_payload *pl, struct bu_vls *out)
{
    if (!pl || !pl->pl.vlist || !pl->pl.vlist->vlist || !out)
	return -1;
    bsg_vlist_export(out, pl->pl.vlist->vlist, "payload_vlist");
    return 0;
}

struct bsg_payload *
bsg_payload_vlist_create(struct bu_list *vlist_head, struct bu_list *vlfree)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_VLIST);
    if (!pl)
	return NULL;

    struct bsg_payload_vlist *vl;
    BU_GET(vl, struct bsg_payload_vlist);
    vl->vlist = vlist_head;
    vl->vlfree = vlfree;

    pl->pl.vlist = vl;
    pl->pl_free = _vlist_payload_free;
    pl->pl_bounds = _vlist_payload_bounds;
    pl->pl_export = _vlist_payload_export;
    return pl;
}

struct bsg_payload_vlist *
bsg_payload_vlist_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_VLIST)
	return NULL;
    return payload->pl.vlist;
}

int
bsg_node_ensure_vlist_payload(bsg_node *node)
{
    if (!node)
	return 0;
    if (node->pl && node->pl->pl_type == BSG_PL_VLIST) {
	if (node->pl->pl.vlist) {
	    node->pl->pl.vlist->vlist = &node->s_vlist;
	    node->pl->pl.vlist->vlfree = node->vlfree;
	}
	return 1;
    }

    struct bsg_payload *pl = bsg_payload_vlist_create(&node->s_vlist, node->vlfree);
    if (!pl)
	return 0;
    bsg_node_set_payload(node, pl);
    return 1;
}

int
bsg_node_clear_vlist_payload(bsg_node *node)
{
    if (!node || !bsg_node_ensure_vlist_payload(node))
	return 0;

    if (BU_LIST_IS_INITIALIZED(&node->s_vlist))
	BSG_FREE_VLIST(node->vlfree, &node->s_vlist);
    BU_LIST_INIT(&node->s_vlist);
    node->s_vlen = 0;
    bsg_payload_bump_revision(node->pl);
    return 1;
}

int
bsg_node_copy_vlist_payload(bsg_node *node, const struct bu_list *src)
{
    if (!node || !src || !bsg_node_ensure_vlist_payload(node))
	return 0;

    if (BU_LIST_IS_INITIALIZED(&node->s_vlist))
	BSG_FREE_VLIST(node->vlfree, &node->s_vlist);
    BU_LIST_INIT(&node->s_vlist);
    bsg_vlist_copy(node->vlfree, &node->s_vlist, src);
    node->s_vlen = 0;
    bsg_vlist *vp;
    for (BU_LIST_FOR(vp, bsg_vlist, &node->s_vlist))
	node->s_vlen += vp->nused;
    bsg_payload_bump_revision(node->pl);
    return 1;
}

int
bsg_node_append_vlist_payload(bsg_node *node, const point_t pt, int cmd)
{
    if (!node || !pt || !bsg_node_ensure_vlist_payload(node))
	return 0;

    BSG_ADD_VLIST(node->vlfree, &node->s_vlist, pt, cmd);
    node->s_vlen++;
    bsg_payload_bump_revision(node->pl);
    return 1;
}


/* -----------------------------------------------------------------------
 * TEXT payload (bsg_label) — Phase D1 pilot
 * ----------------------------------------------------------------------- */

static void
_text_payload_free(struct bsg_payload *pl)
{
    if (!pl)
	return;
    struct bsg_label *label = pl->pl.text;
    if (label) {
	bu_vls_free(&label->label);
	BU_PUT(label, struct bsg_label);
    }
    BU_PUT(pl, struct bsg_payload);
}


struct bsg_payload *
bsg_payload_text_create(struct bsg_label *label)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_TEXT);
    if (!pl)
	return NULL;
    pl->pl.text  = label;
    pl->pl_free  = _text_payload_free;
    return pl;
}


struct bsg_label *
bsg_payload_text_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_TEXT)
	return NULL;
    return payload->pl.text;
}


/* -----------------------------------------------------------------------
 * HUD_TEXT payload — Phase D1
 * ----------------------------------------------------------------------- */

struct bsg_payload *
bsg_payload_hud_text_create(struct bsg_label *label)
{
    struct bsg_payload *pl = bsg_payload_text_create(label);
    if (!pl)
	return NULL;
    pl->pl_type = BSG_PL_HUD_TEXT;
    pl->pl.hud_text = label;
    return pl;
}

struct bsg_label *
bsg_payload_hud_text_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_HUD_TEXT)
	return NULL;
    return payload->pl.hud_text;
}


/* -----------------------------------------------------------------------
 * LINE_SET payload — Phase D1
 * ----------------------------------------------------------------------- */

static void
_line_set_payload_free(struct bsg_payload *pl)
{
    if (!pl)
	return;
    struct bsg_payload_line_set *ls = pl->pl.line_set;
    if (ls) {
	if (ls->points)
	    bu_free(ls->points, "payload line-set points");
	if (ls->cmds)
	    bu_free(ls->cmds, "payload line-set cmds");
	BU_PUT(ls, struct bsg_payload_line_set);
    }
    BU_PUT(pl, struct bsg_payload);
}

static int
_line_set_payload_bounds(struct bsg_payload *pl, point_t *bmin, point_t *bmax)
{
    if (!pl || !pl->pl.line_set || !pl->pl.line_set->point_cnt)
	return 0;
    VSETALL((*bmin), INFINITY);
    VSETALL((*bmax), -INFINITY);
    for (size_t i = 0; i < pl->pl.line_set->point_cnt; i++) {
	VMINMAX((*bmin), (*bmax), pl->pl.line_set->points[i]);
    }
    return 1;
}

struct bsg_payload *
bsg_payload_line_set_create(point_t *points, const int *cmds, size_t point_cnt)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_LINE_SET);
    if (!pl)
	return NULL;

    struct bsg_payload_line_set *ls;
    BU_GET(ls, struct bsg_payload_line_set);
    memset(ls, 0, sizeof(*ls));
    ls->point_cnt = point_cnt;
    if (point_cnt) {
	ls->points = (point_t *)bu_calloc(point_cnt, sizeof(point_t), "payload line-set points");
	ls->cmds = (int *)bu_calloc(point_cnt, sizeof(int), "payload line-set cmds");
	for (size_t i = 0; i < point_cnt; i++) {
	    if (points)
		VMOVE(ls->points[i], points[i]);
	    ls->cmds[i] = cmds ? cmds[i] : ((i % 2) ? BSG_VLIST_LINE_DRAW : BSG_VLIST_LINE_MOVE);
	}
    }

    pl->pl.line_set = ls;
    pl->pl_free = _line_set_payload_free;
    pl->pl_bounds = _line_set_payload_bounds;
    return pl;
}

struct bsg_payload_line_set *
bsg_payload_line_set_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_LINE_SET)
	return NULL;
    return payload->pl.line_set;
}


int
bsg_payload_line_set_append_segments(struct bsg_payload *payload,
	const point_t *points, const int *cmds, size_t add_cnt)
{
    if (!payload || payload->pl_type != BSG_PL_LINE_SET || !add_cnt)
	return 0;
    struct bsg_payload_line_set *ls = payload->pl.line_set;
    if (!ls)
	return 0;
    size_t old_cnt = ls->point_cnt;
    size_t new_cnt = old_cnt + add_cnt;
    point_t *new_pts;
    int *new_cmds;
    if (ls->points) {
	new_pts = (point_t *)bu_realloc(ls->points, new_cnt * sizeof(point_t), "line-set append points");
    } else {
	new_pts = (point_t *)bu_calloc(new_cnt, sizeof(point_t), "line-set append points");
    }
    if (ls->cmds) {
	new_cmds = (int *)bu_realloc(ls->cmds, new_cnt * sizeof(int), "line-set append cmds");
    } else {
	new_cmds = (int *)bu_calloc(new_cnt, sizeof(int), "line-set append cmds");
    }
    if (!new_pts || !new_cmds) {
	if (new_pts) bu_free(new_pts, "line-set append points");
	if (new_cmds) bu_free(new_cmds, "line-set append cmds");
	return 0;
    }
    ls->points = new_pts;
    ls->cmds = new_cmds;
    for (size_t i = 0; i < add_cnt; i++) {
	if (points)
	    VMOVE(ls->points[old_cnt + i], points[i]);
	ls->cmds[old_cnt + i] = cmds ? cmds[i] : BSG_VLIST_LINE_DRAW;
    }
    ls->point_cnt = new_cnt;
    bsg_payload_bump_revision(payload);
    return 1;
}

int
bsg_payload_line_set_replace(struct bsg_payload *payload,
	const point_t *points, const int *cmds, size_t point_cnt)
{
    if (!payload || payload->pl_type != BSG_PL_LINE_SET)
	return 0;
    struct bsg_payload_line_set *ls = payload->pl.line_set;
    if (!ls)
	return 0;
    if (ls->points)
	bu_free(ls->points, "line-set replace points");
    if (ls->cmds)
	bu_free(ls->cmds, "line-set replace cmds");
    ls->points = NULL;
    ls->cmds = NULL;
    ls->point_cnt = 0;
    if (point_cnt) {
	ls->points = (point_t *)bu_calloc(point_cnt, sizeof(point_t), "line-set replace points");
	ls->cmds = (int *)bu_calloc(point_cnt, sizeof(int), "line-set replace cmds");
	for (size_t i = 0; i < point_cnt; i++) {
	    if (points)
		VMOVE(ls->points[i], points[i]);
	    ls->cmds[i] = cmds ? cmds[i] : ((i % 2) ? BSG_VLIST_LINE_DRAW : BSG_VLIST_LINE_MOVE);
	}
	ls->point_cnt = point_cnt;
    }
    bsg_payload_bump_revision(payload);
    return 1;
}

int
bsg_payload_line_set_clear(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_LINE_SET)
	return 0;
    struct bsg_payload_line_set *ls = payload->pl.line_set;
    if (!ls)
	return 0;
    if (ls->points)
	bu_free(ls->points, "line-set clear points");
    if (ls->cmds)
	bu_free(ls->cmds, "line-set clear cmds");
    ls->points = NULL;
    ls->cmds = NULL;
    ls->point_cnt = 0;
    bsg_payload_bump_revision(payload);
    return 1;
}

size_t
bsg_payload_line_set_point_count(const struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_LINE_SET || !payload->pl.line_set)
	return 0;
    return payload->pl.line_set->point_cnt;
}

int
bsg_payload_line_set_cmd_at(const struct bsg_payload *payload, size_t idx)
{
    if (!payload || payload->pl_type != BSG_PL_LINE_SET || !payload->pl.line_set)
	return -1;
    struct bsg_payload_line_set *ls = payload->pl.line_set;
    if (idx >= ls->point_cnt || !ls->cmds)
	return -1;
    return ls->cmds[idx];
}


/* -----------------------------------------------------------------------
 * POLYGON payload — Phase D1
 * ----------------------------------------------------------------------- */

static void
_polygon_payload_free(struct bsg_payload *pl)
{
    if (!pl)
	return;
    struct bsg_polygon *poly = pl->pl.polygon;
    if (poly) {
	bg_polygon_free(&poly->polygon);
	BU_PUT(poly, struct bsg_polygon);
    }
    BU_PUT(pl, struct bsg_payload);
}

static int
_polygon_payload_bounds(struct bsg_payload *pl, point_t *bmin, point_t *bmax)
{
    if (!pl || !pl->pl.polygon || !pl->pl.polygon->polygon.num_contours)
	return 0;
    VSETALL((*bmin), INFINITY);
    VSETALL((*bmax), -INFINITY);
    struct bsg_polygon *poly = pl->pl.polygon;
    for (size_t i = 0; i < poly->polygon.num_contours; i++) {
	struct bg_poly_contour *c = &poly->polygon.contour[i];
	for (size_t j = 0; j < c->num_points; j++) {
	    VMINMAX((*bmin), (*bmax), c->point[j]);
	}
    }
    return 1;
}

struct bsg_payload *
bsg_payload_polygon_create(struct bsg_polygon *polygon)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_POLYGON);
    if (!pl)
	return NULL;
    pl->pl.polygon = polygon;
    pl->pl_free = _polygon_payload_free;
    pl->pl_bounds = _polygon_payload_bounds;
    return pl;
}

struct bsg_polygon *
bsg_payload_polygon_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_POLYGON)
	return NULL;
    return payload->pl.polygon;
}


/* -----------------------------------------------------------------------
 * MESH / CSG / BREP payloads — Phase D1
 * ----------------------------------------------------------------------- */

static void
_mesh_payload_free(struct bsg_payload *pl)
{
    if (!pl)
	return;
    if (pl->pl.mesh)
	bsg_mesh_lod_destroy(pl->pl.mesh);
    BU_PUT(pl, struct bsg_payload);
}

struct bsg_payload *
bsg_payload_mesh_create(struct bsg_mesh_lod *mesh)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_MESH);
    if (!pl)
	return NULL;
    pl->pl.mesh = mesh;
    pl->pl_free = _mesh_payload_free;
    return pl;
}

struct bsg_mesh_lod *
bsg_payload_mesh_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_MESH)
	return NULL;
    return payload->pl.mesh;
}

struct bsg_payload *
bsg_payload_csg_create(void *opaque)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_CSG);
    if (!pl)
	return NULL;
    pl->pl.csg = opaque;
    return pl;
}

void *
bsg_payload_csg_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_CSG)
	return NULL;
    return payload->pl.csg;
}

struct bsg_payload *
bsg_payload_brep_create(void *opaque)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_BREP);
    if (!pl)
	return NULL;
    pl->pl.brep = opaque;
    return pl;
}

void *
bsg_payload_brep_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_BREP)
	return NULL;
    return payload->pl.brep;
}


/* -----------------------------------------------------------------------
 * IMAGE / FRAMEBUFFER payloads — Phase D1
 * ----------------------------------------------------------------------- */

static void
_image_payload_free(struct bsg_payload *pl)
{
    if (!pl)
	return;
    struct bsg_payload_image *img = pl->pl.image;
    if (img) {
	if (img->pixels)
	    bu_free(img->pixels, "payload image pixels");
	BU_PUT(img, struct bsg_payload_image);
    }
    BU_PUT(pl, struct bsg_payload);
}

struct bsg_payload *
bsg_payload_image_create(size_t width, size_t height, size_t channels, const unsigned char *pixels)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_IMAGE);
    if (!pl)
	return NULL;

    struct bsg_payload_image *img;
    BU_GET(img, struct bsg_payload_image);
    memset(img, 0, sizeof(*img));
    img->width = width;
    img->height = height;
    img->channels = channels;
    if (width && height && channels && pixels) {
	size_t psize = width * height * channels;
	img->pixels = (unsigned char *)bu_malloc(psize, "payload image pixels");
	memcpy(img->pixels, pixels, psize);
    }

    pl->pl.image = img;
    pl->pl_free = _image_payload_free;
    return pl;
}

struct bsg_payload_image *
bsg_payload_image_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_IMAGE)
	return NULL;
    return payload->pl.image;
}

static void
_framebuffer_payload_free(struct bsg_payload *pl)
{
    if (!pl)
	return;
    if (pl->pl.framebuffer)
	BU_PUT(pl->pl.framebuffer, struct bsg_payload_framebuffer);
    BU_PUT(pl, struct bsg_payload);
}

struct bsg_payload *
bsg_payload_framebuffer_create(struct fb *fbp, int mode)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_FRAMEBUFFER);
    if (!pl)
	return NULL;
    struct bsg_payload_framebuffer *fbpl;
    BU_GET(fbpl, struct bsg_payload_framebuffer);
    fbpl->fbp = fbp;
    fbpl->mode = mode;
    pl->pl.framebuffer = fbpl;
    pl->pl_free = _framebuffer_payload_free;
    return pl;
}

struct bsg_payload_framebuffer *
bsg_payload_framebuffer_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_FRAMEBUFFER)
	return NULL;
    return payload->pl.framebuffer;
}


/* -----------------------------------------------------------------------
 * AXES payload (bsg_axes) — Phase D1 pilot
 * ----------------------------------------------------------------------- */

static void
_axes_payload_free(struct bsg_payload *pl)
{
    if (!pl)
	return;
    struct bsg_axes *axes = pl->pl.axes;
    if (axes)
	BU_PUT(axes, struct bsg_axes);
    BU_PUT(pl, struct bsg_payload);
}


struct bsg_payload *
bsg_payload_axes_create(struct bsg_axes *axes)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_AXES);
    if (!pl)
	return NULL;
    pl->pl.axes  = axes;
    pl->pl_free  = _axes_payload_free;
    return pl;
}


struct bsg_axes *
bsg_payload_axes_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_AXES)
	return NULL;
    return payload->pl.axes;
}


/* -----------------------------------------------------------------------
 * GRID payload — Phase D1
 * ----------------------------------------------------------------------- */

static void
_grid_payload_free(struct bsg_payload *pl)
{
    if (!pl)
	return;
    if (pl->pl.grid)
	BU_PUT(pl->pl.grid, struct bsg_grid_state);
    BU_PUT(pl, struct bsg_payload);
}

struct bsg_payload *
bsg_payload_grid_create(const struct bsg_grid_state *grid)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_GRID);
    if (!pl)
	return NULL;
    struct bsg_grid_state *g;
    BU_GET(g, struct bsg_grid_state);
    memset(g, 0, sizeof(*g));
    if (grid)
	memcpy(g, grid, sizeof(*g));
    pl->pl.grid = g;
    pl->pl_free = _grid_payload_free;
    return pl;
}

struct bsg_grid_state *
bsg_payload_grid_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_GRID)
	return NULL;
    return payload->pl.grid;
}


/* -----------------------------------------------------------------------
 * SKETCH payload — Phase D6 (drawing_modernization)
 * ----------------------------------------------------------------------- */

static void
_sketch_payload_free(struct bsg_payload *pl)
{
    if (!pl)
	return;
    struct bsg_sketch_live_data *d = pl->pl.sketch;
    if (d) {
	if (d->owns_live_ctx && d->free_cb && d->live_ctx)
	    d->free_cb(d->live_ctx);
	BU_PUT(d, struct bsg_sketch_live_data);
    }
    BU_PUT(pl, struct bsg_payload);
}

static void *
_sketch_live_ctx(struct bsg_sketch_live_data *d)
{
    if (!d)
	return NULL;
    return (d->live_ctx) ? d->live_ctx : d->rt_edit_ptr;
}

static void
_sketch_payload_update(struct bsg_payload *pl, struct bsg_view *v)
{
    if (!pl || pl->pl_type != BSG_PL_SKETCH || !pl->pl.sketch)
	return;

    struct bsg_sketch_live_data *d = pl->pl.sketch;
    void *ctx = _sketch_live_ctx(d);
    if (!ctx)
	return;

    uint64_t prev_live_rev = d->last_realized_revision;
    int updated = 0;

    if (d->update_cb)
	updated = d->update_cb(ctx, v);

    uint64_t live_rev = prev_live_rev;
    if (d->revision_cb)
	live_rev = d->revision_cb(ctx);
    else if (updated)
	live_rev = prev_live_rev + 1;

    /* If update_cb reports a change but revision_cb did not advance, force a
     * monotonic increment so payload_revision tracks realized updates. */
    if (updated && live_rev == prev_live_rev)
	live_rev++;

    if (live_rev != prev_live_rev) {
	d->last_realized_revision = live_rev;
	bsg_payload_bump_revision(pl);
    }
}

static int
_sketch_payload_bounds(struct bsg_payload *pl, point_t *bmin, point_t *bmax)
{
    return bsg_payload_sketch_bounds(pl, bmin, bmax);
}


struct bsg_payload *
bsg_payload_sketch_create(void *rt_edit_ptr, void *grid_ptr)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_SKETCH);
    if (!pl)
	return NULL;

    struct bsg_sketch_live_data *d;
    BU_GET(d, struct bsg_sketch_live_data);
    d->rt_edit_ptr = rt_edit_ptr;
    d->grid_ptr    = grid_ptr;
    d->live_ctx = rt_edit_ptr;
    d->owns_live_ctx = 0;
    d->last_realized_revision = 0;
    d->revision_cb = NULL;
    d->update_cb = NULL;
    d->bounds_cb = NULL;
    d->pick_cb = NULL;
    d->snap_cb = NULL;
    d->free_cb = NULL;

    pl->pl.sketch = d;
    pl->pl_free   = _sketch_payload_free;
    pl->pl_update = _sketch_payload_update;
    pl->pl_bounds = _sketch_payload_bounds;
    return pl;
}


struct bsg_sketch_live_data *
bsg_payload_sketch_get_data(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_SKETCH)
	return NULL;
    return payload->pl.sketch;
}

int
bsg_payload_sketch_set_live_ops(struct bsg_payload *payload,
	void *live_ctx,
	int owns_live_ctx,
	bsg_sketch_live_revision_cb_t revision_cb,
	bsg_sketch_live_update_cb_t update_cb,
	bsg_sketch_live_bounds_cb_t bounds_cb,
	bsg_sketch_live_pick_cb_t pick_cb,
	bsg_sketch_live_snap_cb_t snap_cb,
	bsg_sketch_live_free_cb_t free_cb)
{
    struct bsg_sketch_live_data *d = bsg_payload_sketch_get_data(payload);
    if (!d)
	return -1;

    d->live_ctx = live_ctx;
    d->owns_live_ctx = owns_live_ctx;
    d->revision_cb = revision_cb;
    d->update_cb = update_cb;
    d->bounds_cb = bounds_cb;
    d->pick_cb = pick_cb;
    d->snap_cb = snap_cb;
    d->free_cb = free_cb;
    d->last_realized_revision = (revision_cb) ? revision_cb(_sketch_live_ctx(d)) : 0;

    return 0;
}

uint64_t
bsg_payload_sketch_revision(struct bsg_payload *payload)
{
    struct bsg_sketch_live_data *d = bsg_payload_sketch_get_data(payload);
    if (!d)
	return 0;

    if (d->revision_cb)
	return d->revision_cb(_sketch_live_ctx(d));

    return d->last_realized_revision;
}

int
bsg_payload_sketch_realize(struct bsg_payload *payload, struct bsg_view *v)
{
    if (!payload || payload->pl_type != BSG_PL_SKETCH)
	return -1;

    uint64_t rev_before = payload->pl_revision;
    _sketch_payload_update(payload, v);

    return (payload->pl_revision != rev_before) ? 1 : 0;
}

int
bsg_payload_sketch_bounds(struct bsg_payload *payload, point_t *bmin, point_t *bmax)
{
    struct bsg_sketch_live_data *d = bsg_payload_sketch_get_data(payload);
    if (!d || !d->bounds_cb)
	return 0;
    return d->bounds_cb(_sketch_live_ctx(d), bmin, bmax);
}

int
bsg_payload_sketch_pick(struct bsg_payload *payload, struct bsg_view *v, int x, int y, void *pick_out)
{
    struct bsg_sketch_live_data *d = bsg_payload_sketch_get_data(payload);
    if (!d || !d->pick_cb)
	return 0;
    return d->pick_cb(_sketch_live_ctx(d), v, x, y, pick_out);
}

int
bsg_payload_sketch_snap(struct bsg_payload *payload, struct bsg_view *v, const point_t sample_pt, point_t out_pt)
{
    struct bsg_sketch_live_data *d = bsg_payload_sketch_get_data(payload);
    if (!d || !d->snap_cb)
	return 0;
    return d->snap_cb(_sketch_live_ctx(d), v, sample_pt, out_pt);
}


/* -----------------------------------------------------------------------
 * ANNOTATION payload — Phase D1
 * ----------------------------------------------------------------------- */

static void
_annotation_payload_free(struct bsg_payload *pl)
{
    if (!pl)
	return;
    struct bsg_payload_annotation *ann = pl->pl.annotation;
    if (ann) {
	bu_vls_free(&ann->text);
	if (ann->points)
	    bu_free(ann->points, "payload annotation points");
	BU_PUT(ann, struct bsg_payload_annotation);
    }
    BU_PUT(pl, struct bsg_payload);
}

static int
_annotation_payload_bounds(struct bsg_payload *pl, point_t *bmin, point_t *bmax)
{
    if (!pl || !pl->pl.annotation || !pl->pl.annotation->point_cnt)
	return 0;
    VSETALL((*bmin), INFINITY);
    VSETALL((*bmax), -INFINITY);
    for (size_t i = 0; i < pl->pl.annotation->point_cnt; i++) {
	VMINMAX((*bmin), (*bmax), pl->pl.annotation->points[i]);
    }
    return 1;
}

struct bsg_payload *
bsg_payload_annotation_create(const char *text, point_t *points, size_t point_cnt)
{
    struct bsg_payload *pl = bsg_payload_create(BSG_PL_ANNOTATION);
    if (!pl)
	return NULL;

    struct bsg_payload_annotation *ann;
    BU_GET(ann, struct bsg_payload_annotation);
    memset(ann, 0, sizeof(*ann));
    BU_VLS_INIT(&ann->text);
    if (text)
	bu_vls_sprintf(&ann->text, "%s", text);
    ann->point_cnt = point_cnt;
    if (point_cnt && points) {
	ann->points = (point_t *)bu_calloc(point_cnt, sizeof(point_t), "payload annotation points");
	for (size_t i = 0; i < point_cnt; i++)
	    VMOVE(ann->points[i], points[i]);
    }

    pl->pl.annotation = ann;
    pl->pl_free = _annotation_payload_free;
    pl->pl_bounds = _annotation_payload_bounds;
    return pl;
}

struct bsg_payload_annotation *
bsg_payload_annotation_get(struct bsg_payload *payload)
{
    if (!payload || payload->pl_type != BSG_PL_ANNOTATION)
	return NULL;
    return payload->pl.annotation;
}


/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
