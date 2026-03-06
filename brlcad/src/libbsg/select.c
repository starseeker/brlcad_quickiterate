/*                     S E L E C T . C
 * BRL-CAD
 *
 * Copyright (c) 2024-2025 United States Government as represented by
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
/**
 * @file libbsg/select.c
 *
 * @brief Selection state management.
 *
 * Implements Step 7 of the libbsg migration plan.  Provides a clean
 * re-implementation of selection / highlight with no @c DbiState coupling.
 *
 * ### Design
 *
 * @c libbsg_select_state holds a @c bu_ptbl of @c bu_vls path strings.
 * Selection is path-based: "top/hull/armor".
 *
 * Highlight synchronisation (@c libbsg_select_sync_highlight) walks a
 * libbsg scene graph and toggles the @c s_iflag field on every
 * @c libbsg_shape whose path matches a selected path.  No DBI or
 * database-layer dependency is required.
 */

#include "common.h"

#include <string.h>   /* strcmp */

#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/vls.h"
#include "bu/str.h"

#include "libbsg/libbsg.h"

/* ====================================================================== *
 * libbsg_select_state definition                                         *
 * ====================================================================== */

/**
 * @brief Selection state: tracks the set of selected object paths.
 *
 * Embed in application data or heap-allocate and pass around.
 * Initialise with @c libbsg_select_init().
 */
struct libbsg_select_state {
    struct bu_ptbl paths;   /**< bu_ptbl of bu_vls * (owned) */
};

/* ====================================================================== *
 * Lifecycle                                                               *
 * ====================================================================== */

struct libbsg_select_state *
libbsg_select_alloc(void)
{
    struct libbsg_select_state *ss;
    BU_GET(ss, struct libbsg_select_state);
    bu_ptbl_init(&ss->paths, 8, "libbsg_select_state paths");
    return ss;
}

void
libbsg_select_free(struct libbsg_select_state *ss)
{
    size_t i;
    if (!ss) return;
    for (i = 0; i < BU_PTBL_LEN(&ss->paths); i++) {
	struct bu_vls *vp = (struct bu_vls *)BU_PTBL_GET(&ss->paths, i);
	bu_vls_free(vp);
	bu_free(vp, "libbsg_select path");
    }
    bu_ptbl_free(&ss->paths);
    BU_PUT(ss, struct libbsg_select_state);
}

void
libbsg_select_clear(struct libbsg_select_state *ss)
{
    size_t i;
    if (!ss) return;
    for (i = 0; i < BU_PTBL_LEN(&ss->paths); i++) {
	struct bu_vls *vp = (struct bu_vls *)BU_PTBL_GET(&ss->paths, i);
	bu_vls_free(vp);
	bu_free(vp, "libbsg_select path");
    }
    bu_ptbl_reset(&ss->paths);
}

/* ====================================================================== *
 * Path management                                                         *
 * ====================================================================== */

void
libbsg_select_add_path(struct libbsg_select_state *ss, const char *path)
{
    size_t i;
    struct bu_vls *vp;

    if (!ss || !path) return;

    /* Avoid duplicates. */
    for (i = 0; i < BU_PTBL_LEN(&ss->paths); i++) {
	vp = (struct bu_vls *)BU_PTBL_GET(&ss->paths, i);
	if (BU_STR_EQUAL(bu_vls_cstr(vp), path))
	    return;
    }

    BU_GET(vp, struct bu_vls);
    bu_vls_init(vp);
    bu_vls_strcpy(vp, path);
    bu_ptbl_ins(&ss->paths, (long *)vp);
}

void
libbsg_select_rm_path(struct libbsg_select_state *ss, const char *path)
{
    size_t i;
    if (!ss || !path) return;
    for (i = 0; i < BU_PTBL_LEN(&ss->paths); i++) {
	struct bu_vls *vp = (struct bu_vls *)BU_PTBL_GET(&ss->paths, i);
	if (BU_STR_EQUAL(bu_vls_cstr(vp), path)) {
	    bu_ptbl_rm(&ss->paths, (long *)vp);
	    bu_vls_free(vp);
	    bu_free(vp, "libbsg_select path");
	    return;
	}
    }
}

int
libbsg_select_has_path(const struct libbsg_select_state *ss, const char *path)
{
    size_t i;
    if (!ss || !path) return 0;
    for (i = 0; i < BU_PTBL_LEN(&ss->paths); i++) {
	const struct bu_vls *vp = (const struct bu_vls *)BU_PTBL_GET(&ss->paths, i);
	if (BU_STR_EQUAL(bu_vls_cstr(vp), path))
	    return 1;
    }
    return 0;
}

size_t
libbsg_select_count(const struct libbsg_select_state *ss)
{
    if (!ss) return 0;
    return BU_PTBL_LEN(&ss->paths);
}

/* ====================================================================== *
 * Highlight synchronisation                                               *
 *                                                                        *
 * Walk a libbsg scene graph and toggle s_iflag on shapes whose name     *
 * matches a selected path.                                               *
 * ====================================================================== */

/**
 * @brief Context for the highlight sync traversal visitor.
 */
struct _sync_ctx {
    const struct libbsg_select_state *ss;
    int ilum_flag;   /**< value to assign to s_iflag for matching shapes */
};

static int
_sync_visitor(bsg_node *node, const libbsg_traversal_state *state, void *ud)
{
    (void)state;
    if (!(node->type_flags & LIBBSG_NODE_SHAPE))
	return 0;

    struct _sync_ctx *ctx = (struct _sync_ctx *)ud;
    libbsg_shape *s = (libbsg_shape *)node;

    if (libbsg_select_has_path(ctx->ss, bu_vls_cstr(&s->s_name)))
	s->s_iflag = ctx->ilum_flag;
    else
	s->s_iflag = 0;

    return 0;
}

void
libbsg_select_sync_highlight(bsg_node                         *root,
			     const struct libbsg_select_state *ss,
			     int                               ilum_flag)
{
    struct _sync_ctx ctx;
    libbsg_traversal_state state;

    if (!root || !ss) return;

    ctx.ss        = ss;
    ctx.ilum_flag = ilum_flag;

    libbsg_traversal_state_init(&state);
    libbsg_traverse(root, &state, _sync_visitor, &ctx);
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
