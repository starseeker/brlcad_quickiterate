/*                  A P P E A R A N C E . H
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
 * Phase 3 appearance API for BSG nodes.
 */
/** @{ */
/* @file bsg/appearance.h */

#ifndef BSG_APPEARANCE_H
#define BSG_APPEARANCE_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

enum bsg_appearance_line_style {
    BSG_APPEARANCE_LINE_SOLID = 0,
    BSG_APPEARANCE_LINE_DASHED = 1
};

struct bsg_appearance {
    int draw_mode;
    int line_width;
    enum bsg_appearance_line_style line_style;
    fastf_t transparency; /* [0..1], where 1 is opaque */
    int inherit_settings;
    fastf_t arrow_tip_length;
    fastf_t arrow_tip_width;
    int draw_arrows;           /* non-zero: draw arrow heads on line endpoints */
    int draw_solid_lines_only;
    int draw_non_subtract_only;
};

BSG_EXPORT extern void
bsg_appearance_init(struct bsg_appearance *a);

BSG_EXPORT extern int
bsg_node_appearance_get(const bsg_node *n, struct bsg_appearance *out);

BSG_EXPORT extern void
bsg_node_appearance_set(bsg_node *n, const struct bsg_appearance *a);

BSG_EXPORT extern enum bsg_appearance_line_style
bsg_node_line_style(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_set_line_style(bsg_node *n, enum bsg_appearance_line_style style);

BSG_EXPORT extern int
bsg_node_draw_arrows(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_set_draw_arrows(bsg_node *n, int draw_arrows);

BSG_EXPORT extern void
bsg_node_appearance_resolve(const bsg_node *n, const struct bsg_appearance *parent, struct bsg_appearance *out);

BSG_EXPORT extern void
bsg_appearance_from_legacy_obj_settings(const bsg_node *n, struct bsg_appearance *out);

BSG_EXPORT extern void
bsg_appearance_to_legacy_obj_settings(bsg_node *n, const struct bsg_appearance *a);

/* Install hook bridges so bv_view_obj_set_line_width routes through BSG appearance. */
BSG_EXPORT extern void
bsg_appearance_enable_view_obj_setters(void);

__END_DECLS

#endif /* BSG_APPEARANCE_H */

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
