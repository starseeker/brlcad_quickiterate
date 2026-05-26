/*                     I D E N T I T Y . H
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
 * Node identity and revision accessors.
 */
/** @{ */
/* @file bsg/identity.h */

#ifndef BSG_IDENTITY_H
#define BSG_IDENTITY_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

BSG_EXPORT extern void
bsg_node_identity_set_name(bsg_node *node, const char *name);

BSG_EXPORT extern const char *
bsg_node_identity_name(const bsg_node *node);

BSG_EXPORT extern void
bsg_node_identity_set_path(bsg_node *node, void *path_token);

BSG_EXPORT extern void *
bsg_node_identity_path(const bsg_node *node);

BSG_EXPORT extern void
bsg_node_identity_set_source(bsg_node *node, void *source_data);

BSG_EXPORT extern void *
bsg_node_identity_source(const bsg_node *node);

BSG_EXPORT extern uint64_t
bsg_node_revision_get(const bsg_node *node);

BSG_EXPORT extern void
bsg_node_revision_set(bsg_node *node, uint64_t revision);

BSG_EXPORT extern uint64_t
bsg_node_revision_bump(bsg_node *node);

__END_DECLS

#endif /* BSG_IDENTITY_H */

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
