/*                      E D S K E T C H . C
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file primitives/sketch/edsketch.c
 *
 * Editing support for the 2-D sketch primitive (ID_SKETCH).
 *
 * Vertex/segment indices are carried in e_para[0] (and e_para[1] for
 * the second index of a line segment).  UV coordinates and deltas are
 * carried in e_para[0] and e_para[1].  See the ECMD descriptions below.
 */

#include "common.h"

#include <math.h>
#include <string.h>

#include "vmath.h"
#include "bu/malloc.h"
#include "raytrace.h"
#include "rt/geom.h"
#include "rt/edit.h"
#include "rt/primitives/sketch.h"
#include "wdb.h"

#include "../edit_private.h"

/*
 * ECMD numbers for sketch editing.  ID_SKETCH = 26, so we use 26nnn.
 */

/** Select a vertex by index (e_para[0] = vertex index). */
#define ECMD_SKETCH_PICK_VERTEX    26001
/** Move the currently selected vertex (e_para[0..1] = new UV coords in mm). */
#define ECMD_SKETCH_MOVE_VERTEX    26002
/** Select a curve segment by index (e_para[0] = segment index). */
#define ECMD_SKETCH_PICK_SEGMENT   26003
/** Translate the currently selected segment (e_para[0..1] = UV delta in mm). */
#define ECMD_SKETCH_MOVE_SEGMENT   26004
/** Append a line segment (e_para[0] = start vert index, e_para[1] = end vert index). */
#define ECMD_SKETCH_APPEND_LINE    26005
/**
 * Append a circular arc segment.
 * e_para[0] = start vert index
 * e_para[1] = end   vert index
 * e_para[2] = radius (mm; negative → full circle, "end" is centre)
 * e_para[3] = center_is_left flag (non-zero = center to left of start→end)
 * e_para[4] = orientation flag (0 = ccw, non-zero = cw)
 * e_inpara must be 5.  Uses RT_EDIT_MAXPARA parameter slots.
 */
#define ECMD_SKETCH_APPEND_ARC     26006
/**
 * Append a Bezier curve segment.
 * e_para[0..e_inpara-1] are vertex indices of the (e_inpara) control points.
 * degree = e_inpara - 1.  Requires e_inpara >= 2.
 * Up to RT_EDIT_MAXPARA control points are supported (degree 0..RT_EDIT_MAXPARA-1).
 */
#define ECMD_SKETCH_APPEND_BEZIER  26007
/** Delete the currently selected vertex (only if not used by any segment). */
#define ECMD_SKETCH_DELETE_VERTEX  26008
/** Delete the currently selected curve segment. */
#define ECMD_SKETCH_DELETE_SEGMENT 26009


/* ------------------------------------------------------------------ */
/* ipe_ptr lifecycle                                                   */
/* ------------------------------------------------------------------ */

void *
rt_edit_sketch_prim_edit_create(struct rt_edit *UNUSED(s))
{
    struct rt_sketch_edit *se;
    BU_GET(se, struct rt_sketch_edit);
    se->curr_vert = -1;
    se->curr_seg  = -1;
    return (void *)se;
}

void
rt_edit_sketch_prim_edit_destroy(struct rt_sketch_edit *se)
{
    if (!se)
	return;
    BU_PUT(se, struct rt_sketch_edit);
}

void
rt_edit_sketch_prim_edit_reset(struct rt_edit *s)
{
    struct rt_sketch_edit *se = (struct rt_sketch_edit *)s->ipe_ptr;
    if (!se)
	return;
    se->curr_vert = -1;
    se->curr_seg  = -1;
}


/* ------------------------------------------------------------------ */
/* set_edit_mode                                                       */
/* ------------------------------------------------------------------ */

void
rt_edit_sketch_set_edit_mode(struct rt_edit *s, int mode)
{
    rt_edit_set_edflag(s, mode);

    switch (mode) {
	case ECMD_SKETCH_MOVE_VERTEX:
	case ECMD_SKETCH_MOVE_SEGMENT:
	    s->edit_mode = RT_PARAMS_EDIT_TRANS;
	    break;
	case ECMD_SKETCH_PICK_VERTEX:
	case ECMD_SKETCH_PICK_SEGMENT:
	    s->edit_mode = RT_PARAMS_EDIT_PICK;
	    break;
	default:
	    break;
    }

    rt_edit_process(s);
}

static void
sketch_ed(struct rt_edit *s, int arg, int UNUSED(a), int UNUSED(b), void *UNUSED(data))
{
    rt_edit_sketch_set_edit_mode(s, arg);
}


/* ------------------------------------------------------------------ */
/* Menu                                                                */
/* ------------------------------------------------------------------ */

struct rt_edit_menu_item sketch_menu[] = {
    { "SKETCH MENU",         NULL,      0 },
    { "Pick Vertex",         sketch_ed, ECMD_SKETCH_PICK_VERTEX },
    { "Move Vertex",         sketch_ed, ECMD_SKETCH_MOVE_VERTEX },
    { "Pick Segment",        sketch_ed, ECMD_SKETCH_PICK_SEGMENT },
    { "Move Segment",        sketch_ed, ECMD_SKETCH_MOVE_SEGMENT },
    { "Append Line",         sketch_ed, ECMD_SKETCH_APPEND_LINE },
    { "Append Arc",          sketch_ed, ECMD_SKETCH_APPEND_ARC },
    { "Append Bezier",       sketch_ed, ECMD_SKETCH_APPEND_BEZIER },
    { "Delete Vertex",       sketch_ed, ECMD_SKETCH_DELETE_VERTEX },
    { "Delete Segment",      sketch_ed, ECMD_SKETCH_DELETE_SEGMENT },
    { "", NULL, 0 }
};

struct rt_edit_menu_item *
rt_edit_sketch_menu_item(const struct bn_tol *UNUSED(tol))
{
    return sketch_menu;
}


/* ------------------------------------------------------------------ */
/* Helper: check if a vertex is referenced by any segment             */
/* ------------------------------------------------------------------ */

static int
sketch_vert_is_used(const struct rt_sketch_internal *skt, int vi)
{
    size_t i;
    for (i = 0; i < skt->curve.count; i++) {
	void *seg = skt->curve.segment[i];
	if (!seg)
	    continue;
	uint32_t magic = *(uint32_t *)seg;
	if (magic == CURVE_LSEG_MAGIC) {
	    struct line_seg *ls = (struct line_seg *)seg;
	    if (ls->start == vi || ls->end == vi)
		return 1;
	} else if (magic == CURVE_CARC_MAGIC) {
	    struct carc_seg *cs = (struct carc_seg *)seg;
	    if (cs->start == vi || cs->end == vi)
		return 1;
	} else if (magic == CURVE_BEZIER_MAGIC) {
	    struct bezier_seg *bs = (struct bezier_seg *)seg;
	    int j;
	    for (j = 0; j <= bs->degree; j++) {
		if (bs->ctl_points[j] == vi)
		    return 1;
	    }
	} else if (magic == CURVE_NURB_MAGIC) {
	    struct nurb_seg *ns = (struct nurb_seg *)seg;
	    int j;
	    for (j = 0; j < ns->c_size; j++) {
		if (ns->ctl_points[j] == vi)
		    return 1;
	    }
	}
    }
    return 0;
}


/* ------------------------------------------------------------------ */
/* Edit operation implementations                                      */
/* ------------------------------------------------------------------ */

static int
ecmd_sketch_pick_vertex(struct rt_edit *s)
{
    struct rt_sketch_edit *se = (struct rt_sketch_edit *)s->ipe_ptr;
    struct rt_sketch_internal *skt =
	(struct rt_sketch_internal *)s->es_int.idb_ptr;
    RT_SKETCH_CK_MAGIC(skt);

    if (!s->e_inpara) {
	bu_vls_printf(s->log_str, "ERROR: vertex index required\n");
	return BRLCAD_ERROR;
    }

    int vi = (int)s->e_para[0];
    if (vi < 0 || (size_t)vi >= skt->vert_count) {
	bu_vls_printf(s->log_str,
		      "ERROR: vertex index %d out of range [0, %zu)\n",
		      vi, skt->vert_count);
	s->e_inpara = 0;
	return BRLCAD_ERROR;
    }

    se->curr_vert = vi;
    s->e_inpara = 0;
    return 0;
}

static int
ecmd_sketch_move_vertex(struct rt_edit *s)
{
    struct rt_sketch_edit *se = (struct rt_sketch_edit *)s->ipe_ptr;
    struct rt_sketch_internal *skt =
	(struct rt_sketch_internal *)s->es_int.idb_ptr;
    RT_SKETCH_CK_MAGIC(skt);

    if (se->curr_vert < 0) {
	bu_vls_printf(s->log_str, "ERROR: no vertex selected (use ECMD_SKETCH_PICK_VERTEX first)\n");
	return BRLCAD_ERROR;
    }
    if (!s->e_inpara || s->e_inpara < 2) {
	bu_vls_printf(s->log_str, "ERROR: two parameters required (U V in mm)\n");
	s->e_inpara = 0;
	return BRLCAD_ERROR;
    }

    skt->verts[se->curr_vert][0] = s->e_para[0] * s->local2base;
    skt->verts[se->curr_vert][1] = s->e_para[1] * s->local2base;
    rt_edit_snap_point(skt->verts[se->curr_vert], s);
    s->e_inpara = 0;
    return 0;
}

static int
ecmd_sketch_pick_segment(struct rt_edit *s)
{
    struct rt_sketch_edit *se = (struct rt_sketch_edit *)s->ipe_ptr;
    struct rt_sketch_internal *skt =
	(struct rt_sketch_internal *)s->es_int.idb_ptr;
    RT_SKETCH_CK_MAGIC(skt);

    if (!s->e_inpara) {
	bu_vls_printf(s->log_str, "ERROR: segment index required\n");
	return BRLCAD_ERROR;
    }

    int si = (int)s->e_para[0];
    if (si < 0 || (size_t)si >= skt->curve.count) {
	bu_vls_printf(s->log_str,
		      "ERROR: segment index %d out of range [0, %zu)\n",
		      si, skt->curve.count);
	s->e_inpara = 0;
	return BRLCAD_ERROR;
    }

    se->curr_seg = si;
    s->e_inpara = 0;
    return 0;
}

/* Collect the set of vertex indices referenced by segment[si] */
static void
sketch_seg_verts(const struct rt_sketch_internal *skt, int si,
		 int *verts_out, int *count_out)
{
    *count_out = 0;
    void *seg = skt->curve.segment[si];
    if (!seg) return;
    uint32_t magic = *(uint32_t *)seg;
    if (magic == CURVE_LSEG_MAGIC) {
	struct line_seg *ls = (struct line_seg *)seg;
	verts_out[(*count_out)++] = ls->start;
	verts_out[(*count_out)++] = ls->end;
    } else if (magic == CURVE_CARC_MAGIC) {
	struct carc_seg *cs = (struct carc_seg *)seg;
	verts_out[(*count_out)++] = cs->start;
	verts_out[(*count_out)++] = cs->end;
    } else if (magic == CURVE_BEZIER_MAGIC) {
	struct bezier_seg *bs = (struct bezier_seg *)seg;
	int j;
	for (j = 0; j <= bs->degree; j++)
	    verts_out[(*count_out)++] = bs->ctl_points[j];
    } else if (magic == CURVE_NURB_MAGIC) {
	struct nurb_seg *ns = (struct nurb_seg *)seg;
	int j;
	for (j = 0; j < ns->c_size; j++)
	    verts_out[(*count_out)++] = ns->ctl_points[j];
    }
}

static int
ecmd_sketch_move_segment(struct rt_edit *s)
{
    struct rt_sketch_edit *se = (struct rt_sketch_edit *)s->ipe_ptr;
    struct rt_sketch_internal *skt =
	(struct rt_sketch_internal *)s->es_int.idb_ptr;
    RT_SKETCH_CK_MAGIC(skt);

    if (se->curr_seg < 0) {
	bu_vls_printf(s->log_str, "ERROR: no segment selected (use ECMD_SKETCH_PICK_SEGMENT first)\n");
	return BRLCAD_ERROR;
    }
    if (!s->e_inpara || s->e_inpara < 2) {
	bu_vls_printf(s->log_str, "ERROR: two parameters required (dU dV in mm)\n");
	s->e_inpara = 0;
	return BRLCAD_ERROR;
    }

    fastf_t du = s->e_para[0] * s->local2base;
    fastf_t dv = s->e_para[1] * s->local2base;

    int verts[64]; /* bezier degree <= 63 is more than enough */
    int nv = 0;
    sketch_seg_verts(skt, se->curr_seg, verts, &nv);
    int j;
    for (j = 0; j < nv; j++) {
	int vi = verts[j];
	skt->verts[vi][0] += du;
	skt->verts[vi][1] += dv;
    }

    s->e_inpara = 0;
    return 0;
}

static int
ecmd_sketch_append_line(struct rt_edit *s)
{
    struct rt_sketch_internal *skt =
	(struct rt_sketch_internal *)s->es_int.idb_ptr;
    RT_SKETCH_CK_MAGIC(skt);

    if (!s->e_inpara || s->e_inpara < 2) {
	bu_vls_printf(s->log_str, "ERROR: two vertex indices required (start end)\n");
	s->e_inpara = 0;
	return BRLCAD_ERROR;
    }

    int v0 = (int)s->e_para[0];
    int v1 = (int)s->e_para[1];
    if (v0 < 0 || (size_t)v0 >= skt->vert_count ||
	v1 < 0 || (size_t)v1 >= skt->vert_count) {
	bu_vls_printf(s->log_str,
		      "ERROR: vertex index out of range (have %zu verts)\n",
		      skt->vert_count);
	s->e_inpara = 0;
	return BRLCAD_ERROR;
    }

    struct line_seg *ls;
    BU_ALLOC(ls, struct line_seg);
    ls->magic = CURVE_LSEG_MAGIC;
    ls->start = v0;
    ls->end   = v1;

    size_t old_count = skt->curve.count;
    skt->curve.count++;
    skt->curve.segment = (void **)bu_realloc(
	    skt->curve.segment,
	    skt->curve.count * sizeof(void *),
	    "sketch curve segments");
    skt->curve.reverse = (int *)bu_realloc(
	    skt->curve.reverse,
	    skt->curve.count * sizeof(int),
	    "sketch curve reverse");
    skt->curve.segment[old_count] = (void *)ls;
    skt->curve.reverse[old_count] = 0;

    s->e_inpara = 0;
    return 0;
}

static int
ecmd_sketch_append_arc(struct rt_edit *s)
{
    struct rt_sketch_internal *skt =
	(struct rt_sketch_internal *)s->es_int.idb_ptr;
    RT_SKETCH_CK_MAGIC(skt);

    /* e_para: [0]=start_vi [1]=end_vi [2]=radius_mm [3]=center_is_left [4]=orientation
     * e_inpara must be 5. */
    if (!s->e_inpara || s->e_inpara < 5) {
	bu_vls_printf(s->log_str,
		"ERROR: 5 parameters required "
		"(start_vi end_vi radius_mm center_is_left orientation)\n");
	s->e_inpara = 0;
	return BRLCAD_ERROR;
    }

    int v0  = (int)s->e_para[0];
    int v1  = (int)s->e_para[1];
    if (v0 < 0 || (size_t)v0 >= skt->vert_count ||
	v1 < 0 || (size_t)v1 >= skt->vert_count) {
	bu_vls_printf(s->log_str,
		      "ERROR: vertex index out of range (have %zu verts)\n",
		      skt->vert_count);
	s->e_inpara = 0;
	return BRLCAD_ERROR;
    }

    struct carc_seg *cs;
    BU_ALLOC(cs, struct carc_seg);
    cs->magic          = CURVE_CARC_MAGIC;
    cs->start          = v0;
    cs->end            = v1;
    cs->radius         = s->e_para[2] * s->local2base;
    cs->center_is_left = (int)s->e_para[3];
    cs->orientation    = (int)s->e_para[4];
    cs->center         = -1; /* computed during sketch tessellation */

    size_t old_count = skt->curve.count;
    skt->curve.count++;
    skt->curve.segment = (void **)bu_realloc(
	    skt->curve.segment,
	    skt->curve.count * sizeof(void *),
	    "sketch curve segments");
    skt->curve.reverse = (int *)bu_realloc(
	    skt->curve.reverse,
	    skt->curve.count * sizeof(int),
	    "sketch curve reverse");
    skt->curve.segment[old_count] = (void *)cs;
    skt->curve.reverse[old_count] = 0;

    s->e_inpara = 0;
    return 0;
}

static int
ecmd_sketch_append_bezier(struct rt_edit *s)
{
    struct rt_sketch_internal *skt =
	(struct rt_sketch_internal *)s->es_int.idb_ptr;
    RT_SKETCH_CK_MAGIC(skt);

    /* All e_inpara values are control point vertex indices.
     * degree = e_inpara - 1.
     * e_inpara must be >= 2 (at least linear bezier). */
    if (!s->e_inpara || s->e_inpara < 2) {
	bu_vls_printf(s->log_str,
		"ERROR: at least 2 vertex indices required "
		"(e_inpara = degree+1 control points)\n");
	s->e_inpara = 0;
	return BRLCAD_ERROR;
    }

    int degree = s->e_inpara - 1;

    struct bezier_seg *bs;
    BU_ALLOC(bs, struct bezier_seg);
    bs->magic  = CURVE_BEZIER_MAGIC;
    bs->degree = degree;
    bs->ctl_points = (int *)bu_malloc((degree + 1) * sizeof(int), "bezier ctl_points");
    int j;
    for (j = 0; j <= degree; j++) {
	int vi = (int)s->e_para[j];
	if (vi < 0 || (size_t)vi >= skt->vert_count) {
	    bu_vls_printf(s->log_str,
			  "ERROR: control point index %d out of range\n", vi);
	    bu_free(bs->ctl_points, "bezier ctl_points");
	    BU_FREE(bs, struct bezier_seg);
	    s->e_inpara = 0;
	    return BRLCAD_ERROR;
	}
	bs->ctl_points[j] = vi;
    }

    size_t old_count = skt->curve.count;
    skt->curve.count++;
    skt->curve.segment = (void **)bu_realloc(
	    skt->curve.segment,
	    skt->curve.count * sizeof(void *),
	    "sketch curve segments");
    skt->curve.reverse = (int *)bu_realloc(
	    skt->curve.reverse,
	    skt->curve.count * sizeof(int),
	    "sketch curve reverse");
    skt->curve.segment[old_count] = (void *)bs;
    skt->curve.reverse[old_count] = 0;

    s->e_inpara = 0;
    return 0;
}

static int
ecmd_sketch_delete_vertex(struct rt_edit *s)
{
    struct rt_sketch_edit *se = (struct rt_sketch_edit *)s->ipe_ptr;
    struct rt_sketch_internal *skt =
	(struct rt_sketch_internal *)s->es_int.idb_ptr;
    RT_SKETCH_CK_MAGIC(skt);

    if (se->curr_vert < 0) {
	bu_vls_printf(s->log_str, "ERROR: no vertex selected\n");
	return BRLCAD_ERROR;
    }
    int vi = se->curr_vert;
    if (sketch_vert_is_used(skt, vi)) {
	bu_vls_printf(s->log_str,
		      "ERROR: vertex %d is referenced by a segment; delete the segment first\n",
		      vi);
	return BRLCAD_ERROR;
    }

    /* Remove vertex vi by collapsing the array */
    size_t i;
    for (i = vi; i + 1 < skt->vert_count; i++) {
	V2MOVE(skt->verts[i], skt->verts[i + 1]);
    }
    skt->vert_count--;

    /* Fix up all segment vertex references > vi */
    for (i = 0; i < skt->curve.count; i++) {
	void *seg = skt->curve.segment[i];
	if (!seg) continue;
	uint32_t magic = *(uint32_t *)seg;
	if (magic == CURVE_LSEG_MAGIC) {
	    struct line_seg *ls = (struct line_seg *)seg;
	    if (ls->start > vi) ls->start--;
	    if (ls->end   > vi) ls->end--;
	} else if (magic == CURVE_CARC_MAGIC) {
	    struct carc_seg *cs = (struct carc_seg *)seg;
	    if (cs->start > vi) cs->start--;
	    if (cs->end   > vi) cs->end--;
	} else if (magic == CURVE_BEZIER_MAGIC) {
	    struct bezier_seg *bs = (struct bezier_seg *)seg;
	    int j;
	    for (j = 0; j <= bs->degree; j++)
		if (bs->ctl_points[j] > vi) bs->ctl_points[j]--;
	} else if (magic == CURVE_NURB_MAGIC) {
	    struct nurb_seg *ns = (struct nurb_seg *)seg;
	    int j;
	    for (j = 0; j < ns->c_size; j++)
		if (ns->ctl_points[j] > vi) ns->ctl_points[j]--;
	}
    }

    se->curr_vert = -1;
    s->e_inpara = 0;
    return 0;
}

static int
ecmd_sketch_delete_segment(struct rt_edit *s)
{
    struct rt_sketch_edit *se = (struct rt_sketch_edit *)s->ipe_ptr;
    struct rt_sketch_internal *skt =
	(struct rt_sketch_internal *)s->es_int.idb_ptr;
    RT_SKETCH_CK_MAGIC(skt);

    if (se->curr_seg < 0) {
	bu_vls_printf(s->log_str, "ERROR: no segment selected\n");
	return BRLCAD_ERROR;
    }

    int si = se->curr_seg;

    /* Free the segment data */
    void *seg = skt->curve.segment[si];
    if (seg) {
	uint32_t magic = *(uint32_t *)seg;
	if (magic == CURVE_BEZIER_MAGIC) {
	    struct bezier_seg *bs = (struct bezier_seg *)seg;
	    bu_free(bs->ctl_points, "bezier ctl_points");
	}
	bu_free(seg, "sketch segment");
    }

    /* Collapse the segment array */
    size_t i;
    for (i = si; i + 1 < skt->curve.count; i++) {
	skt->curve.segment[i] = skt->curve.segment[i + 1];
	skt->curve.reverse[i] = skt->curve.reverse[i + 1];
    }
    skt->curve.count--;

    se->curr_seg = -1;
    s->e_inpara = 0;
    return 0;
}


/* ------------------------------------------------------------------ */
/* ft_edit / ft_edit_xy dispatch                                       */
/* ------------------------------------------------------------------ */

int
rt_edit_sketch_edit(struct rt_edit *s)
{
    switch (s->edit_flag) {
	case RT_PARAMS_EDIT_SCALE:
	    return edit_sscale(s);
	case RT_PARAMS_EDIT_TRANS:
	    edit_stra(s);
	    break;
	case RT_PARAMS_EDIT_ROT:
	    edit_srot(s);
	    break;
	case ECMD_SKETCH_PICK_VERTEX:
	    return ecmd_sketch_pick_vertex(s);
	case ECMD_SKETCH_MOVE_VERTEX:
	    return ecmd_sketch_move_vertex(s);
	case ECMD_SKETCH_PICK_SEGMENT:
	    return ecmd_sketch_pick_segment(s);
	case ECMD_SKETCH_MOVE_SEGMENT:
	    return ecmd_sketch_move_segment(s);
	case ECMD_SKETCH_APPEND_LINE:
	    return ecmd_sketch_append_line(s);
	case ECMD_SKETCH_APPEND_ARC:
	    return ecmd_sketch_append_arc(s);
	case ECMD_SKETCH_APPEND_BEZIER:
	    return ecmd_sketch_append_bezier(s);
	case ECMD_SKETCH_DELETE_VERTEX:
	    return ecmd_sketch_delete_vertex(s);
	case ECMD_SKETCH_DELETE_SEGMENT:
	    return ecmd_sketch_delete_segment(s);
	default:
	    return edit_generic(s);
    }

    return 0;
}

int
rt_edit_sketch_edit_xy(struct rt_edit *s, const vect_t mousevec)
{
    vect_t pos_view = VINIT_ZERO;

    switch (s->edit_flag) {
	case RT_PARAMS_EDIT_SCALE:
	    edit_sscale_xy(s, mousevec);
	    return 0;
	case RT_PARAMS_EDIT_TRANS:
	    edit_stra_xy(&pos_view, s, mousevec);
	    break;
	default:
	    return edit_generic_xy(s, mousevec);
    }

    edit_abs_tra(s, pos_view);
    return 0;
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
