/*                     R E N D E R . C
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
/** @file libbsg/render.c
 *
 * Phase 8: BSG render-request — pre-render traversal and payload dispatch.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "bu/ptbl.h"

#include "bsg/defines.h"
#include "bsg/visit.h"
#include "bsg/payload.h"
#include "bsg/render.h"


/* ------------------------------------------------------------------ */
/* Internal visitor state                                               */
/* ------------------------------------------------------------------ */

struct render_state {
    struct bsg_render_request *req;
    struct bu_ptbl             overlays;  /* deferred overlay nodes */
    int                        dispatched;
};

static int
render_visit_cb(bsg_node *node, void *userdata)
{
    struct render_state *st = (struct render_state *)userdata;
    const struct bsg_render_request *req = st->req;

    /* Only dispatch shape nodes */
    if (!(node->s_type_flags & BSG_NODE_SHAPE))
	return 1;

    /* Visibility filter */
    if ((req->flags & BSG_RENDER_FLAG_VISIBLE_ONLY) &&
	node->s_flag == DOWN)
	return 1;

    /* Defer overlay nodes if requested */
    if ((req->flags & BSG_RENDER_FLAG_OVERLAY_LAST) &&
	(bsg_node_get_payload_type(node) & BSG_PAYLOAD_OVERLAY)) {
	bu_ptbl_ins_unique(&st->overlays, (long *)node);
	return 1;
    }

    /* Payload dispatch */
    if (req->flags & BSG_RENDER_FLAG_PAYLOAD_DISPATCH)
	bsg_payload_dispatch(req->dmp, node, req->view);

    st->dispatched++;
    return 1;
}


/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

struct bsg_render_request *
bsg_render_request_create(struct bsg_view *view,
			  bsg_node        *root,
			  void            *dmp)
{
    struct bsg_render_request *req;
    BU_ALLOC(req, struct bsg_render_request);
    memset(req, 0, sizeof(struct bsg_render_request));
    req->view  = view;
    req->root  = root;
    req->dmp   = dmp;
    req->flags = BSG_RENDER_FLAG_VISIBLE_ONLY | BSG_RENDER_FLAG_PAYLOAD_DISPATCH;
    return req;
}


void
bsg_render_request_destroy(struct bsg_render_request *req)
{
    if (!req)
	return;
    bu_free(req, "bsg_render_request");
}


int
bsg_render_request_execute(struct bsg_render_request *req)
{
    if (!req)
	return -1;

    struct render_state st;
    st.req        = req;
    st.dispatched = 0;
    bu_ptbl_init(&st.overlays, 4, "render overlays");

    bsg_visit(req->root, 0, render_visit_cb, &st);

    /* Dispatch deferred overlay nodes */
    if (BU_PTBL_LEN(&st.overlays) > 0) {
	size_t i;
	for (i = 0; i < BU_PTBL_LEN(&st.overlays); i++) {
	    bsg_node *node = (bsg_node *)BU_PTBL_GET(&st.overlays, i);

	    if ((req->flags & BSG_RENDER_FLAG_VISIBLE_ONLY) &&
		node->s_flag == DOWN)
		continue;

	    if (req->flags & BSG_RENDER_FLAG_PAYLOAD_DISPATCH)
		bsg_payload_dispatch(req->dmp, node, req->view);

	    st.dispatched++;
	}
    }

    bu_ptbl_free(&st.overlays);
    return st.dispatched;
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
