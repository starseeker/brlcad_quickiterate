/*              B O T _ O R I E N T E D _ B B O X . C P P
 * BRL-CAD
 *
 * Copyright (c) 2008-2025 United States Government as represented by
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

#include "common.h"

#include "bg/pnts.h"
#include "bg/trimesh.h"
#include "raytrace.h"
#include "rt/geom.h"


/* calc oriented bounding box via bg_trimesh_obb (face-aware, tighter OBB) */
extern "C" int
rt_bot_oriented_bbox(struct rt_arb_internal *bbox, struct rt_db_internal *ip, const fastf_t UNUSED(tol))
{
    struct rt_bot_internal *bot_ip;
    RT_CK_DB_INTERNAL(ip);
    bot_ip = (struct rt_bot_internal *)ip->idb_ptr;
    RT_BOT_CK_MAGIC(bot_ip);

    point_t c;
    vect_t v1, v2, v3;
    if (bg_trimesh_obb(&c, &v1, &v2, &v3,
		       bot_ip->faces, bot_ip->num_faces,
		       (const point_t *)bot_ip->vertices, bot_ip->num_vertices)
	!= BRLCAD_OK)
	return -1;

    /* Convert OBB center+extents to 8 arb8 corner points */
    point_t verts[8];
    if (bg_obb_pnts(verts, &c, &v1, &v2, &v3) != BRLCAD_OK)
	return -1;

    for (int k = 0; k < 8; k++)
	VMOVE(bbox->pt[k], verts[k]);

    return 0;
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
