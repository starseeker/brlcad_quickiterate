/*                   A P P E A R A N C E . H
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
/** @addtogroup libbsg
 *
 * @brief
 * Node appearance properties independent of material.
 */
/** @{ */
/* @file bsg/appearance.h */

#ifndef BSG_APPEARANCE_H
#define BSG_APPEARANCE_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

BSG_EXPORT extern void
bsg_appearance_set_visible(bsg_node *node, int visible);

BSG_EXPORT extern int
bsg_appearance_visible(const bsg_node *node);

BSG_EXPORT extern void
bsg_appearance_set_force_draw(bsg_node *node, int force_draw);

BSG_EXPORT extern int
bsg_appearance_force_draw(const bsg_node *node);

BSG_EXPORT extern void
bsg_appearance_set_line_style(bsg_node *node, int dashed);

BSG_EXPORT extern int
bsg_appearance_line_style(const bsg_node *node);

BSG_EXPORT extern void
bsg_appearance_set_line_width(bsg_node *node, int line_width);

BSG_EXPORT extern int
bsg_appearance_line_width(const bsg_node *node);

__END_DECLS

#endif /* BSG_APPEARANCE_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
