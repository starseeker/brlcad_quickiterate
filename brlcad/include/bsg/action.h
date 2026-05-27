/*                       A C T I O N . H
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
 * BSG action framework — typed visitor-based tree traversals (Phase 5).
 *
 * An action encapsulates a single well-defined operation over a BSG
 * sub-tree.  Each action kind maps to a specific depth-first traversal
 * implemented via bsg_visit().  Callers allocate an action, execute it
 * against a root node, and then read the result fields.
 *
 * Built-in action kinds:
 *   BSG_ACTION_BBOX    — compute aggregate bounding box of all shape nodes.
 *   BSG_ACTION_COLLECT — harvest all nodes matching a type-mask into a ptbl.
 *   BSG_ACTION_EXPORT  — copy s_vlist data from all shape leaves into a vlist.
 */
/** @{ */
/* @file bsg/action.h */

#ifndef BSG_ACTION_H
#define BSG_ACTION_H

#include "common.h"
#include "vmath.h"
#include "bu/ptbl.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/** Action kind constants */
#define BSG_ACTION_BBOX     1  /**< @brief compute subtree bounding box */
#define BSG_ACTION_COLLECT  2  /**< @brief collect nodes matching a type mask */
#define BSG_ACTION_EXPORT   3  /**< @brief export s_vlist data from shape leaves */

/**
 * BSG action descriptor.
 *
 * Allocate with bsg_action_create(), release with bsg_action_destroy().
 * After bsg_action_execute() the result fields of the matching union
 * member are populated.
 */
struct bsg_action {
    int kind;                   /**< @brief BSG_ACTION_* constant */

    /** Per-kind input parameters */
    union {
	/** BSG_ACTION_COLLECT: only visit nodes that match this mask.
	 *  Set to 0 to collect all nodes. */
	unsigned long long collect_mask;
    } params;

    /** Per-kind result storage (populated by bsg_action_execute) */
    union {
	/** BSG_ACTION_BBOX result */
	struct {
	    point_t bmin;       /**< @brief subtree bounding-box minimum */
	    point_t bmax;       /**< @brief subtree bounding-box maximum */
	    int     valid;      /**< @brief non-zero when at least one shape was found */
	} bbox;

	/** BSG_ACTION_COLLECT result: caller must call bu_ptbl_free when done */
	struct bu_ptbl nodes;

	/** BSG_ACTION_EXPORT result: vlist head; caller manages vlfree */
	struct bu_list vlist;
    } result;
};

/**
 * Allocate and initialise a new action of kind @p kind.
 * Returns NULL on allocation failure.
 * For BSG_ACTION_COLLECT, set action->params.collect_mask before calling
 * bsg_action_execute().
 */
BSG_EXPORT extern struct bsg_action *
bsg_action_create(int kind);

/**
 * Release an action previously created by bsg_action_create().
 * For BSG_ACTION_COLLECT the internal ptbl is freed; for BSG_ACTION_EXPORT
 * the vlist entries are freed via @p vlfree (may be NULL, in which case
 * vlist entries are freed via bu_free).
 * No-op if @p action is NULL.
 */
BSG_EXPORT extern void
bsg_action_destroy(struct bsg_action *action, struct bu_list *vlfree);

/**
 * Execute @p action starting at @p root.
 * @p vlfree is used only for BSG_ACTION_EXPORT to allocate vlist entries;
 * it may be NULL to use bu_malloc directly.
 * No-op if @p action or @p root is NULL.
 */
BSG_EXPORT extern void
bsg_action_execute(struct bsg_action *action,
		   bsg_node          *root,
		   struct bu_list    *vlfree);

__END_DECLS

#endif /* BSG_ACTION_H */

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
