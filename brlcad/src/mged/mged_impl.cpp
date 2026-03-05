/*                   M G E D _ I M P L . C P P
 * BRL-CAD
 *
 * Copyright (c) 2019-2025 United States Government as represented by
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
/** @file mged_impl.cpp
 *
 * Internal state implementations for the per-primitive callback map
 * infrastructure.
 */

#include "common.h"

#include "./mged.h"
#include "./sedit.h"

MGED_Internal::MGED_Internal()
{
}

MGED_Internal::~MGED_Internal()
{
    std::map<int, rt_edit_map *>::iterator c_it;
    for (c_it = cmd_map.begin(); c_it != cmd_map.end(); c_it++) {
	rt_edit_map *m = c_it->second;
	rt_edit_map_destroy(m);
    }
}

extern "C" struct mged_state_impl *
mged_state_impl_create(void)
{
    struct mged_state_impl *i;
    BU_GET(i, struct mged_state_impl);
    i->i = new MGED_Internal;
    return i;
}

extern "C" void
mged_state_impl_destroy(struct mged_state_impl *i)
{
    if (!i)
	return;
    delete i->i;
    BU_PUT(i, struct mged_state_impl);
}

static struct rt_edit_map *
mged_internal_clbk_map(MGED_Internal *i, int obj_type)
{
    struct rt_edit_map *omap = NULL;
    std::map<int, rt_edit_map *>::iterator m_it = i->cmd_map.find(obj_type);
    if (m_it != i->cmd_map.end()) {
	omap = m_it->second;
    } else {
	omap = rt_edit_map_create();
	i->cmd_map[obj_type] = omap;
    }
    return omap;
}

extern "C" int
mged_state_clbk_set(struct mged_state *s, int obj_type, int ed_cmd, int mode, bu_clbk_t f, void *d)
{
    if (!s)
	return BRLCAD_OK;

    MGED_Internal *i = s->i->i;
    struct rt_edit_map *mp = mged_internal_clbk_map(i, obj_type);
    if (!mp)
	return BRLCAD_ERROR;

    return rt_edit_map_clbk_set(mp, ed_cmd, mode, f, d);
}

extern "C" int
mged_state_clbk_get(bu_clbk_t *f, void **d, struct mged_state *s, int obj_type, int ed_cmd, int mode)
{
    if (!f || !d || !s)
	return BRLCAD_OK;

    MGED_Internal *i = s->i->i;
    struct rt_edit_map *mp = mged_internal_clbk_map(i, obj_type);
    if (!mp)
	return BRLCAD_ERROR;

    return rt_edit_map_clbk_get(f, d, mp, ed_cmd, mode);
}

extern "C" int
mged_edit_clbk_sync(struct rt_edit *se, struct mged_state *s)
{
    if (!se)
	return BRLCAD_ERROR;

    MGED_Internal *i = s->i->i;

    rt_edit_map_clear(se->m);

    struct rt_edit_map *gmp = mged_internal_clbk_map(i, 0);
    rt_edit_map_copy(se->m, gmp);

    struct rt_edit_map *mp = mged_internal_clbk_map(i, MEDIT(s)->es_int.idb_type);

    return rt_edit_map_copy(se->m, mp);
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
