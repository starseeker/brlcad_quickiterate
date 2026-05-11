/*                    M A T E R I A L . H
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
 * Phase 3 material API for BSG nodes.
 */
/** @{ */
/* @file bsg/material.h */

#ifndef BSG_MATERIAL_H
#define BSG_MATERIAL_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

enum bsg_material_source {
    BSG_MATERIAL_SOURCE_UNKNOWN = 0,
    BSG_MATERIAL_SOURCE_EXPLICIT_OVERRIDE,
    BSG_MATERIAL_SOURCE_DB_TABLE,
    BSG_MATERIAL_SOURCE_ATTRIBUTE,
    BSG_MATERIAL_SOURCE_INHERITED,
    BSG_MATERIAL_SOURCE_DEFAULT_GEOMETRY_COLOR,
    BSG_MATERIAL_SOURCE_LEGACY_COMPAT
};

struct bsg_material {
    unsigned char rgba[4];
    fastf_t transparency; /* [0..1], where 1 is opaque */
    enum bsg_material_source source_kind;
    uint64_t revision;
    int use_override_color;
    unsigned char override_rgb[3];
    int use_geometry_default_color;
};

BSG_EXPORT extern void
bsg_material_init(struct bsg_material *m);

BSG_EXPORT extern void
bsg_material_set_rgba(struct bsg_material *m,
		      unsigned char r,
		      unsigned char g,
		      unsigned char b,
		      unsigned char a);

BSG_EXPORT extern int
bsg_node_material_get(const bsg_node *n, struct bsg_material *out);

BSG_EXPORT extern void
bsg_node_material_set(bsg_node *n, const struct bsg_material *m);

BSG_EXPORT extern void
bsg_node_material_resolve(const bsg_node *n, const struct bsg_material *parent, struct bsg_material *out);

BSG_EXPORT extern void
bsg_material_from_legacy_obj(const bsg_node *n, struct bsg_material *out);

BSG_EXPORT extern void
bsg_material_to_legacy_obj(bsg_node *n, const struct bsg_material *m);

/* Install hook bridges so bv_view_obj_set_color routes through BSG material. */
BSG_EXPORT extern void
bsg_material_enable_view_obj_setters(void);

__END_DECLS

#endif /* BSG_MATERIAL_H */

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
