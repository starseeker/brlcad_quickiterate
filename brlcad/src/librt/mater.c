/*                         M A T E R . C
 * BRL-CAD
 *
 * Copyright (c) 1985-2025 United States Government as represented by
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
/** @addtogroup librt */
/** @{ */
/** @file librt/mater.c
 *
 * Implementation of the per-database region-ID material/color table.
 *
 * Establishes and maintains the sorted list of @c struct @c mater entries that
 * map region-ID ranges to RGB colors.  See include/rt/mater.h for the full API
 * description.
 */
/** @} */

#include "common.h"

#include "bio.h"

#include "vmath.h"
#include "raytrace.h"
#include "./librt_private.h"

/*
 * Entries are kept in strictly ascending order by mt_low, with no overlaps.
 */


static void
_db_mater_print(const struct mater *mp)
{
    bu_log("%5ld..%ld\t", mp->mt_low, mp->mt_high);
    bu_log("%d, %d, %d\t", mp->mt_r, mp->mt_g, mp->mt_b);
}


static void
_rt_check_overlap(struct mater *newp)
{
    struct mater *zot;

    /* Check for overlap, i.e., redefinition of following colors */
    while (newp->mt_forw != MATER_NULL &&
	   newp->mt_high >= newp->mt_forw->mt_low)
    {
	if (newp->mt_high >= newp->mt_forw->mt_high) {
	    /* Drop this mater struct */
	    zot = newp->mt_forw;
	    newp->mt_forw = zot->mt_forw;
	    bu_log("dropping overlapping region-id based material property entry:\n");
	    _db_mater_print(zot);
	    bu_free((char *)zot, "getstruct mater");
	    continue;
	}
	if (newp->mt_high >= newp->mt_forw->mt_low) {
	    /* Shorten this mater struct, then done */
	    bu_log("Shortening region-id based material property entry rhs range, from:\n");
	    _db_mater_print(newp->mt_forw);
	    bu_log("to:\n");
	    newp->mt_forw->mt_low = newp->mt_high+1;
	    _db_mater_print(newp->mt_forw);
	    continue;	/* more conservative than returning */
	}
    }
}


/**
 * While any additional database records are created and written here,
 * it is the responsibility of the caller to color_putrec(newp) if
 * needed.
 */
void
db_mater_insert(struct db_i *dbip, struct mater *newp)
{
    struct mater *mp;
    struct mater *zot;
    struct mater **material_headp;

    RT_CK_DBI(dbip);
    material_headp = &dbip->i->material_head;

    if (*material_headp == MATER_NULL || newp->mt_high < (*material_headp)->mt_low) {
	/* Insert at head of list */
	newp->mt_forw = *material_headp;
	*material_headp = newp;
	return;
    }
    if (newp->mt_low < (*material_headp)->mt_low) {
	/* Insert at head of list, check for redefinition */
	newp->mt_forw = *material_headp;
	*material_headp = newp;
	_rt_check_overlap(newp);
	return;
    }
    for (mp = *material_headp; mp != MATER_NULL; mp = mp->mt_forw) {
	if (mp->mt_low == newp->mt_low && mp->mt_high <= newp->mt_high) {
	    bu_log("dropping overwritten region-id based material property entry:\n");
	    newp->mt_forw = mp->mt_forw;
	    _db_mater_print(mp);
	    *mp = *newp;		/* struct copy */
	    bu_free((char *)newp, "getstruct mater");
	    newp = mp;
	    _rt_check_overlap(newp);
	    return;
	}
	if (mp->mt_low < newp->mt_low && mp->mt_high > newp->mt_high) {
	    /* New range entirely contained in old range; split */
	    bu_log("Splitting region-id based material property entry into 3 ranges\n");
	    BU_ALLOC(zot, struct mater);
	    *zot = *mp;		/* struct copy */
	    zot->mt_daddr = MATER_NO_ADDR;
	    /* zot->mt_high = mp->mt_high; */
	    zot->mt_low = newp->mt_high+1;
	    mp->mt_high = newp->mt_low-1;
	    /* mp, newp, zot */
	    /* zot->mt_forw = mp->mt_forw; */
	    newp->mt_forw = zot;
	    mp->mt_forw = newp;
	    _db_mater_print(mp);
	    _db_mater_print(newp);
	    _db_mater_print(zot);
	    return;
	}
	if (mp->mt_high > newp->mt_low) {
	    /* Overlap to the left: Shorten preceding entry */
	    bu_log("Shortening region-id based material property entry lhs range, from:\n");
	    _db_mater_print(mp);
	    bu_log("to:\n");
	    mp->mt_high = newp->mt_low-1;
	    _db_mater_print(mp);
	    /* Now append */
	    newp->mt_forw = mp->mt_forw;
	    mp->mt_forw = newp;
	    _rt_check_overlap(newp);
	    return;
	}
	if (mp->mt_forw == MATER_NULL ||
	    newp->mt_low < mp->mt_forw->mt_low) {
	    /* Append */
	    newp->mt_forw = mp->mt_forw;
	    mp->mt_forw = newp;
	    _rt_check_overlap(newp);
	    return;
	}
    }
    bu_log("fell out of db_mater_insert loop, append region-id based material property entry to end of list\n");
    /* Append at end */
    newp->mt_forw = MATER_NULL;
    mp->mt_forw = newp;
    return;
}


/**
 * Called from db_scan() when initially scanning a v4 database.
 */
void
db_mater_add(struct db_i *dbip, int low, int hi, int r, int g, int b, b_off_t addr)
{
    register struct mater *mp;

    BU_ALLOC(mp, struct mater);
    mp->mt_low = low;
    mp->mt_high = hi;
    mp->mt_r = r;
    mp->mt_g = g;
    mp->mt_b = b;
    mp->mt_daddr = addr;
    db_mater_insert(dbip, mp);
}


void
db_mater_color_region(struct db_i *dbip, register struct region *regp)
{
    register struct mater *mp;

    if (regp == REGION_NULL) {
	bu_log("db_mater_color_region(NULL)\n");
	return;
    }
    if (!dbip)
	return;
    RT_CK_DBI(dbip);
    for (mp = dbip->i->material_head; mp != MATER_NULL; mp = mp->mt_forw) {
	if (regp->reg_regionid <= mp->mt_high &&
	    regp->reg_regionid >= mp->mt_low) {
	    regp->reg_mater.ma_color_valid = 1;
	    regp->reg_mater.ma_color[0] =
		(((double)mp->mt_r)+0.5) / 255.0;
	    regp->reg_mater.ma_color[1] =
		(((double)mp->mt_g)+0.5) / 255.0;
	    regp->reg_mater.ma_color[2] =
		(((double)mp->mt_b)+0.5) / 255.0;
	    return;
	}
    }
}


void
db_mater_to_vls(struct bu_vls *str, struct db_i *dbip)
{
    struct mater *mp;

    BU_CK_VLS(str);
    RT_CK_DBI(dbip);

    for (mp = dbip->i->material_head; mp != MATER_NULL; mp = mp->mt_forw) {
	bu_vls_printf(str,
		      "{%ld %ld %d %d %d} ",
		      mp->mt_low,
		      mp->mt_high,
		      mp->mt_r,
		      mp->mt_g,
		      mp->mt_b);
    }
}


struct mater *
db_mater_head(struct db_i *dbip)
{
    RT_CK_DBI(dbip);
    return dbip->i->material_head;
}


void
db_mater_set_head(struct db_i *dbip, struct mater *newmat)
{
    RT_CK_DBI(dbip);
    dbip->i->material_head = newmat;
}


struct mater *
db_mater_dup(struct db_i *dbip)
{
    register struct mater *mp = NULL;
    register struct mater *newmp = NULL;
    struct mater *newmater = NULL;
    struct mater *dupmater = NULL;

    RT_CK_DBI(dbip);

    mp = dbip->i->material_head;
    while (mp != MATER_NULL) {
	BU_ALLOC(newmater, struct mater);
	*newmater = *mp; /* struct copy */
	newmater->mt_forw = MATER_NULL;

	if (dupmater == NULL) {
	    dupmater = newmater;
	} else {
	    newmp->mt_forw = newmater;
	}

	newmp = newmater;
	mp = mp->mt_forw;
    }

    return dupmater;
}


void
db_mater_free(struct db_i *dbip)
{
    register struct mater *mp;

    RT_CK_DBI(dbip);

    while ((mp = dbip->i->material_head) != MATER_NULL) {
	dbip->i->material_head = mp->mt_forw;	/* Dequeue 'mp' */
	bu_free((char *)mp, "getstruct mater");
	mp = MATER_NULL;
    }
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
