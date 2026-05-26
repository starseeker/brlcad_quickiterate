/*                     P A Y L O A D . H
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
 * Payload type accessors and pre-render dispatch (Phase 6-D).
 *
 * Payload type bits occupy bits 40-44 of s_type_flags, above all BSG_NODE_*
 * (bits 28-34) and BSG_SENSOR_* (bits 36-38) bits.
 *
 * bsg_payload_dispatch() is a pre-render hook that runs s_update_callback for
 * payload types that require an update before drawing (BSG_PAYLOAD_CSG,
 * BSG_PAYLOAD_MESH).  The actual rendering still lives in libdm to avoid a
 * libbsg → libdm circular dependency.
 */
/** @{ */
/* @file bsg/payload.h */

#ifndef BSG_PAYLOAD_H
#define BSG_PAYLOAD_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * Set the payload-type bits of @p node to @p payload_flags.
 * @p payload_flags should be one or more BSG_PAYLOAD_* constants from
 * bsg/defines.h.  Any existing payload bits are replaced.
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_payload_type(bsg_node *node, unsigned long long payload_flags);

/**
 * Return the payload-type bits currently stored in @p node's s_type_flags.
 * Returns 0 if @p node is NULL.
 */
BSG_EXPORT extern unsigned long long
bsg_node_get_payload_type(const bsg_node *node);

/**
 * Pre-render payload dispatch: inspect the payload-type bits of @p node
 * and invoke s_update_callback (when set) for types that require an update
 * pass before drawing (BSG_PAYLOAD_CSG, BSG_PAYLOAD_MESH, BSG_PAYLOAD_BREP,
 * BSG_PAYLOAD_OVERLAY).  For BSG_PAYLOAD_VLIST the vlist is assumed to be
 * already populated so no callback is invoked.
 *
 * @p dmp is passed as the first argument to s_update_callback (cast to void*);
 * @p v is passed as the second.  Both may be NULL when calling from tests.
 *
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_payload_dispatch(void *dmp, bsg_node *node, struct bsg_view *v);

__END_DECLS

#endif /* BSG_PAYLOAD_H */

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
