/*                    S E L E C T I O N . H
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
 * Phase 6 selection API for BSG nodes.
 *
 * Selection is a first-class scene-level concept.  A named
 * bsg_selection_set holds zero or more bsg_selection_entry records.
 * Multiple sets may coexist on a scene root (e.g. "active", "hover",
 * "edit").  Selection entries carry whole-object, instance, part, or
 * sub-primitive selection details.
 *
 * The s_iflag UP/DOWN illumination state is maintained for
 * compatibility; callers should prefer the BSG selection accessors
 * when writing new code.
 */
/** @{ */
/* @file bsg/selection.h */

#ifndef BSG_SELECTION_H
#define BSG_SELECTION_H

#include "common.h"

#include "vmath.h"
#include "bsg/defines.h"
#include "bsg/identity.h"

__BEGIN_DECLS

/**
 * Selection mutation policy applied by bsg_selection_apply_policy().
 */
enum bsg_selection_policy {
    BSG_SELECTION_POLICY_SINGLE  = 0, /**< @brief Clear set, add entry (single-select) */
    BSG_SELECTION_POLICY_TOGGLE  = 1, /**< @brief Remove if present, add if absent */
    BSG_SELECTION_POLICY_APPEND  = 2, /**< @brief Add if not already present */
    BSG_SELECTION_POLICY_REMOVE  = 3, /**< @brief Remove if present, no-op otherwise */
    BSG_SELECTION_POLICY_REPLACE = 4  /**< @brief Clear entire set and add entry */
};

/**
 * Granularity of a selection entry.
 */
enum bsg_selection_kind {
    BSG_SELECTION_NODE               = 0, /**< @brief Whole node */
    BSG_SELECTION_INSTANCE           = 1, /**< @brief Specific path instance */
    BSG_SELECTION_PART               = 2, /**< @brief Named part */
    BSG_SELECTION_EDGE               = 3, /**< @brief Edge sub-primitive */
    BSG_SELECTION_FACE               = 4, /**< @brief Face sub-primitive */
    BSG_SELECTION_VERTEX             = 5, /**< @brief Vertex sub-primitive */
    BSG_SELECTION_POINT              = 6, /**< @brief Pick point */
    BSG_SELECTION_PRIMITIVE_SPECIFIC = 7  /**< @brief Primitive-specific */
};

/**
 * Pick details for entries selected by a pick/ray operation.
 */
struct bsg_pick_detail {
    point_t pick_pt;   /**< @brief World-space pick point */
    vect_t  pick_dir;  /**< @brief Pick ray direction (unit vector) */
    fastf_t depth;     /**< @brief Depth along ray to selected point */
};

/**
 * One record in a bsg_selection_set.  Multiple entries in the same set
 * may share a node pointer (e.g. separate face or edge sub-selections).
 */
struct bsg_selection_entry {
    bsg_node               *node;        /**< @brief Node pointer; may be NULL for path-only entries */
    struct bsg_node_id      node_id;     /**< @brief Node identity */
    struct bsg_part_id      part_id;     /**< @brief Part identity (0 = whole node) */
    struct bsg_instance_id  instance_id; /**< @brief Instance identity (0 = any) */
    char                   *src_path;   /**< @brief Owned copy of source DB path; may be NULL */
    enum bsg_selection_kind kind;
    int                     have_pick;   /**< @brief Non-zero when pick detail is valid */
    struct bsg_pick_detail  pick;
    void                   *app_tag;    /**< @brief Application-specific tag; not freed by BSG */
};

/**
 * A named, ordered set of bsg_selection_entry records.
 * Do not access _priv directly; use the BSG selection API.
 */
struct bsg_selection_set {
    char   *name;   /**< @brief Owned set name string */
    size_t  count;  /**< @brief Current entry count */
    void   *_priv;  /**< @brief Internal linked-list head; do not use directly */
};


/* ---------------------------------------------------------------------- */
/* Selection set lifecycle                                                  */
/* ---------------------------------------------------------------------- */

/**
 * Allocate a new, empty selection set with the given @p name.
 * The caller owns the returned pointer; free with bsg_selection_set_destroy().
 * Returns NULL on allocation failure.
 */
BSG_EXPORT extern struct bsg_selection_set *
bsg_selection_set_create(const char *name);

/**
 * Destroy @p ss and free all memory associated with it, including the
 * owned src_path copies inside each entry.  No-op if @p ss is NULL.
 */
BSG_EXPORT extern void
bsg_selection_set_destroy(struct bsg_selection_set *ss);

/**
 * Remove all entries from @p ss without destroying the set itself.
 * No-op if @p ss is NULL.
 */
BSG_EXPORT extern void
bsg_selection_clear(struct bsg_selection_set *ss);


/* ---------------------------------------------------------------------- */
/* Entry operations                                                         */
/* ---------------------------------------------------------------------- */

/**
 * Add a copy of @p e to @p ss.  src_path is duplicated.  If an entry
 * for the same node (and same kind when @p e->node is non-NULL) already
 * exists the duplicate is silently skipped.  Fires BSG_FIELD_SELECTION
 * on @p e->node when non-NULL.
 *
 * Returns 1 if the entry was added, 0 if it was already present or on
 * failure.
 */
BSG_EXPORT extern int
bsg_selection_add(struct bsg_selection_set *ss,
		  const struct bsg_selection_entry *e);

/**
 * Remove the entry whose node pointer matches @p node from @p ss.
 * Fires BSG_FIELD_SELECTION on @p node when non-NULL.
 * Returns 1 if an entry was removed, 0 if none matched.
 */
BSG_EXPORT extern int
bsg_selection_remove(struct bsg_selection_set *ss, const bsg_node *node);

/**
 * Return non-zero when @p ss contains an entry matching @p node.
 * Returns 0 if @p ss or @p node is NULL.
 */
BSG_EXPORT extern int
bsg_selection_contains(const struct bsg_selection_set *ss,
		       const bsg_node *node);

/**
 * Return the current number of entries in @p ss.  Returns 0 if NULL.
 */
BSG_EXPORT extern size_t
bsg_selection_count(const struct bsg_selection_set *ss);

/**
 * Visitor callback type for bsg_selection_visit().
 * Return 0 to stop traversal early, non-zero to continue.
 */
typedef int (*bsg_selection_visit_fn)(const struct bsg_selection_entry *,
				      void *);

/**
 * Call @p cb for each entry in @p ss in insertion order.  Stops early
 * if @p cb returns 0.  No-op if @p ss or @p cb is NULL.
 */
BSG_EXPORT extern void
bsg_selection_visit(const struct bsg_selection_set *ss,
		    bsg_selection_visit_fn cb,
		    void *data);

/**
 * Apply @p policy to @p ss using @p e as the new entry.
 *
 * - SINGLE / REPLACE: clears @p ss, then adds @p e.
 * - APPEND: adds @p e only when not already present.
 * - REMOVE: removes the entry matching @p e->node; @p e itself is not added.
 * - TOGGLE: removes if present, adds if absent.
 *
 * No-op if @p ss or @p e is NULL.
 */
BSG_EXPORT extern void
bsg_selection_apply_policy(struct bsg_selection_set *ss,
			   const struct bsg_selection_entry *e,
			   enum bsg_selection_policy policy);


/* ---------------------------------------------------------------------- */
/* Scene-root named-set registry (Phase 6B)                                */
/* ---------------------------------------------------------------------- */

/**
 * Look up the named selection set registered on @p root.  When @p create
 * is non-zero the set is created if not present.  Returns NULL when the
 * set does not exist and @p create is 0, or on failure.
 */
BSG_EXPORT extern struct bsg_selection_set *
bsg_scene_selection_get(bsg_node *root, const char *name, int create);

/**
 * Convenience: add or remove @p node from the named selection set on
 * @p root.  When @p selected is non-zero the node is added (APPEND
 * policy); when zero it is removed.  Creates the set when needed.
 * No-op if @p root or @p node is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_selected(bsg_node *root, bsg_node *node,
		      const char *set_name, int selected);

/**
 * Return non-zero when @p node appears in the named selection set on
 * @p root.  Returns 0 if any argument is NULL or the set does not
 * exist.
 */
BSG_EXPORT extern int
bsg_node_is_selected(const bsg_node *root, const bsg_node *node,
		     const char *set_name);


/* ---------------------------------------------------------------------- */
/* Compatibility sync helpers                                               */
/* ---------------------------------------------------------------------- */

/**
 * Walk @p root's subtree and set s_iflag = UP on every node in the
 * "active" selection set and s_iflag = DOWN on every node NOT in it.
 * No-op if @p root is NULL.
 */
BSG_EXPORT extern void
bsg_selection_sync_illum_flags(bsg_node *root);

/**
 * Walk @p root's subtree and populate the "active" selection set from
 * nodes whose s_iflag == UP.  The existing "active" set is cleared
 * first.  No-op if @p root is NULL.
 */
BSG_EXPORT extern void
bsg_selection_from_illum_flags(bsg_node *root);

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
