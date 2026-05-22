/*                        T E X T . H
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
 * Slice 6 (bv_scene_obj_migrate): BSG text/label adornment payload API.
 *
 * A text payload stores a string, a 3-D origin point, an optional
 * rotation matrix, a character-size scale, and label-style decoration
 * options (anchor, optional line/arrow to a target point).  It is the
 * typed BSG replacement for ad-hoc @c bv_label data stored in
 * @c s_i_data on a @c bv_scene_obj.
 *
 * Usage sketch:
 * @code
 *   bsg_node *shape = bsg_shape_create(v);
 *   point_t origin = {0.0, 0.0, 0.0};
 *   struct bsg_payload *tp = bsg_payload_text_create("hello", origin, 1.0);
 *   bsg_node_payload_set(shape, tp);
 * @endcode
 *
 * To generate vlist wireframe geometry from the text (for renderers that
 * do not support direct text rendering):
 * @code
 *   struct bu_list vhead, vlfree;
 *   BU_LIST_INIT(&vhead);
 *   BU_LIST_INIT(&vlfree);
 *   bsg_payload_text_build_vlist(tp, &vhead, &vlfree);
 *   // vhead now contains bv_vlist commands for the stroked text
 * @endcode
 */
/** @{ */
/* @file bsg/text.h */

#ifndef BSG_TEXT_H
#define BSG_TEXT_H

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bu/vls.h"
#include "bsg/defines.h"
#include "bsg/payload.h"

__BEGIN_DECLS

/**
 * Anchor position constants for text/label payloads.
 * Identical in meaning to the @c BV_ANCHOR_* constants in bv/defines.h;
 * re-stated here so callers do not need to include bv headers.
 */
#define BSG_TEXT_ANCHOR_AUTO          0
#define BSG_TEXT_ANCHOR_BOTTOM_LEFT   1
#define BSG_TEXT_ANCHOR_BOTTOM_CENTER 2
#define BSG_TEXT_ANCHOR_BOTTOM_RIGHT  3
#define BSG_TEXT_ANCHOR_MIDDLE_LEFT   4
#define BSG_TEXT_ANCHOR_MIDDLE_CENTER 5
#define BSG_TEXT_ANCHOR_MIDDLE_RIGHT  6
#define BSG_TEXT_ANCHOR_TOP_LEFT      7
#define BSG_TEXT_ANCHOR_TOP_CENTER    8
#define BSG_TEXT_ANCHOR_TOP_RIGHT     9


/**
 * Create a BSG text/label payload.
 *
 * @p text    NUL-terminated string to display.  May be NULL or empty.
 * @p origin  3-D anchor point in model coordinates (lower-left of the
 *            first character).
 * @p scale   Character width in model-space units (mm).  Must be > 0.
 *
 * The payload stores its own copy of @p text.  The rotation matrix
 * defaults to the identity transform.  The anchor defaults to
 * BSG_TEXT_ANCHOR_AUTO.
 *
 * Returns NULL on allocation failure.
 * Caller must free with bsg_payload_destroy().
 */
BSG_EXPORT extern struct bsg_payload *
bsg_payload_text_create(const char *text, const point_t origin, double scale);


/**
 * Replace the string content of @p text_payload with @p text.
 *
 * No-op if @p text_payload is NULL or not of type BSG_PAYLOAD_TYPE_TEXT.
 */
BSG_EXPORT extern void
bsg_payload_text_set(struct bsg_payload *text_payload, const char *text);

/**
 * Return the NUL-terminated string stored in @p text_payload.
 *
 * The returned pointer is owned by the payload; do not free or modify it.
 * Returns NULL if @p text_payload is NULL, the wrong type, or the text
 * has not been set.
 */
BSG_EXPORT extern const char *
bsg_payload_text_get(const struct bsg_payload *text_payload);


/**
 * Set the 3-D origin point of @p text_payload.
 *
 * No-op if @p text_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_text_origin_set(struct bsg_payload *text_payload, const point_t origin);

/**
 * Copy the 3-D origin point from @p text_payload into @p out.
 *
 * No-op if either argument is NULL or @p text_payload is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_text_origin_get(const struct bsg_payload *text_payload, point_t out);


/**
 * Set the character-size scale (model-space units) of @p text_payload.
 *
 * Values <= 0 are ignored.
 * No-op if @p text_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_text_scale_set(struct bsg_payload *text_payload, double scale);

/**
 * Return the character-size scale of @p text_payload.
 *
 * Returns 0.0 if @p text_payload is NULL or the wrong type.
 */
BSG_EXPORT extern double
bsg_payload_text_scale_get(const struct bsg_payload *text_payload);


/**
 * Set the 4×4 rotation/transform matrix of @p text_payload.
 *
 * @p rot  A row-major vmath @c mat_t (16 doubles).
 * No-op if either argument is NULL or @p text_payload is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_text_rot_set(struct bsg_payload *text_payload, const mat_t rot);

/**
 * Copy the rotation matrix of @p text_payload into @p out.
 *
 * No-op if either argument is NULL or @p text_payload is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_text_rot_get(const struct bsg_payload *text_payload, mat_t out);


/**
 * Set the anchor alignment of @p text_payload.
 *
 * @p anchor  One of the BSG_TEXT_ANCHOR_* constants.
 * No-op if @p text_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_text_anchor_set(struct bsg_payload *text_payload, int anchor);

/**
 * Return the anchor alignment of @p text_payload.
 *
 * Returns BSG_TEXT_ANCHOR_AUTO (0) if @p text_payload is NULL or the
 * wrong type.
 */
BSG_EXPORT extern int
bsg_payload_text_anchor_get(const struct bsg_payload *text_payload);


/**
 * Enable or disable drawing a leader line from @p text_payload's anchor
 * to its target point.
 *
 * @p flag  Non-zero to draw the leader line; zero to suppress it.
 * No-op if @p text_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_text_line_flag_set(struct bsg_payload *text_payload, int flag);

/**
 * Return the leader-line flag of @p text_payload.
 *
 * Returns 0 if @p text_payload is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_text_line_flag_get(const struct bsg_payload *text_payload);


/**
 * Set the target point (terminus of the leader line) in @p text_payload.
 *
 * No-op if @p text_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_text_target_set(struct bsg_payload *text_payload, const point_t target);

/**
 * Copy the target point from @p text_payload into @p out.
 *
 * No-op if either argument is NULL or @p text_payload is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_text_target_get(const struct bsg_payload *text_payload, point_t out);


/**
 * Enable or disable drawing an arrowhead on the leader line.
 *
 * @p flag  Non-zero to draw an arrow; zero for a plain line.
 * No-op if @p text_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_text_arrow_set(struct bsg_payload *text_payload, int flag);

/**
 * Return the arrowhead flag of @p text_payload.
 *
 * Returns 0 if @p text_payload is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_text_arrow_get(const struct bsg_payload *text_payload);


/**
 * Generate stroked vlist wireframe geometry for the text stored in
 * @p text_payload using the vector-font helpers.
 *
 * On success the @c bv_vlist commands are appended to @p vhead using
 * @p vlfree as the chunk allocator.  Both must be valid initialized
 * @c bu_list heads.
 *
 * Returns 0 on success, -1 if @p text_payload is NULL, the wrong type,
 * or carries no text string.
 */
BSG_EXPORT extern int
bsg_payload_text_build_vlist(struct bsg_payload *text_payload,
			     struct bu_list *vhead,
			     struct bu_list *vlfree);


__END_DECLS

#endif /* BSG_TEXT_H */

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
