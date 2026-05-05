/*                       V I S I T . H
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
/** @addtogroup bsg
 *
 * @brief
 * BSG depth-first visitor (Phase 7 / Layer A / plan §B9).
 *
 * Replaces the ad-hoc BU_LIST_FOR / foreach_solid / foreach_group
 * idioms with a single DFS traversal parameterized by a type-flag
 * predicate and a caller callback.
 */
/** @{ */
/* @file bsg/visit.h */

#ifndef BSG_VISIT_H
#define BSG_VISIT_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * Depth-first traversal of the BSG tree rooted at @p root.
 *
 * For every node whose (@p type_mask == 0 || (node->s_type_flags & @p
 * type_mask) != 0), call @p cb(node, userdata).  Return 0 from @p cb
 * to stop traversal early.  @p type_mask == 0 visits every node.
 *
 * Children of a node are visited in ptbl order (insertion order).
 * The callback is invoked on a parent before its children.
 */
BSG_EXPORT extern void
bsg_visit(bsg_node *root,
	  unsigned long long type_mask,
	  int (*cb)(bsg_node *node, void *userdata),
	  void *userdata);

__END_DECLS

#endif /* BSG_VISIT_H */

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
