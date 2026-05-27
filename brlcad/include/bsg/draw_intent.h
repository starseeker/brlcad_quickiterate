/*                  D R A W _ I N T E N T . H
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
 * Phase D2 (drawing_modernization):
 * Explicit draw-intent metadata attached to BSG scene groups.
 *
 * When a database draw command (e.g. "draw comb/a") creates a scene group
 * in the BSG draw tree, a @c bsg_draw_intent record is attached to that
 * group via @c bsg_node_set_draw_intent().  The intent records the source
 * path that was drawn, the draw mode (wireframe/shaded/hidden-line/etc.),
 * the LoD policy, and whether the group is a synthetic overlay container.
 *
 * This replaces the old convention of:
 *   - inferring the drawn path from @c group->s_name
 *   - detecting overlay groups by comparing @c s_name to "_overlays"
 *   - discovering draw mode by walking shape descendants
 *
 * Draw/erase semantics are now queryable through the intent API rather than
 * reconstructed from group naming conventions and child structure.
 *
 * Per drawing_modernization.txt Phase D2 exit criteria:
 *   - Database draw/erase semantics are explicit scene metadata.
 *   - Export/raytrace/report commands do not reconstruct command intent
 *     from child tables or raw node fields.
 */
/** @{ */
/* @file bsg/draw_intent.h */

#ifndef BSG_DRAW_INTENT_H
#define BSG_DRAW_INTENT_H

#include "common.h"
#include "bu/ptbl.h"
#include "bu/vls.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/* -----------------------------------------------------------------------
 * Draw mode enumeration
 *
 * These values match the legacy _GED_WIREFRAME/_GED_SHADED_MODE_*
 * constants from ged_private.h and the bsg_obj_settings::s_dmode field.
 * New code should use BSG_DRAW_MODE_* names rather than the raw integers.
 * ----------------------------------------------------------------------- */

/**
 * Rendering mode requested by the draw command.
 *
 * Numeric values are intentionally identical to the legacy s_dmode
 * field values (0–6) so that existing renderers can cast freely
 * between the two representations during the migration.
 */
typedef enum {
    BSG_DRAW_MODE_WIRE         = 0,  /**< wireframe only */
    BSG_DRAW_MODE_SHADED_BOTS  = 1,  /**< shaded BOTs/polysolids; CSG NOT evaluated */
    BSG_DRAW_MODE_SHADED       = 2,  /**< shaded; CSG NOT evaluated */
    BSG_DRAW_MODE_SHADED_EVAL  = 3,  /**< shaded; CSG evaluated */
    BSG_DRAW_MODE_HIDDEN_LINE  = 4,  /**< hidden-line removal */
    BSG_DRAW_MODE_WIRE_EVAL    = 6   /**< wireframe with CSG evaluation */
} bsg_draw_mode;

/* -----------------------------------------------------------------------
 * LoD (Level-of-Detail) policy
 * ----------------------------------------------------------------------- */

/**
 * Level-of-detail policy for the drawn group.
 */
typedef enum {
    BSG_LOD_AUTO  = 0,  /**< automatic (let the renderer decide) */
    BSG_LOD_FORCE = 1,  /**< force LoD mesh even at close range */
    BSG_LOD_FULL  = 2,  /**< force full-detail tessellation */
    BSG_LOD_OFF   = 3   /**< LoD disabled (always draw full detail) */
} bsg_lod_policy;

/* -----------------------------------------------------------------------
 * bsg_draw_intent
 *
 * Lifecycle: bsg_draw_intent_create() / bsg_draw_intent_free().
 * Ownership: a bsg_node owns its bsg_draw_intent; the node frees the
 *   intent during bsg_obj_reset().
 * ----------------------------------------------------------------------- */

/**
 * Draw-intent metadata record.
 *
 * Attached to BSG scene groups by the draw command when a database path
 * is drawn.  Synthetic groups (the overlay container, etc.) are marked
 * with @c di_overlay = 1.
 *
 * Fields:
 *   di_path     — source path string as supplied to the draw command
 *                 (e.g. "comb/a").  Never NULL on valid intents.
 *   di_mode     — draw mode selected for this group.
 *   di_lod      — LoD policy for this group.
 *   di_mixed    — non-zero when mixed-mode drawing is enabled
 *                 (do not evict objects drawn in a different mode).
 *   di_overlay  — non-zero when this is a synthetic overlay container
 *                 (the "_overlays" group or similar) rather than a real
 *                 database-path draw.
 */
struct bsg_draw_intent {
    struct bu_vls  di_path;    /**< @brief drawn source path string */
    bsg_draw_mode  di_mode;    /**< @brief draw mode */
    bsg_lod_policy di_lod;     /**< @brief LoD policy */
    int            di_mixed;   /**< @brief mixed-mode flag */
    int            di_overlay; /**< @brief 1 = synthetic overlay container */
};

/* -----------------------------------------------------------------------
 * Lifecycle
 * ----------------------------------------------------------------------- */

/**
 * Allocate and initialize a draw-intent for a database draw command.
 *
 * @p path    Source path string (copied into @c di_path).
 * @p mode    Draw mode.
 *
 * Returns NULL on allocation failure.  The caller should bind the intent
 * to a node via bsg_node_set_draw_intent().
 */
BSG_EXPORT extern struct bsg_draw_intent *
bsg_draw_intent_create(const char *path, bsg_draw_mode mode);

/**
 * Allocate and initialize an overlay draw-intent.
 *
 * Used by bsg_ensure_overlay_group() to mark the "_overlays" container.
 * The @c di_overlay flag is set; @c di_path is set to @p name.
 *
 * Returns NULL on allocation failure.
 */
BSG_EXPORT extern struct bsg_draw_intent *
bsg_draw_intent_create_overlay(const char *name);

/**
 * Free a draw-intent allocated by bsg_draw_intent_create() or
 * bsg_draw_intent_create_overlay().
 *
 * No-op if @p di is NULL.
 */
BSG_EXPORT extern void
bsg_draw_intent_free(struct bsg_draw_intent *di);

/* -----------------------------------------------------------------------
 * Node binding
 * ----------------------------------------------------------------------- */

/**
 * Attach @p di to @p node, transferring ownership.
 *
 * Any previously attached intent is freed.  No-op if @p node is NULL.
 * If @p di is NULL the existing intent (if any) is freed and cleared.
 */
BSG_EXPORT extern void
bsg_node_set_draw_intent(bsg_node *node, struct bsg_draw_intent *di);

/**
 * Return the draw-intent attached to @p node, or NULL.
 */
BSG_EXPORT extern struct bsg_draw_intent *
bsg_node_get_draw_intent(const bsg_node *node);

/* -----------------------------------------------------------------------
 * Accessors
 * ----------------------------------------------------------------------- */

/**
 * Return the source path string from @p di (never NULL for valid intents).
 *
 * Returns NULL if @p di is NULL.
 */
BSG_EXPORT extern const char *
bsg_draw_intent_path(const struct bsg_draw_intent *di);

/**
 * Return the draw mode recorded in @p di.
 *
 * Returns BSG_DRAW_MODE_WIRE (0) if @p di is NULL.
 */
BSG_EXPORT extern bsg_draw_mode
bsg_draw_intent_mode(const struct bsg_draw_intent *di);

/**
 * Return the LoD policy recorded in @p di.
 *
 * Returns BSG_LOD_AUTO (0) if @p di is NULL.
 */
BSG_EXPORT extern bsg_lod_policy
bsg_draw_intent_lod(const struct bsg_draw_intent *di);

/**
 * Return non-zero if @p di is a synthetic overlay intent
 * (di_overlay == 1), zero for real database-path intents.
 *
 * Returns 0 if @p di is NULL.
 */
BSG_EXPORT extern int
bsg_draw_intent_is_overlay(const struct bsg_draw_intent *di);

/* -----------------------------------------------------------------------
 * Scene-level actions
 * ----------------------------------------------------------------------- */

/**
 * Walk the direct children of @p root and find the first group whose
 * draw-intent path matches @p path (exact string match after stripping
 * any leading '/').
 *
 * Returns the matching group node, or NULL if not found.
 * @p root may be the draw root (BSG_NODE_GROUP/ROOT) or any group.
 */
BSG_EXPORT extern bsg_node *
bsg_draw_intent_find(bsg_node *root, const char *path);

/**
 * Collect all direct children of @p root that carry draw intents into
 * the caller-supplied @p groups table.
 *
 * When @p include_overlays is zero, groups with @c di_overlay == 1 are
 * skipped (the common case for draw/raytrace/report consumers).  When
 * @p include_overlays is non-zero all groups are included.
 *
 * Groups without any draw intent attached are also included when
 * @p include_overlays is non-zero; they are always skipped otherwise.
 *
 * @p groups must be initialised by the caller (BU_PTBL_INIT_ZERO or
 * bu_ptbl_init()).  The caller is responsible for bu_ptbl_free().
 *
 * @p root may be NULL (no-op).
 */
BSG_EXPORT extern void
bsg_collect_draw_groups(bsg_node *root, struct bu_ptbl *groups,
			int include_overlays);

__END_DECLS

#endif /* BSG_DRAW_INTENT_H */

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
