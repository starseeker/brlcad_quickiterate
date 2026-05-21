/*                         C O M P A T . H
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
 * Temporary compatibility conversions between legacy scene-object handles
 * and BSG node handles.
 */
/** @{ */
/* @file bsg/compat.h */

#ifndef BSG_COMPAT_H
#define BSG_COMPAT_H

#include "common.h"
#include "bsg/defines.h"

struct bv_scene_obj;

__BEGIN_DECLS

BSG_EXPORT extern bsg_node *
bsg_compat_from_bv_scene_obj(struct bv_scene_obj *s);

BSG_EXPORT extern const bsg_node *
bsg_compat_from_bv_scene_obj_const(const struct bv_scene_obj *s);

BSG_EXPORT extern struct bv_scene_obj *
bsg_compat_to_bv_scene_obj(bsg_node *n);

BSG_EXPORT extern const struct bv_scene_obj *
bsg_compat_to_bv_scene_obj_const(const bsg_node *n);

__END_DECLS

#endif /* BSG_COMPAT_H */

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
