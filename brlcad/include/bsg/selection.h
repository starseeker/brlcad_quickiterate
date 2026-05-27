/*                   S E L E C T I O N . H
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
 * First-class selection model for BSG scene nodes (Phase 6).
 *
 * A `bsg_selection` is an opaque set of `bsg_node *` pointers backed by
 * a `bu_ptbl`.  It replaces direct manipulation of `s_iflag` for
 * selection-state tracking.
 *
 * bsg_selection_highlight() / bsg_selection_unhighlight() provide a
 * compatibility bridge that mirrors the selection state into both the modern
 * appearance highlight state and the legacy `s_iflag` bits.
 */
/** @{ */
/* @file bsg/selection.h */

#ifndef BSG_SELECTION_H
#define BSG_SELECTION_H

#include "common.h"
#include "bu/ptbl.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/** Opaque selection-set handle */
struct bsg_selection;

/**
 * Allocate and initialise an empty selection set.
 * Returns NULL on allocation failure.
 */
BSG_EXPORT extern struct bsg_selection *
bsg_selection_create(void);

/**
 * Release a selection set.
 * Nodes referenced by the set are NOT destroyed; only the set itself is freed.
 * No-op if @p sel is NULL.
 */
BSG_EXPORT extern void
bsg_selection_destroy(struct bsg_selection *sel);

/**
 * Add @p node to @p sel.  Duplicate insertions are silently ignored.
 * No-op if either argument is NULL.
 */
BSG_EXPORT extern void
bsg_selection_add(struct bsg_selection *sel, bsg_node *node);

/**
 * Remove @p node from @p sel.  No-op if @p node is not in the set.
 * No-op if either argument is NULL.
 */
BSG_EXPORT extern void
bsg_selection_remove(struct bsg_selection *sel, bsg_node *node);

/**
 * Remove all nodes from @p sel.
 * No-op if @p sel is NULL.
 */
BSG_EXPORT extern void
bsg_selection_clear(struct bsg_selection *sel);

/**
 * Return non-zero if @p node is currently in @p sel.
 * Returns 0 if either argument is NULL.
 */
BSG_EXPORT extern int
bsg_selection_contains(const struct bsg_selection *sel,
		       const bsg_node              *node);

/**
 * Return the number of nodes currently in @p sel.
 * Returns 0 if @p sel is NULL.
 */
BSG_EXPORT extern size_t
bsg_selection_count(const struct bsg_selection *sel);

/**
 * Return a read-only pointer to the internal bu_ptbl of selected nodes.
 * Callers must not modify the table contents directly.
 * Returns NULL if @p sel is NULL.
 */
BSG_EXPORT extern const struct bu_ptbl *
bsg_selection_nodes(const struct bsg_selection *sel);

/**
 * Mark every node currently in @p sel as highlighted in both appearance state
 * and the legacy `s_iflag` compatibility bit.
 * No-op if @p sel is NULL.
 */
BSG_EXPORT extern void
bsg_selection_highlight(struct bsg_selection *sel);

/**
 * Clear highlight state on every node currently in @p sel in both appearance
 * state and the legacy `s_iflag` compatibility bit.
 * No-op if @p sel is NULL.
 */
BSG_EXPORT extern void
bsg_selection_unhighlight(struct bsg_selection *sel);

__END_DECLS

#endif /* BSG_SELECTION_H */

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
