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
 * Implements bsg_payload lifecycle helpers and the first two concrete
 * payload types (BSG_PL_TEXT, BSG_PL_AXES) as Phase D1 pilots.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "bu/vls.h"
#include "bsg/defines.h"
#include "bsg/payload_typed.h"


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


/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
