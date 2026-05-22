/*                        V L I S T . C
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
/** @file libbsg/vlist.c
 *
 * Self-contained vlist helpers for BSG scene-graph construction.
 *
 * All functions in this file are implemented directly without calling any
 * compiled libbv symbol.  This is intentional: libbv is being phased out
 * and libbsg should not acquire a runtime dependency on it.
 *
 * Functions provided:
 *   bsg_vlist_cmd_cnt   — count commands in a bv_vlist chain
 *   bsg_vlist_bbox      — compute AABB from a vlist chain
 *   bsg_vlist_copy      — copy one vlist into another
 *   bsg_vlist_cleanup   — free all chunks in a free-list
 *   bsg_vlist_arb8      — emit 18-command ARB8 wireframe
 *   bsg_vlist_3string   — stroke text in 3-D (vector font, native impl)
 *   bsg_vlist_2string   — stroke text in 2-D (vector font, native impl)
 */

#include "common.h"

#include <math.h>
#include <string.h>

#include "vmath.h"
#include "bu/list.h"
#include "bu/malloc.h"
#include "bn/mat.h"
#include "bv/vectfont.h"
#include "bv/vlist.h"

#include "bsg/defines.h"
#include "bsg/vlist.h"


/* ------------------------------------------------------------------ */
/* bsg_vlist_cmd_cnt                                                   */
/* ------------------------------------------------------------------ */

size_t
bsg_vlist_cmd_cnt(struct bv_vlist *vlist)
{
    size_t num_commands = 0;
    struct bv_vlist *vp;

    if (!vlist)
	return 0;

    for (BU_LIST_FOR(vp, bv_vlist, &(vlist->l)))
	num_commands += vp->nused;

    return num_commands;
}


/* ------------------------------------------------------------------ */
/* bsg_vlist_bbox                                                      */
/* ------------------------------------------------------------------ */

int
bsg_vlist_bbox(struct bu_list *vlistp, point_t *bmin, point_t *bmax)
{
    struct bv_vlist *vp;

    if (!vlistp)
	return 0;

    for (BU_LIST_FOR(vp, bv_vlist, vlistp)) {
	size_t i;
	size_t nused = vp->nused;
	int *cmd = vp->cmd;
	point_t *pt = vp->pt;

	for (i = 0; i < nused; i++, cmd++, pt++) {
	    switch (*cmd) {
		/* Attribute commands — no position */
		case BV_VLIST_POLY_START:
		case BV_VLIST_POLY_VERTNORM:
		case BV_VLIST_TRI_START:
		case BV_VLIST_TRI_VERTNORM:
		case BV_VLIST_POINT_SIZE:
		case BV_VLIST_LINE_WIDTH:
		case BV_VLIST_MODEL_MAT:
		case BV_VLIST_DISPLAY_MAT:
		    break;
		/* Position-bearing commands */
		case BV_VLIST_LINE_MOVE:
		case BV_VLIST_LINE_DRAW:
		case BV_VLIST_POLY_MOVE:
		case BV_VLIST_POLY_DRAW:
		case BV_VLIST_POLY_END:
		case BV_VLIST_TRI_MOVE:
		case BV_VLIST_TRI_DRAW:
		case BV_VLIST_TRI_END:
		case BV_VLIST_POINT_DRAW:
		    if (bmin) VMIN((*bmin), *pt);
		    if (bmax) VMAX((*bmax), *pt);
		    break;
		default:
		    return *cmd;
	    }
	}
    }
    return 0;
}


/* ------------------------------------------------------------------ */
/* bsg_vlist_copy                                                      */
/* ------------------------------------------------------------------ */

void
bsg_vlist_copy(struct bu_list *vlists,
	       struct bu_list *dest,
	       const struct bu_list *src)
{
    struct bv_vlist *vp;

    if (!vlists || !dest || !src)
	return;

    for (BU_LIST_FOR(vp, bv_vlist, src)) {
	size_t i;
	size_t nused = vp->nused;
	int *cmd = vp->cmd;
	point_t *pt = vp->pt;
	for (i = 0; i < nused; i++, cmd++, pt++)
	    BV_ADD_VLIST(vlists, dest, *pt, *cmd);
    }
}


/* ------------------------------------------------------------------ */
/* bsg_vlist_cleanup                                                   */
/* ------------------------------------------------------------------ */

void
bsg_vlist_cleanup(struct bu_list *hd)
{
    struct bv_vlist *vp;

    if (!hd)
	return;

    if (!BU_LIST_IS_INITIALIZED(hd)) {
	BU_LIST_INIT(hd);
	return;
    }

    while (BU_LIST_WHILE(vp, bv_vlist, hd)) {
	BU_LIST_DEQUEUE(&(vp->l));
	bu_free((char *)vp, "bsg vlist cleanup");
    }
}


/* ------------------------------------------------------------------ */
/* bsg_vlist_arb8                                                      */
/* ------------------------------------------------------------------ */

void
bsg_vlist_arb8(struct bu_list *vhead, struct bu_list *vlfree, point_t pts[8])
{
    if (!vhead || !vlfree || !pts)
	return;

    /* Bottom face loop: MOVE(0) DRAW(1) DRAW(2) DRAW(3) DRAW(0)  — 5 cmds */
    BV_ADD_VLIST(vlfree, vhead, pts[0], BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(vlfree, vhead, pts[1], BV_VLIST_LINE_DRAW);
    BV_ADD_VLIST(vlfree, vhead, pts[2], BV_VLIST_LINE_DRAW);
    BV_ADD_VLIST(vlfree, vhead, pts[3], BV_VLIST_LINE_DRAW);
    BV_ADD_VLIST(vlfree, vhead, pts[0], BV_VLIST_LINE_DRAW); /* close */

    /* Top face loop: MOVE(4) DRAW(5) DRAW(6) DRAW(7) DRAW(4)     — 5 cmds */
    BV_ADD_VLIST(vlfree, vhead, pts[4], BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(vlfree, vhead, pts[5], BV_VLIST_LINE_DRAW);
    BV_ADD_VLIST(vlfree, vhead, pts[6], BV_VLIST_LINE_DRAW);
    BV_ADD_VLIST(vlfree, vhead, pts[7], BV_VLIST_LINE_DRAW);
    BV_ADD_VLIST(vlfree, vhead, pts[4], BV_VLIST_LINE_DRAW); /* close */

    /* Four vertical edges: 4 × (MOVE + DRAW)                     — 8 cmds */
    BV_ADD_VLIST(vlfree, vhead, pts[0], BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(vlfree, vhead, pts[4], BV_VLIST_LINE_DRAW);

    BV_ADD_VLIST(vlfree, vhead, pts[1], BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(vlfree, vhead, pts[5], BV_VLIST_LINE_DRAW);

    BV_ADD_VLIST(vlfree, vhead, pts[2], BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(vlfree, vhead, pts[6], BV_VLIST_LINE_DRAW);

    BV_ADD_VLIST(vlfree, vhead, pts[3], BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(vlfree, vhead, pts[7], BV_VLIST_LINE_DRAW);

    /* Total: 5 + 5 + 8 = 18 vlist commands */
}


/* ------------------------------------------------------------------ */
/* bsg_vlist_3string — native vector-font stroke (3-D)                 */
/* ------------------------------------------------------------------ */

void
bsg_vlist_3string(struct bu_list *vhead,
		  struct bu_list *free_hd,
		  const char *string,
		  const point_t origin,
		  const mat_t rot,
		  double scale)
{
    register unsigned char *cp;
    double offset;
    int ysign;
    vect_t temp;
    vect_t loc;
    mat_t xlate_to_origin;
    mat_t mat;

    if (!vhead || !free_hd || !string)
	return;

    if (string[0] == '\0')
	return;

    /*
     * Build a combined matrix: first apply rot, then translate to origin.
     * Text is initially in a local space with lower-left at (0,0,0).
     */
    MAT_IDN(xlate_to_origin);
    MAT_DELTAS_VEC(xlate_to_origin, origin);
    bn_mat_mul(mat, xlate_to_origin, rot);

    offset = 0;
    for (cp = (unsigned char *)string; *cp; cp++, offset += scale) {
	register int *p;
	register int stroke;

	VSET(temp, offset, 0, 0);
	MAT4X3PNT(loc, mat, temp);
	BV_ADD_VLIST(free_hd, vhead, loc, BV_VLIST_LINE_MOVE);

	for (p = tp_getchar(cp); ((stroke = *p)) != VFONT_LAST; p++) {
	    int draw;

	    if (stroke == NEGY) {
		ysign = -1;
		stroke = *++p;
	    } else {
		ysign = 1;
	    }

	    if (stroke < 0) {
		stroke = -stroke;
		draw = 0;
	    } else {
		draw = 1;
	    }

	    VSET(temp,
		 (stroke / 11) * 0.1 * scale + offset,
		 (ysign * (stroke % 11)) * 0.1 * scale,
		 0);
	    MAT4X3PNT(loc, mat, temp);
	    if (draw)
		BV_ADD_VLIST(free_hd, vhead, loc, BV_VLIST_LINE_DRAW);
	    else
		BV_ADD_VLIST(free_hd, vhead, loc, BV_VLIST_LINE_MOVE);
	}
    }
}


/* ------------------------------------------------------------------ */
/* bsg_vlist_2string — native vector-font stroke (2-D)                 */
/* ------------------------------------------------------------------ */

void
bsg_vlist_2string(struct bu_list *vhead,
		  struct bu_list *free_hd,
		  const char *string,
		  double x,
		  double y,
		  double scale,
		  double theta)
{
    mat_t mat;
    vect_t p;

    if (!vhead || !free_hd || !string)
	return;

    bn_mat_angles(mat, 0.0, 0.0, theta);
    VSET(p, x, y, 0);
    bsg_vlist_3string(vhead, free_hd, string, p, mat, scale);
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
