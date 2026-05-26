/*                       F I E L D . H
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
 * Field accessor and notification API (SoField analogue).
 *
 * Each typed accessor mutates the corresponding bsg_node field and then
 * fires any registered FieldSensors or NodeSensors watching that node.
 */
/** @{ */
/* @file bsg/field.h */

#ifndef BSG_FIELD_H
#define BSG_FIELD_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * Identifies which field on a node was modified.  Used by FieldSensors to
 * subscribe to a specific field and by bsg_node_field_touch() to dispatch.
 */
typedef enum {
    BSG_FIELD_UNKNOWN    = 0,
    BSG_FIELD_FLAG       = 1,  /**< @brief s_flag (UP/DOWN) */
    BSG_FIELD_COLOR      = 2,  /**< @brief s_color[3] */
    BSG_FIELD_VISIBILITY = 3,  /**< @brief alias for s_flag UP/DOWN */
    BSG_FIELD_TRANSFORM  = 4,  /**< @brief s_mat */
    BSG_FIELD_CHILDREN   = 5   /**< @brief children ptbl modified */
} bsg_field_id_t;

/**
 * Notify sensors that field @p fid on node @p n has changed.
 * Iterates the global sensor registry and fires callbacks for all
 * FieldSensors watching (n, fid) and all NodeSensors watching n.
 * No-op when @p n is NULL.
 */
BSG_EXPORT extern void
bsg_node_field_touch(bsg_node *n, bsg_field_id_t fid);

/**
 * Set the visibility flag (s_flag) on @p n to @p flag and fire
 * BSG_FIELD_FLAG notifications.  No-op if @p n is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_flag(bsg_node *n, int flag);

/**
 * Return the current s_flag value from @p n, or 0 if @p n is NULL.
 */
BSG_EXPORT extern int
bsg_node_get_flag(const bsg_node *n);

/**
 * Set the RGB colour (s_color[0..2]) on @p n and fire BSG_FIELD_COLOR
 * notifications.  No-op if @p n is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_color(bsg_node *n,
		   unsigned char r,
		   unsigned char g,
		   unsigned char b);

/**
 * Copy the current s_color values into @p *r, @p *g, @p *b.
 * NULL output pointers are silently skipped.
 * No-op if @p n is NULL.
 */
BSG_EXPORT extern void
bsg_node_get_color(const bsg_node *n,
		   unsigned char *r,
		   unsigned char *g,
		   unsigned char *b);

/**
 * Set visibility: @p on non-zero → s_flag = UP, zero → s_flag = DOWN.
 * Fires BSG_FIELD_VISIBILITY notifications.  No-op if @p n is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_visible(bsg_node *n, int on);

/**
 * Return non-zero when the node is visible (s_flag == UP), zero otherwise.
 * Returns 0 if @p n is NULL.
 */
BSG_EXPORT extern int
bsg_node_get_visible(const bsg_node *n);

__END_DECLS

#endif /* BSG_FIELD_H */

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
