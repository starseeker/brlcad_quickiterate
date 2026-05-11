/*                   N O D E _ C O R E . C
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
/** @file libbsg/node_core.c
 *
 * Phase 10: BSG node-core initialization and cleanup.
 *
 * Provides:
 *   _bsg_core_release()   — cleanup hook stored in bsg_core.bsg_core_free_fn;
 *                           called by bv_obj_reset() to free material,
 *                           appearance, and payload before zeroing the core.
 *   bsg_node_core_get()   — public accessor that ensures the core is
 *                           initialized and returns a typed pointer.
 *   bsg_node_core_init()  — explicit initializer for pre-emptive use.
 *   bsg_node_core_initialized() — diagnostic query.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "bv/defines.h"
#include "bsg/defines.h"
#include "bsg/node_core.h"
#include "bsg/payload.h"

#include "./bsg_private.h"


/* ------------------------------------------------------------------ */
/* Internal: cleanup hook for bv_obj_reset()                            */
/* ------------------------------------------------------------------ */

/**
 * Release any libbsg-owned heap data in @p core, then zero the pointer
 * fields so a subsequent free by bv_obj_reset() is safe.
 *
 * This function is stored in core->bsg_core_free_fn by _bsg_core_ensure()
 * and is therefore called by bv_obj_reset() via the function pointer
 * (no libbsg -> libbv symbol dependency).
 */
void
_bsg_core_release(struct bsg_node_core *core)
{
    if (!core)
	return;

    if (core->material) {
	bu_free(core->material, "bsg_node_core material");
	core->material = NULL;
    }

    if (core->appearance) {
	bu_free(core->appearance, "bsg_node_core appearance");
	core->appearance = NULL;
    }

    if (core->payload) {
	bsg_payload_destroy((struct bsg_payload *)core->payload);
	core->payload = NULL;
    }
}


/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

struct bsg_node_core *
bsg_node_core_get(bsg_node *n)
{
    return _bsg_core_ensure(n);
}


void
bsg_node_core_init(bsg_node *n)
{
    (void)_bsg_core_ensure(n);
}


int
bsg_node_core_initialized(const bsg_node *n)
{
    if (!n)
	return 0;
    return (((const struct bv_scene_obj *)n)->bsg_core.bsg_magic
	    == BSG_NODE_CORE_MAGIC) ? 1 : 0;
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
