/*                     M A T E R I A L . H
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
 * Node material properties (color and material revision stamp).
 */
/** @{ */
/* @file bsg/material.h */

#ifndef BSG_MATERIAL_H
#define BSG_MATERIAL_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

BSG_EXPORT extern void
bsg_material_set_rgb(bsg_node *node, unsigned char r, unsigned char g, unsigned char b);

BSG_EXPORT extern void
bsg_material_get_rgb(const bsg_node *node, unsigned char *r, unsigned char *g, unsigned char *b);

BSG_EXPORT extern void
bsg_material_set_revision(bsg_node *node, uint32_t revision);

BSG_EXPORT extern uint32_t
bsg_material_revision(const bsg_node *node);

__END_DECLS

#endif /* BSG_MATERIAL_H */

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
