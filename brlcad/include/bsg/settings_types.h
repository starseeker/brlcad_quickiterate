/*              S E T T I N G S _ T Y P E S . H
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
 * Shared BSG settings snapshot type definitions.
 */
/** @{ */
/* @file bsg/settings_types.h */

#ifndef BSG_SETTINGS_TYPES_H
#define BSG_SETTINGS_TYPES_H

#include "common.h"
#include "vmath.h"

/**
 * BSG settings-inheritance snapshot.
 *
 * Many settings have application level defaults that can be overridden for
 * individual scene nodes.  Transitional storage carries these as BSG settings
 * so callers do not need a separate compatibility object-settings type.
 */
struct bsg_settings {

    int draw_mode;         	/**< @brief  draw modes (TODO - are these accurate?):
				 *            0 - wireframe
				 *	      1 - shaded bots and polysolids only (booleans NOT evaluated)
				 *	      2 - shaded (booleans NOT evaluated)
				 *	      3 - shaded (booleans evaluated)
				 *	      4 - hidden line
				 */
    int mixed_modes;            /**< @brief  when drawing, don't remove an objects view objects for other modes */
    fastf_t transparency;	/**< @brief  holds a transparency value in the range [0.0, 1.0] - 1 is opaque */

    int color_override;
    unsigned char color[3];	/**< @brief  color to draw as */

    int line_width;		/**< @brief  current line width */
    fastf_t arrow_tip_length; /**< @brief  arrow tip length */
    fastf_t arrow_tip_width;  /**< @brief  arrow tip width */
    int draw_solid_lines_only;   /**< @brief do not use dashed lines for subtraction solids */
    int draw_non_subtract_only;  /**< @brief do not visualize subtraction solids */
};
#define BSG_SETTINGS_INIT {0, 0, 1.0, 0, {255, 255, 255}, 1, 0.0, 0.0, 0, 0}

#endif /* BSG_SETTINGS_TYPES_H */

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
