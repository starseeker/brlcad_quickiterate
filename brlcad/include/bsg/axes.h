/*                        A X E S . H
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
 * Slice 6 (bv_scene_obj_migrate): BSG axes overlay payload API.
 *
 * An axes payload stores the display state for a 3-D axes indicator
 * (position, size, line width, colors, labelling, and tick-mark
 * options).  It is the typed BSG replacement for @c bv_axes stored
 * directly on a @c bview or in @c s_i_data on a @c bv_scene_obj.
 *
 * Usage sketch:
 * @code
 *   bsg_node *shape = bsg_shape_create(v);
 *   struct bsg_payload *ap = bsg_payload_axes_create();
 *   point_t origin = VINIT_ZERO;
 *   bsg_payload_axes_pos_set(ap, origin);
 *   bsg_payload_axes_size_set(ap, 1.0);
 *   bsg_node_payload_set(shape, ap);
 * @endcode
 */
/** @{ */
/* @file bsg/axes.h */

#ifndef BSG_AXES_H
#define BSG_AXES_H

#include "common.h"
#include "vmath.h"
#include "bsg/defines.h"
#include "bsg/payload.h"

__BEGIN_DECLS


/**
 * Create a BSG axes overlay payload with all fields defaulting to zero
 * (invisible, at the origin, default colors).
 *
 * Returns NULL on allocation failure.
 * Caller must free with bsg_payload_destroy().
 */
BSG_EXPORT extern struct bsg_payload *
bsg_payload_axes_create(void);


/* ------------------------------------------------------------------ */
/* Draw flag                                                           */
/* ------------------------------------------------------------------ */

/**
 * Set whether the axes should be drawn.
 *
 * @p flag  Non-zero to enable drawing; zero to suppress.
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_draw_set(struct bsg_payload *axes_payload, int flag);

/**
 * Return the draw flag of @p axes_payload.
 *
 * Returns 0 if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_axes_draw_get(const struct bsg_payload *axes_payload);


/* ------------------------------------------------------------------ */
/* Position                                                            */
/* ------------------------------------------------------------------ */

/**
 * Set the 3-D position of the axes origin in model coordinates.
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_pos_set(struct bsg_payload *axes_payload, const point_t pos);

/**
 * Copy the position from @p axes_payload into @p out.
 *
 * No-op if either argument is NULL or @p axes_payload is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_pos_get(const struct bsg_payload *axes_payload, point_t out);


/* ------------------------------------------------------------------ */
/* Size and line width                                                 */
/* ------------------------------------------------------------------ */

/**
 * Set the axes display size.
 *
 * For HUD axes this is in view coordinates; for model-space axes it is
 * a model-space length.
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_size_set(struct bsg_payload *axes_payload, fastf_t size);

/**
 * Return the axes display size from @p axes_payload.
 *
 * Returns 0.0 if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern fastf_t
bsg_payload_axes_size_get(const struct bsg_payload *axes_payload);

/**
 * Set the axes line width in pixels.
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_line_width_set(struct bsg_payload *axes_payload, int width);

/**
 * Return the axes line width (pixels) from @p axes_payload.
 *
 * Returns 0 if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_axes_line_width_get(const struct bsg_payload *axes_payload);


/* ------------------------------------------------------------------ */
/* Colors                                                              */
/* ------------------------------------------------------------------ */

/**
 * Set the axes RGB color of @p axes_payload.
 *
 * @p rgb  Three integers in the range [0, 255].
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_color_set(struct bsg_payload *axes_payload, const int rgb[3]);

/**
 * Copy the axes color from @p axes_payload into @p rgb_out.
 *
 * @p rgb_out  Array of three ints (caller-provided).
 * No-op if either argument is NULL or @p axes_payload is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_color_get(const struct bsg_payload *axes_payload, int rgb_out[3]);


/* ------------------------------------------------------------------ */
/* Label options                                                       */
/* ------------------------------------------------------------------ */

/**
 * Set whether axis labels (X/Y/Z) should be drawn.
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_label_flag_set(struct bsg_payload *axes_payload, int flag);

/**
 * Return the label-flag of @p axes_payload.
 *
 * Returns 0 if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_axes_label_flag_get(const struct bsg_payload *axes_payload);

/**
 * Set the label RGB color of @p axes_payload.
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_label_color_set(struct bsg_payload *axes_payload, const int rgb[3]);

/**
 * Copy the label color from @p axes_payload into @p rgb_out.
 *
 * No-op if either argument is NULL or @p axes_payload is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_label_color_get(const struct bsg_payload *axes_payload, int rgb_out[3]);

/**
 * Set whether each axis should be drawn in its own color (triple-color
 * mode).
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_triple_color_set(struct bsg_payload *axes_payload, int flag);

/**
 * Return the triple-color flag of @p axes_payload.
 *
 * Returns 0 if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_axes_triple_color_get(const struct bsg_payload *axes_payload);

/**
 * Set whether the axes draws only the positive half-axes (pos_only mode).
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_pos_only_set(struct bsg_payload *axes_payload, int flag);

/**
 * Return the pos_only flag of @p axes_payload.
 *
 * Returns 0 if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_axes_pos_only_get(const struct bsg_payload *axes_payload);


/* ------------------------------------------------------------------ */
/* Tick marks                                                          */
/* ------------------------------------------------------------------ */

/**
 * Enable or disable tick marks on @p axes_payload.
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_tick_enabled_set(struct bsg_payload *axes_payload, int flag);

/**
 * Return the tick-enabled flag of @p axes_payload.
 *
 * Returns 0 if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_axes_tick_enabled_get(const struct bsg_payload *axes_payload);

/**
 * Set the minor-tick length (pixels) of @p axes_payload.
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_tick_length_set(struct bsg_payload *axes_payload, int length);

/**
 * Return the minor-tick length of @p axes_payload.
 *
 * Returns 0 if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_axes_tick_length_get(const struct bsg_payload *axes_payload);

/**
 * Set the major-tick length (pixels) of @p axes_payload.
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_tick_major_length_set(struct bsg_payload *axes_payload, int length);

/**
 * Return the major-tick length of @p axes_payload.
 *
 * Returns 0 if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_axes_tick_major_length_get(const struct bsg_payload *axes_payload);

/**
 * Set the tick interval (model-space mm) of @p axes_payload.
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_tick_interval_set(struct bsg_payload *axes_payload, fastf_t interval);

/**
 * Return the tick interval of @p axes_payload.
 *
 * Returns 0.0 if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern fastf_t
bsg_payload_axes_tick_interval_get(const struct bsg_payload *axes_payload);

/**
 * Set the number of minor ticks per major tick of @p axes_payload.
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_ticks_per_major_set(struct bsg_payload *axes_payload, int n);

/**
 * Return the ticks-per-major count of @p axes_payload.
 *
 * Returns 0 if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_axes_ticks_per_major_get(const struct bsg_payload *axes_payload);

/**
 * Set the tick threshold (minimum view-space gap, in pixels, before
 * ticks are suppressed) of @p axes_payload.
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_tick_threshold_set(struct bsg_payload *axes_payload, int threshold);

/**
 * Return the tick threshold of @p axes_payload.
 *
 * Returns 0 if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_axes_tick_threshold_get(const struct bsg_payload *axes_payload);

/**
 * Set the minor-tick RGB color of @p axes_payload.
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_tick_color_set(struct bsg_payload *axes_payload, const int rgb[3]);

/**
 * Copy the minor-tick color from @p axes_payload into @p rgb_out.
 *
 * No-op if either argument is NULL or @p axes_payload is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_tick_color_get(const struct bsg_payload *axes_payload, int rgb_out[3]);

/**
 * Set the major-tick RGB color of @p axes_payload.
 *
 * No-op if @p axes_payload is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_tick_major_color_set(struct bsg_payload *axes_payload, const int rgb[3]);

/**
 * Copy the major-tick color from @p axes_payload into @p rgb_out.
 *
 * No-op if either argument is NULL or @p axes_payload is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_axes_tick_major_color_get(const struct bsg_payload *axes_payload, int rgb_out[3]);


__END_DECLS

#endif /* BSG_AXES_H */

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
