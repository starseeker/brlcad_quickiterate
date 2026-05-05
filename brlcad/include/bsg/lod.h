/*                          L O D . H
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
 * Level-of-detail helpers for the BSG scene graph.
 *
 * These functions operate on bsg_node trees and complement the existing
 * libbv LoD machinery (bv/lod.h).  In Phase 4 the implementations are
 * thin wrappers or stubs; fuller bodies land in Phase 5+.
 */
/** @{ */
/* @file bsg/lod.h */

#ifndef BSG_LOD_H
#define BSG_LOD_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * Update LoD levels for all BSG_NODE_LOD nodes in the subtree rooted
 * at @p root for view @p v.  A no-op when @p root is NULL.
 */
BSG_EXPORT extern void
bsg_lod_update(bsg_node *root, struct bview *v);

/**
 * Return non-zero if the bsg_node @p n requires a LoD update for the
 * current view parameters stored in @p v.
 */
BSG_EXPORT extern int
bsg_lod_stale(bsg_node *n, struct bview *v);

__END_DECLS

#endif /* BSG_LOD_H */

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
