/*                    D M - G L _ L O D . C P P
 * BRL-CAD
 *
 * Copyright (c) 1988-2026 United States Government as represented by
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
/** @file libdm/dm-gl_lod.cpp
 *
 * OpenGL logic for rendering LoD structures.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "vmath.h"
#include "bu.h"
#include "bn.h"
extern "C" {
#include "bsg/defines.h"
#include "bsg/appearance.h"
#include "bsg/lod.h"
#include "dm.h"
#include "./dm-gl.h"
#include "./include/private.h"
}

struct swrast_vars_fast {
    struct bsg_view *v;
    void *ctx;
    void *os_b;
};

static const fastf_t GL_SWRAST_PERSPECTIVE_DELTA_FACTOR = 0.0001;
static const int GL_SWRAST_GED_COORD_SCALE = 2047;

static int
gl_swrast_database_wireframe(struct dm *dmp, struct bsg_node *s)
{
    if (!dmp || !s)
	return 0;

    struct gl_vars *mvars = (struct gl_vars *)dmp->i->m_vars;
    if (!mvars || !mvars->fast_wireframe_active)
	return 0;

    if (!(s->s_type_flags & BSG_OBJ_DB))
	return 0;

    return (s->s_os->s_dmode == 0 || s->s_os->s_dmode == 3);
}

static int
gl_swrast_wireframe_obj(struct dm *dmp, struct bsg_node *s)
{
    if (!dmp || !s || !dm_get_dm_name(dmp) || !BU_STR_EQUAL(dm_get_dm_name(dmp), "swrast"))
	return 0;
    if (!(s->s_type_flags & BSG_OBJ_DB))
	return 0;
    return (s->s_os->s_dmode == 0 || s->s_os->s_dmode == 3);
}

static inline void
swrast_put_pixel_rgba(struct swrast_vars_fast *pv, int w, int h, int x, int y, const unsigned char *rgba_color)
{
    if (!pv || !pv->os_b || x < 0 || y < 0 || x >= w || y >= h)
	return;
    unsigned char *pix = ((unsigned char *)pv->os_b) + (((h - 1 - y) * w + x) * 4);
    pix[0] = rgba_color[0];
    pix[1] = rgba_color[1];
    pix[2] = rgba_color[2];
    pix[3] = 255;
}

static void
swrast_draw_line_rgba(struct swrast_vars_fast *pv, int w, int h, int x0, int y0, int x1, int y1, const unsigned char *rgba_color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int e2;

    for (;;) {
	swrast_put_pixel_rgba(pv, w, h, x0, y0, rgba_color);
	if (x0 == x1 && y0 == y1)
	    break;
	e2 = 2 * err;
	if (e2 >= dy) {
	    err += dy;
	    x0 += sx;
	}
	if (e2 <= dx) {
	    err += dx;
	    y0 += sy;
	}
    }
}

static inline int
nonzero_fallback_one(int d)
{
    return d ? d : 1;
}

/* Clip a screen-space line to the current swrast buffer using
 * Cohen-Sutherland outcodes.  Returns 1 when any portion of the line is
 * visible and updates the endpoints in-place; returns 0 when fully rejected. */
static int
clip_line_to_win(int *x0, int *y0, int *x1, int *y1, int w, int h)
{
    enum { LEFT = 1, RIGHT = 2, BOTTOM = 4, TOP = 8 };

    auto compute_outcode = [w, h](int x, int y) {
	int c = 0;
	if (x < 0) c |= LEFT;
	else if (x >= w) c |= RIGHT;
	if (y < 0) c |= BOTTOM;
	else if (y >= h) c |= TOP;
	return c;
    };

    int c0 = compute_outcode(*x0, *y0);
    int c1 = compute_outcode(*x1, *y1);
    while (true) {
	if (!(c0 | c1)) return 1;
	if (c0 & c1) return 0;

	int c = c0 ? c0 : c1;
	int x;
	int y;
	if (c & TOP) {
	    int y_denominator = nonzero_fallback_one(*y1 - *y0);
	    y = h - 1;
	    x = *x0 + (*x1 - *x0) * (y - *y0) / y_denominator;
	} else if (c & BOTTOM) {
	    int y_denominator = nonzero_fallback_one(*y1 - *y0);
	    y = 0;
	    x = *x0 + (*x1 - *x0) * (y - *y0) / y_denominator;
	} else if (c & RIGHT) {
	    int x_denominator = nonzero_fallback_one(*x1 - *x0);
	    x = w - 1;
	    y = *y0 + (*y1 - *y0) * (x - *x0) / x_denominator;
	} else {
	    int x_denominator = nonzero_fallback_one(*x1 - *x0);
	    x = 0;
	    y = *y0 + (*y1 - *y0) * (x - *x0) / x_denominator;
	}

	if (c == c0) {
	    *x0 = x;
	    *y0 = y;
	    c0 = compute_outcode(*x0, *y0);
	} else {
	    *x1 = x;
	    *y1 = y;
	    c1 = compute_outcode(*x1, *y1);
	}
    }
}

/* Fast swrast wireframe rendering path for database-object vlists.  It draws
 * transformed line segments directly into the OSMesa RGBA buffer and bypasses
 * the OpenGL vlist/display-list path; callers fall back to dm_draw_vlist when
 * this routine cannot use the swrast private buffer. */
static int
swrast_draw_vlist_fast(struct dm *dmp, bsg_vlist *vp)
{
    if (!dmp || !vp)
	return BRLCAD_ERROR;

    struct swrast_vars_fast *pv = (struct swrast_vars_fast *)dmp->i->dm_vars.priv_vars;
    if (!pv || !pv->os_b || !pv->v)
	return BRLCAD_ERROR;

    int w = dmp->i->dm_width;
    int h = dmp->i->dm_height;
    if (w <= 0 || h <= 0)
	return BRLCAD_ERROR;

    fastf_t *xmat = pv->v->gv_model2view;
    point_t lpnt, pnt;
    int have_lpnt = 0;
    point_t *pt_prev = NULL;
    fastf_t dist_prev = 1.0;
    fastf_t delta = xmat[15] * GL_SWRAST_PERSPECTIVE_DELTA_FACTOR;
    if (delta < 0.0)
	delta = -delta;
    if (delta < SQRT_SMALL_FASTF)
	delta = SQRT_SMALL_FASTF;

    const unsigned char *fg = dmp->i->dm_fg;
    bsg_vlist *tvp;
    for (BU_LIST_FOR(tvp, bsg_vlist, &vp->l)) {
	int *cmd = tvp->cmd;
	point_t *pt = tvp->pt;
	for (size_t i = 0; i < tvp->nused; i++, cmd++, pt++) {
	    switch (*cmd) {
		case BSG_VLIST_MODEL_MAT:
		    xmat = pv->v->gv_model2view;
		    continue;
		case BSG_VLIST_DISPLAY_MAT:
		    xmat = pv->v->gv_model2view;
		    continue;
		case BSG_VLIST_POLY_START:
		case BSG_VLIST_POLY_VERTNORM:
		case BSG_VLIST_TRI_START:
		case BSG_VLIST_TRI_VERTNORM:
		    continue;
		case BSG_VLIST_POLY_MOVE:
		case BSG_VLIST_LINE_MOVE:
		case BSG_VLIST_TRI_MOVE: {
		    if (dmp->i->dm_perspective > 0) {
			fastf_t dist = VDOT(*pt, &xmat[12]) + xmat[15];
			if (dist <= 0.0) {
			    pt_prev = pt;
			    dist_prev = dist;
			    continue;
			}
			dist_prev = dist;
			pt_prev = pt;
		    }
		    MAT4X3PNT(lpnt, xmat, *pt);
		    lpnt[0] *= GL_SWRAST_GED_COORD_SCALE;
		    lpnt[1] *= GL_SWRAST_GED_COORD_SCALE * dmp->i->dm_aspect;
		    have_lpnt = 1;
		    continue;
		}
		case BSG_VLIST_POLY_DRAW:
		case BSG_VLIST_POLY_END:
		case BSG_VLIST_LINE_DRAW:
		case BSG_VLIST_TRI_DRAW:
		case BSG_VLIST_TRI_END: {
		    if (!have_lpnt)
			continue;
		    if (dmp->i->dm_perspective > 0) {
			fastf_t dist = VDOT(*pt, &xmat[12]) + xmat[15];
			if (dist <= 0.0 && dist_prev <= 0.0) {
			    dist_prev = dist;
			    pt_prev = pt;
			    continue;
			}
			if (dist <= 0.0 && pt_prev) {
			    vect_t diff;
			    point_t tmp_pt;
			    fastf_t alpha = (dist_prev - delta) / (dist_prev - dist);
			    VSUB2(diff, *pt, *pt_prev);
			    VJOIN1(tmp_pt, *pt_prev, alpha, diff);
			    MAT4X3PNT(pnt, xmat, tmp_pt);
			} else {
			    MAT4X3PNT(pnt, xmat, *pt);
			}
			dist_prev = dist;
			pt_prev = pt;
		    } else {
			MAT4X3PNT(pnt, xmat, *pt);
		    }
		    pnt[0] *= GL_SWRAST_GED_COORD_SCALE;
		    pnt[1] *= GL_SWRAST_GED_COORD_SCALE * dmp->i->dm_aspect;

		    int x0 = GED_TO_Xx(dmp, lpnt[0]);
		    int y0 = GED_TO_Xy(dmp, lpnt[1]);
		    int x1 = GED_TO_Xx(dmp, pnt[0]);
		    int y1 = GED_TO_Xy(dmp, pnt[1]);
		    if (clip_line_to_win(&x0, &y0, &x1, &y1, w, h)) {
			swrast_draw_line_rgba(pv, w, h, x0, y0, x1, y1, fg);
		    }
		    VMOVE(lpnt, pnt);
		    continue;
		}
		default:
		    continue;
	    }
	}
    }

    return BRLCAD_OK;
}

/* ---------------------------------------------------------------------
 * Phase 13 (drawing_stack_modernization): GL-backend per-shape state.
 *
 * The GL family of display managers (dm-gl, dm-qtgl, dm-glx, dm-wgl,
 * dm-swrast) caches its per-shape OpenGL display list and the mode it was
 * compiled in here, attached to the generic bsg_node::s_backend slot.
 * This replaces the BV_DEPRECATED s_dlist / s_dlist_mode / s_dlist_stale
 * fields that previously lived on every scene object.
 *
 * Lifecycle:
 *   - allocated lazily by gl_backend_handle_get(s, true) the first time the
 *     backend caches a list for the shape;
 *   - the dm_backend_ops invalidate callback (gl_backend_invalidate_obj)
 *     simply flips dlist_stale on the existing handle if any;
 *   - the dm_backend_ops release callback (gl_backend_release_obj) is
 *     fired through bsg_scene_obj_release_backend (from bsg_obj_reset,
 *     bsg_obj_put, the libbsg tree free paths, ...) and tears down the GL
 *     list and the handle itself.
 */
struct gl_backend_handle {
    unsigned int dlist;     /* compiled GL display list index, 0 if none */
    int dlist_mode;         /* mode the list was compiled in (s_os->s_dmode) */
    int dlist_stale;        /* set by invalidate_obj; next draw regenerates */
};

static void gl_backend_release_obj(struct dm *dmp, struct bsg_node *s);
static void gl_backend_release_obj_free(struct bsg_node *s);
static void gl_backend_invalidate_obj_free(struct bsg_node *s);

/* Fetch (or lazily allocate) the GL backend handle for shape s.
 * Returns NULL if no handle exists and create==false, or if s is NULL. */
static struct gl_backend_handle *
gl_backend_handle_get(struct bsg_node *s, bool create)
{
    if (!s)
	return NULL;
    if (s->s_backend) {
	if (s->s_backend->type_tag != BV_BACKEND_GL)
	    return NULL;
	return (struct gl_backend_handle *)s->s_backend->handle;
    }
    if (!create)
	return NULL;

    struct bsg_backend *be;
    BU_GET(be, struct bsg_backend);
    be->type_tag = BV_BACKEND_GL;
    be->free = gl_backend_release_obj_free;
    be->invalidate = gl_backend_invalidate_obj_free;

    struct gl_backend_handle *h;
    BU_GET(h, struct gl_backend_handle);
    h->dlist = 0;
    h->dlist_mode = 0;
    h->dlist_stale = 0;
    be->handle = h;
    s->s_backend = be;
    return h;
}

/* Release any GL display list held by shape s and free the backing
 * gl_backend_handle / bsg_backend descriptor.  Recurses into children
 * for group-style scene objects, matching the legacy dlist_free_callback
 * walk.  Safe to call when no backend handle is attached. */
static void
gl_backend_handle_release(struct bsg_node *s, int enqueue_delete)
{
    if (!s)
	return;
    /* Do not recurse into children here.  Backend release is triggered per
     * object by higher-level scene teardown paths (e.g. bsg_obj_put on each
     * leaf).  Recursing from a parent can double-release child backend state,
     * leading to stale GL list IDs reaching glDeleteLists. */
    if (s->s_backend && s->s_backend->type_tag == BV_BACKEND_GL) {
	struct gl_backend_handle *h = (struct gl_backend_handle *)s->s_backend->handle;
	if (h) {
	    if (enqueue_delete && h->dlist && s->s_v && s->s_v->dmp)
		gl_dlist_delete_enqueue((struct dm *)s->s_v->dmp, h->dlist);
	    h->dlist = 0;
	    BU_PUT(h, struct gl_backend_handle);
	    s->s_backend->handle = NULL;
	}
	BU_PUT(s->s_backend, struct bsg_backend);
	s->s_backend = NULL;
    }
}

/* dm_backend_ops::release_obj — tear down GL state for this shape. */
static void
gl_backend_release_obj(struct dm *dmp, struct bsg_node *s)
{
    (void)dmp;
    gl_backend_handle_release(s, 1);
}

/* bsg_backend::free — also fired indirectly by
 * bsg_scene_obj_release_backend; same semantics as the dm-side wrapper. */
static void
gl_backend_release_obj_free(struct bsg_node *s)
{
	/* Called from generic scene-teardown paths where owning dm/m_vars may
	 * already be partially torn down.  Avoid touching dm-owned delete queues.
	 */
	gl_backend_handle_release(s, 0);
}

extern "C" int gl_draw_obj(struct dm *dmp, struct bsg_node *s);

/* dm_backend_ops::invalidate_obj — mark the cached GL list stale. */
static void
gl_backend_invalidate_obj(struct dm *dmp, struct bsg_node *s)
{
    (void)dmp;
    struct gl_backend_handle *h = gl_backend_handle_get(s, false);
    if (h)
	h->dlist_stale = 1;
}

/* bsg_backend::invalidate — same semantics as the dm-side wrapper. */
static void
gl_backend_invalidate_obj_free(struct bsg_node *s)
{
    struct gl_backend_handle *h = gl_backend_handle_get(s, false);
    if (h)
	h->dlist_stale = 1;
}

extern "C" const struct dm_backend_ops gl_backend_ops = {
    BV_BACKEND_GL,
    gl_draw_obj,
    gl_backend_invalidate_obj,
    gl_backend_release_obj,
};


// TODO - We can't currently use display lists for really large meshes, as we
// won't be able to hold both the original data and the compiled display list
// in memory at the same time.  For that scenario, we would first need to break
// down the big mesh into smaller pieces as in the earlier LoD experiments in
// order to keep using display lists...
static int
gl_draw_tri(struct dm *dmp, struct bsg_mesh_lod *lod)
{
    int fcnt = lod->fcnt;
    int pcnt = lod->pcnt;
    const int *faces = lod->faces;
    const point_t *points = lod->points;
    const point_t *points_orig = lod->points_orig;
    const vect_t *normals = lod->normals;
    struct bsg_node *s = lod->s;
    int mode = s->s_os->s_dmode;
    mat_t save_mat, draw_mat;

    struct gl_vars *mvars = (struct gl_vars *)dmp->i->m_vars;
    GLdouble dpt[3];
    static float black[4] = {0.0, 0.0, 0.0, 0.0};
    GLfloat originalLineWidth;

    if (mode < 0 || mode > 1)
	return BRLCAD_ERROR;

    glGetFloatv(GL_LINE_WIDTH, &originalLineWidth);

    gl_debug_print(dmp, "gl_draw_tri", dmp->i->dm_debugLevel);

    struct gl_backend_handle *h = gl_backend_handle_get(s, false);

    // If the dlist is stale, clear it
    if (h && h->dlist_stale) {
	if (h->dlist) {
	    glDeleteLists(h->dlist, 1);
	    h->dlist = 0;
	}
	h->dlist_stale = 0;

	if (!pcnt || !fcnt) {
	    // If we've had a memshrink, the loaded data isn't
	    // going to be correct to generate new draw info.
	    // First, find out the current level:
	    int curr_level = bsg_mesh_lod_level(s, -1, 0);

	    // Trigger a load operation to restore it
	    bsg_mesh_lod_level(s, curr_level, 1);

	    fcnt = lod->fcnt;
	    pcnt = lod->pcnt;
	    faces = lod->faces;
	    points = lod->points;
	    points_orig = lod->points_orig;
	    normals = lod->normals;
	}
    }

    // We don't want color to be part of the dlist, to allow the app
    // to change it without regeneration - hence, we need to do it
    // up front
    if (mode == 0) {
	if (bsg_appearance_is_highlighted(s)) {
	    dm_set_fg(dmp, 255, 255, 255, 0, s->s_os->transparency);
	}
	if (mvars->lighting_on) {
	    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mvars->i.wireColor);
	    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, black);
	    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, black);
	    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, black);
	    if (mvars->transparency_on)
		glDisable(GL_BLEND);
	}
    } else {
	if (bsg_appearance_is_highlighted(s)) {
	    dm_set_fg(dmp, 255, 255, 255, 0, s->s_os->transparency);
	}
	if (mvars->lighting_on) {
	    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, black);
	    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mvars->i.ambientColor);
	    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mvars->i.specularColor);
	    glMaterialfv(GL_FRONT, GL_DIFFUSE, mvars->i.diffuseColor);
	    switch (mvars->lighting_on) {
		case 1:
		    break;
		case 2:
		    glMaterialfv(GL_BACK, GL_DIFFUSE, mvars->i.diffuseColor);
		    break;
		case 3:
		    glMaterialfv(GL_BACK, GL_DIFFUSE, mvars->i.backDiffuseColorDark);
		    break;
		default:
		    glMaterialfv(GL_BACK, GL_DIFFUSE, mvars->i.backDiffuseColorLight);
		    break;
	    }
	}
	if (mvars->lighting_on) {
	    if (mvars->transparency_on) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	    }
	}
    }

    // If we have a dlist in the correct mode, use it
    if (h && h->dlist) {
	if (mode == h->dlist_mode) {
	    //bu_log("use dlist %d\n", h->dlist);
	    MAT_COPY(save_mat, s->s_v->gv_model2view);
	    bn_mat_mul(draw_mat, s->s_v->gv_model2view, s->s_mat);
	    dm_loadmatrix(dmp, draw_mat, 0);
	    glCallList(h->dlist);
	    dm_loadmatrix(dmp, save_mat, 0);
	    glLineWidth(originalLineWidth);
	    if (mvars->transparency_on)
		glDisable(GL_BLEND);
	    return BRLCAD_OK;
	} else {
	    // Display list mode is incorrect (wireframe when we
	    // want shaded, or vice versa.)
	    glDeleteLists(h->dlist, 1);
	    h->dlist = 0;
	}
    }

    // Figure out if we need a new dlist (and if so whether this triangle set
    // is a candidate.)  OpenGL Display Lists are faster than immediate
    // triangle drawing when we can use them, but they require more memory
    // usage while they are being generated.  If we're tight on memory and the
    // triangle set is large, accept the slower drawing to avoid memory stress
    // - otherwise, we want the list
    ssize_t avail_mem = 0.5*bu_mem(BU_MEM_AVAIL, NULL);
    size_t size_est = (size_t)(fcnt*3*sizeof(point_t));
    bool gen_dlist = false;
    if ((!h || !h->dlist) && avail_mem > 0 && size_est < (size_t)avail_mem) {
	gen_dlist = true;
	if (!h)
	    h = gl_backend_handle_get(s, true);
	h->dlist = glGenLists(1);
	h->dlist_mode = mode;
	//bu_log("gen_dlist: %d\n", h->dlist);
	glNewList(h->dlist, GL_COMPILE);
    } else {
	bu_log("Not using dlist\n");
	// Straight-up drawing - set up the matrix
	MAT_COPY(save_mat, s->s_v->gv_model2view);
	bn_mat_mul(draw_mat, s->s_v->gv_model2view, s->s_mat);
	dm_loadmatrix(dmp, draw_mat, 0);
    }

    // Wireframe
    if (mode == 0) {
	// Draw all the triangles in faces array
	for (int i = 0; i < fcnt; i++) {

	    bool bad_face = false;
	    for (int j = 0; j < 3; j++) {
		int f_ind = faces[3*i+j];
		if (f_ind >= pcnt || f_ind < 0) {
		    bu_log("bad face %d - skipping\n", i);
		    bad_face = true;
		    break;
		}
	    }
	    if (bad_face)
		continue;

	    glBegin(GL_LINE_STRIP);
	    VMOVE(dpt, points[faces[3*i+0]]);
	    glVertex3dv(dpt);
	    VMOVE(dpt, points[faces[3*i+1]]);
	    glVertex3dv(dpt);
	    VMOVE(dpt, points[faces[3*i+2]]);
	    glVertex3dv(dpt);
	    VMOVE(dpt, points[faces[3*i+0]]);
	    glVertex3dv(dpt);
	    glEnd();
	}
	if (gen_dlist) {
	    glEndList();
	    h->dlist_stale = 0;
	    if (size_est > (avail_mem * 0.01)) {
		// If the original data is sizable, clear it to save system memory.
		// The dlist has what it needs, and the LoD code will re-load info
		// as needed for updates.
		bsg_mesh_lod_memshrink(s);
	    }

	    MAT_COPY(save_mat, s->s_v->gv_model2view);
	    bn_mat_mul(draw_mat, s->s_v->gv_model2view, s->s_mat);
	    dm_loadmatrix(dmp, draw_mat, 0);
	    glCallList(h->dlist);
	    dm_loadmatrix(dmp, save_mat, 0);
	}

	glLineWidth(originalLineWidth);

	if (mvars->transparency_on)
	    glDisable(GL_BLEND);

	// Without dlist, we had to set the matrix - restore
	if (!h || !h->dlist)
	    dm_loadmatrix(dmp, save_mat, 0);

	return BRLCAD_OK;
    }

    // Shaded
    if (mode == 1) {

	// For LoD drawing, we need to use two sided shading - thin objects or
	// very low detail triangle objects will sometimes draw multiple
	// triangles in the same position, which can result in the wrong "side"
	// being visible from some views.
	GLint two_sided;
	glGetIntegerv(GL_LIGHT_MODEL_TWO_SIDE, &two_sided);
	glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);

	glBegin(GL_TRIANGLES);

	// Draw all the triangles in faces array
	for (int i = 0; i < fcnt; i++) {

	    bool bad_face = false;
	    for (int j = 0; j < 3; j++) {
		int f_ind = faces[3*i+j];
		if (f_ind >= pcnt || f_ind < 0) {
		    bu_log("bad face %d - skipping\n", i);
		    bad_face = true;
		    break;
		}
	    }
	    if (bad_face)
		continue;

	    // Set surface normal
	    vect_t ab, ac, norm;
	    VSUB2(ab, points_orig[faces[3*i+0]], points_orig[faces[3*i+1]]);
	    VSUB2(ac, points_orig[faces[3*i+0]], points_orig[faces[3*i+2]]);
	    VCROSS(norm, ab, ac);
	    VUNITIZE(norm);

	    if (normals) {
		vect_t vnorm;
		VMOVE(vnorm, normals[3*i+0]);
		if (((int)(vnorm[0]*10+vnorm[1]*10+vnorm[2]*10)) != 0) {
		    glNormal3dv(vnorm);
		} else {
		    glNormal3dv(norm);
		}
		VMOVE(dpt, points[faces[3*i+0]]);
		glVertex3dv(dpt);

		VMOVE(vnorm, normals[3*i+1]);
		if (((int)(vnorm[0]*10+vnorm[1]*10+vnorm[2]*10)) != 0) {
		    glNormal3dv(vnorm);
		} else {
		    glNormal3dv(norm);
		}
		VMOVE(dpt, points[faces[3*i+1]]);
		glVertex3dv(dpt);

		VMOVE(vnorm, normals[3*i+2]);
		if (((int)(vnorm[0]*10+vnorm[1]*10+vnorm[2]*10)) != 0) {
		    glNormal3dv(vnorm);
		} else {
		    glNormal3dv(norm);
		}
		VMOVE(dpt, points[faces[3*i+2]]);
		glVertex3dv(dpt);

	    } else {
		glNormal3dv(norm);
		VMOVE(dpt, points[faces[3*i+0]]);
		glVertex3dv(dpt);
		VMOVE(dpt, points[faces[3*i+1]]);
		glVertex3dv(dpt);
		VMOVE(dpt, points[faces[3*i+2]]);
		glVertex3dv(dpt);
	    }

	}

	glEnd();

	if (gen_dlist) {
	    glEndList();
	    h->dlist_stale = 0;
	    if (size_est > (avail_mem * 0.01)) {
		// If the original data is sizable, clear it to save system memory.
		// The dlist has what it needs, and the LoD code will re-load info
		// as needed for updates.
		bsg_mesh_lod_memshrink(s);
	    }

	    /* notify registered sensors that the dlist was regenerated */
	    dm_fire_dlist_sensors(dmp);

	    MAT_COPY(save_mat, s->s_v->gv_model2view);
	    bn_mat_mul(draw_mat, s->s_v->gv_model2view, s->s_mat);
	    dm_loadmatrix(dmp, draw_mat, 0);
	    glCallList(h->dlist);
	    dm_loadmatrix(dmp, save_mat, 0);
	}

	if (mvars->lighting_on && mvars->transparency_on)
	    glDisable(GL_BLEND);

	// Put the lighting model back where it was prior to this operation
	glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, two_sided);

	glLineWidth(originalLineWidth);

	// If we're not using a pre-baked dlist, restore matrix
	if (!h || !h->dlist)
	    dm_loadmatrix(dmp, save_mat, 0);

	return BRLCAD_OK;
    }

    return BRLCAD_ERROR;
}

extern "C"
int gl_draw_obj(struct dm *dmp, struct bsg_node *s)
{
    GLint originalShadeModel = 0;
    int restoreShadeModel = 0;
    GLboolean lightingWasEnabled = GL_FALSE;
    int restoreLighting = 0;

    gl_dlist_delete_flush(dmp);

    if (s->mesh_obj && s->draw_data) {
	struct bsg_mesh_lod *lod = (struct bsg_mesh_lod *)s->draw_data;
	return gl_draw_tri(dmp, lod);
    }

    // "Standard" vlist object drawing
    if (bu_list_len(&s->s_vlist)) {
	if (gl_swrast_wireframe_obj(dmp, s)) {
	    /* Swrast wireframes should render as flat, unlit lines whether the
	     * fast path or the fallback GL path draws them. */
	    lightingWasEnabled = glIsEnabled(GL_LIGHTING);
	    if (lightingWasEnabled) {
		unsigned char *fg = dm_get_fg(dmp);
		glDisable(GL_LIGHTING);
		glColor3ub((GLubyte)fg[0], (GLubyte)fg[1], (GLubyte)fg[2]);
		restoreLighting = 1;
	    }
	    if (gl_swrast_database_wireframe(dmp, s)) {
		int fast_ret = swrast_draw_vlist_fast(dmp, (bsg_vlist *)&s->s_vlist);
		if (restoreLighting) {
		    glEnable(GL_LIGHTING);
		    restoreLighting = 0;
		}
		if (fast_ret == BRLCAD_OK) {
		    return BRLCAD_OK;
		}
	    }
	    glGetIntegerv(GL_SHADE_MODEL, &originalShadeModel);
	    if (originalShadeModel != GL_FLAT) {
		glShadeModel(GL_FLAT);
		restoreShadeModel = 1;
	    }
	}
	if (s->s_os->s_dmode == 4) {
	    /* Hidden-line mode always uses the explicit vlist path so the
	     * line/edge drawing logic in dm_draw_vlist_hidden_line runs. */
	    dm_draw_vlist_hidden_line(dmp, (bsg_vlist *)&s->s_vlist);
	} else if (dm_get_displaylist(dmp)) {
	    /* Phase 13 (drawing_stack_modernization): the GL backend owns the
	     * per-shape display-list lifecycle for ordinary vlist objects
	     * (matching the gl_draw_tri pattern for mesh LoD).  When the dm
	     * advertises display-list support, lazily compile the vlist into
	     * a named GL display list on first draw, then replay it via
	     * dm_draw_dlist on subsequent frames.  Color is intentionally NOT
	     * baked into the list — the BSG render contract sets the current
	     * GL colour via dm_set_fg before each call to dm_backend_draw_obj,
	     * so changes to s_color (e.g. via the `color` / `mater` commands)
	     * take effect immediately without dlist invalidation.  Geometry
	     * mutations route through dm_backend_ops::invalidate_obj which
	     * flips dlist_stale on the gl_backend_handle; the next draw
	     * regenerates the list.  When available memory is too tight to
	     * safely buffer the recording, fall back to immediate-mode
	     * dm_draw_vlist.  This replaces the legacy MGED/libtclcad eager
	     * pre-generation paths (createDListSolid /
	     * to_create_vlist_callback_solid). */
	    struct gl_backend_handle *h = gl_backend_handle_get(s, false);
	    if (h && h->dlist != 0 && h->dlist_stale) {
		glDeleteLists(h->dlist, 1);
		h->dlist = 0;
		h->dlist_stale = 0;
	    }
	    if (!h || h->dlist == 0) {
		size_t size_est = 0;
		bsg_vlist *vp;
		for (BU_LIST_FOR(vp, bsg_vlist, &s->s_vlist)) {
		    size_est += vp->nused * sizeof(point_t);
		}
		ssize_t avail_mem = 0.5 * bu_mem(BU_MEM_AVAIL, NULL);
		if (avail_mem > 0 && size_est < (size_t)avail_mem) {
		    if (!h)
			h = gl_backend_handle_get(s, true);
		    h->dlist = glGenLists(1);
		    if (h->dlist != 0) {
			glNewList(h->dlist, GL_COMPILE);
			dm_draw_vlist(dmp, (bsg_vlist *)&s->s_vlist);
			glEndList();
		    }
		}
	    }
	    if (h && h->dlist != 0) {
		dm_draw_dlist(dmp, h->dlist);
	    } else {
		/* Memory was tight or list allocation failed; fall back to
		 * immediate-mode drawing so the object still renders. */
		dm_draw_vlist(dmp, (bsg_vlist *)&s->s_vlist);
	    }
	} else {
	    dm_draw_vlist(dmp, (bsg_vlist *)&s->s_vlist);
	}
	if (restoreShadeModel)
	    glShadeModel((GLenum)originalShadeModel);
	if (restoreLighting)
	    glEnable(GL_LIGHTING);
	return BRLCAD_OK;
    }

    return BRLCAD_ERROR;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
