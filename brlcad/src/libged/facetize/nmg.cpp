/*                        N M G  . C P P
 * BRL-CAD
 *
 * Copyright (c) 2008-2026 United States Government as represented by
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
/** @file libged/facetize/nmg.cpp
 *
 * GED wrapper for the libgcv classic NMG boolean evaluation path.
 */

#include "common.h"

#include "gcv/facetize.h"
#include "../ged_private.h"
#include "./ged_facetize.h"

static void
_ged_facetize_nmg_log(void *ctx, int verbosity, const char *msg)
{
    struct _ged_facetize_state *s = (struct _ged_facetize_state *)ctx;

    if (!s || !msg)
        return;

    facetize_log(s, verbosity, "%s", msg);
}

int
_ged_facetize_nmgeval(struct _ged_facetize_state *s, int argc, const char **argv, const char *oname)
{
    if (!s)
        return BRLCAD_ERROR;

    return gcv_facetize_nmg_eval_to_db(s->dbip,
            argc,
            argv,
            oname,
            s->make_nmg,
            s->verbosity,
            _ged_facetize_nmg_log,
            (void *)s);
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
