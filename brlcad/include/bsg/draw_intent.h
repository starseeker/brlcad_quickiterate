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

struct bsg_view; /* forward declaration */

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
 * Update the source path stored in @p di.
 *
 * No-op if @p di is NULL.
 */
BSG_EXPORT extern void
bsg_draw_intent_set_path(struct bsg_draw_intent *di, const char *path);

/**
 * Return the draw mode recorded in @p di.
 *
 * Returns BSG_DRAW_MODE_WIRE (0) if @p di is NULL.
 */
BSG_EXPORT extern bsg_draw_mode
bsg_draw_intent_mode(const struct bsg_draw_intent *di);

/**
 * Update the draw mode stored in @p di.
 *
 * No-op if @p di is NULL.
 */
BSG_EXPORT extern void
bsg_draw_intent_set_mode(struct bsg_draw_intent *di, bsg_draw_mode mode);

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
 * Database event (D2)
 *
 * Describes a database I/O change that may affect drawn intents.
 * Produced by libged after every modifying db_io call and consumed by
 * bsg_draw_intent_revalidate().
 * ----------------------------------------------------------------------- */

/** Kind of database change that triggered the event. */
typedef enum {
    BSG_DB_EVENT_MODIFIED = 0,  /**< object contents changed */
    BSG_DB_EVENT_REMOVED  = 1,  /**< object deleted from database */
    BSG_DB_EVENT_RENAMED  = 2   /**< object moved/renamed (old → new path) */
} bsg_db_event_kind;

/**
 * Lightweight value-type describing a single database change.
 *
 * For BSG_DB_EVENT_RENAMED, @c dbe_path holds the *new* full path and
 * @c dbe_old_path holds the previous name.  For other kinds @c dbe_old_path
 * is unused (set to NULL / empty).
 *
 * Callers may stack-allocate this struct; bsg_draw_intent_revalidate()
 * does not retain a reference after it returns.
 */
struct bsg_db_event {
    const char       *dbe_path;      /**< affected full path (new path for renames) */
    const char       *dbe_old_path;  /**< previous path (rename only, else NULL)    */
    bsg_db_event_kind dbe_kind;      /**< what happened                             */
};

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

/* -----------------------------------------------------------------------
 * D2 Action API
 *
 * The functions below are the primary draw-intent manipulation interface
 * for Phase D2 consumers.  Implementations live in libbsg/draw_intent.c.
 * ----------------------------------------------------------------------- */

/**
 * Re-evaluate every intent whose source path is affected by @p event.
 *
 * - BSG_DB_EVENT_MODIFIED: marks the matching intent's geometry as stale
 *   (sets a "needs-redraw" flag on the group node).  The group is NOT
 *   removed; callers must issue a new draw command or call the redraw
 *   path to regenerate geometry.
 * - BSG_DB_EVENT_REMOVED: calls bsg_draw_intent_erase_by_path() for the
 *   affected path, removing the group from the scene.
 * - BSG_DB_EVENT_RENAMED: updates @c di_path in the matching intent to
 *   the new name (@c dbe_path); the group is retained in the scene.
 *
 * No-op if @p root or @p event is NULL, or if no matching intent exists.
 *
 * @returns the number of intents affected (0 = no match).
 */
BSG_EXPORT extern int
bsg_draw_intent_revalidate(bsg_node *root, const struct bsg_db_event *event);

/**
 * Remove the first group under @p root whose intent path matches @p path
 * (same path-normalisation as bsg_draw_intent_find()).
 *
 * The matched group node is unlinked from its parent and freed.
 *
 * No-op if @p root or @p path is NULL, or if no matching group is found.
 *
 * @returns 1 if a group was erased, 0 otherwise.
 */
BSG_EXPORT extern int
bsg_draw_intent_erase_by_path(bsg_node *root, const char *path);

/**
 * Remove the group identified by the explicit @p intent pointer from
 * @p root's subtree.
 *
 * Walks root's direct children looking for a node whose @c di pointer
 * equals @p intent; when found the node is unlinked and freed.
 *
 * No-op if any argument is NULL or the intent is not found.
 *
 * @returns 1 if a group was erased, 0 otherwise.
 */
BSG_EXPORT extern int
bsg_draw_intent_erase(bsg_node *root, struct bsg_draw_intent *intent);

/**
 * Collapse redundant or subsumed intents under @p root.
 *
 * An intent I is *subsumed* by another intent J when J's source path
 * is a proper prefix of I's source path (e.g. "a" subsumes "a/b/c").
 * All subsumed groups are removed (unlinked and freed).
 *
 * Duplicate paths (same path drawn twice) are also collapsed: the
 * first occurrence is retained, subsequent ones are freed.
 *
 * No-op if @p root is NULL.
 *
 * @returns the number of groups removed.
 */
BSG_EXPORT extern int
bsg_draw_intent_simplify(bsg_node *root);

/**
 * Collect the intent-bearing groups under @p root that are visible from
 * view @p v into the caller-supplied @p out table.
 *
 * A group is considered visible when it is not hidden (BSG_NODE_HIDDEN
 * flag clear) and its @c s_v scope is either NULL (shared) or equals @p v.
 * Overlay groups are always excluded.
 *
 * @p out must be initialised by the caller.  The caller is responsible
 * for bu_ptbl_free().  @p v may be NULL (only scope-NULL groups included).
 *
 * No-op if @p root or @p out is NULL.
 */
BSG_EXPORT extern void
bsg_draw_intent_collect_visible(bsg_node *root, struct bu_ptbl *out,
				const struct bsg_view *v);

/**
 * Collect the intent-bearing groups under @p root that should contribute
 * to a geometry export (rt, nirt, plot, bot_dump, …) into @p out.
 *
 * @p flags is a bitmask that controls which groups are included:
 *   - BSG_EXPORT_INCLUDE_OVERLAYS (0x01) — include synthetic overlay groups.
 *   - BSG_EXPORT_SHADED_ONLY      (0x02) — only include groups whose draw
 *     mode is shaded or shaded-eval (exclude wireframe-only groups).
 *
 * @p out must be initialised by the caller.  The caller is responsible
 * for bu_ptbl_free().
 *
 * No-op if @p root or @p out is NULL.
 */
#define BSG_EXPORT_INCLUDE_OVERLAYS 0x01ULL  /**< include overlay groups */
#define BSG_EXPORT_SHADED_ONLY      0x02ULL  /**< shaded/eval groups only */

BSG_EXPORT extern void
bsg_draw_intent_collect_for_export(bsg_node *root, struct bu_ptbl *out,
				   unsigned long long flags);

/**
 * Collect all intent-bearing groups under @p root whose source path
 * matches the glob @p pattern into @p out.
 *
 * Pattern matching uses bu_path_match() semantics (POSIX fnmatch with
 * '/' as a component separator; use '*' for a single-component wildcard,
 * '**' is not specially handled by this implementation).
 *
 * @p out must be initialised by the caller.  The caller is responsible
 * for bu_ptbl_free().
 *
 * No-op if @p root, @p pattern, or @p out is NULL.
 */
BSG_EXPORT extern void
bsg_draw_intent_match(bsg_node *root, const char *pattern,
		      struct bu_ptbl *out);

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
