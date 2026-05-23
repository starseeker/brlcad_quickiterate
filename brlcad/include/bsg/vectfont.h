/*                        V E C T F O N T . H
 * BRL-CAD
 *
 * Copyright (c) 2004-2026 United States Government as represented by
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

#ifndef BSG_VECTFONT_H
#define BSG_VECTFONT_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/** @addtogroup bsg_vfont
 *
 *  @brief
 *  Terminal Independent Graphics Display Package.
 *
 *  Canonical home; bv/vectfont.h is a backward-compatibility bridge.
 *
 *  Vector font definitions, for TIG-PACK fonts.
 */
/** @{ */
/** @file bsg/vectfont.h */

/*
 *	Motion encoding macros
 *
 * All characters reference absolute points within a 10 x 10 square
 */
#define	brt(x, y)	(11*x+y)
#define drk(x, y)	-(11*x+y)
#define	VFONT_LAST	-128		/**< @brief  0200 Marks end of stroke list */
#define	NEGY		-127		/**< @brief  0201 Denotes negative y stroke */
#define bneg(x, y)	NEGY, brt(x, y)
#define dneg(x, y)	NEGY, drk(x, y)

BV_EXPORT extern int *tp_getchar(const unsigned char *c);

/** @} */

__END_DECLS

#endif  /* BSG_VECTFONT_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
