/*                        F B S E R V . C
 * BRL-CAD
 *
 * Copyright (c) 1995-2025 United States Government as represented by
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
/** @file mged/fbserv.c
 *
 * Step 7.20: fbserv stub — the framebuffer-server path required a libdm
 * display manager handle (mp_fbp/mp_netfd/mp_clients), all of which have
 * been removed from mged_pane.  MGED is now Obol-only and has no legacy
 * dm attach path, so fbserv_set_port is a no-op.
 */

#include "common.h"

#include "vmath.h"
#include "raytrace.h"

#include "./mged.h"
#include "./mged_dm.h"


/*
 * Called from set.c when mv_listen or mv_port changes.
 * Step 7.20: No-op — fbp/mp_netfd removed from mged_pane.
 */
void
fbserv_set_port(const struct bu_structparse *UNUSED(sdp),
const char *UNUSED(name),
void *UNUSED(base),
const char *UNUSED(value),
void *UNUSED(data))
{
    /* Step 7.20: libdm framebuffer server removed — no-op. */
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
