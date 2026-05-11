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
 * Typed payload APIs for BSG geometry containers (Phase 4).
 *
 * The payload side-car stores typed geometry metadata and revision counters.
 * During migration, the vlist payload wrapper is backed by the existing
 * bv_scene_obj::s_vlist storage.
 */
/** @{ */
/* @file bsg/payload.h */

#ifndef BSG_PAYLOAD_H
#define BSG_PAYLOAD_H

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bsg/defines.h"

__BEGIN_DECLS

enum bsg_payload_type {
    BSG_PAYLOAD_TYPE_NONE = 0,
    BSG_PAYLOAD_TYPE_VLIST,
    BSG_PAYLOAD_TYPE_WIRE,
    BSG_PAYLOAD_TYPE_MESH,
    BSG_PAYLOAD_TYPE_BREP_REF,
    BSG_PAYLOAD_TYPE_CSG_REF,
    BSG_PAYLOAD_TYPE_OVERLAY,
    BSG_PAYLOAD_TYPE_IMAGE
};

struct bsg_payload {
    enum bsg_payload_type type;
    uint64_t revision;
    uint64_t bounds_revision;
    void *source;
    void (*free_fn)(struct bsg_payload *);
    struct bsg_payload *(*clone_fn)(const struct bsg_payload *);
    int (*update_fn)(struct bsg_payload *, struct bview *);
};

struct bsg_wire_polyline {
    size_t point_count;
    point_t *points;
};

BSG_EXPORT extern struct bsg_payload *
bsg_payload_create(enum bsg_payload_type type);

BSG_EXPORT extern void
bsg_payload_destroy(struct bsg_payload *payload);

BSG_EXPORT extern struct bsg_payload *
bsg_node_payload_get(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_payload_set(bsg_node *n, struct bsg_payload *payload);

BSG_EXPORT extern enum bsg_payload_type
bsg_payload_type(const struct bsg_payload *payload);

BSG_EXPORT extern uint64_t
bsg_payload_revision(const struct bsg_payload *payload);

BSG_EXPORT extern uint64_t
bsg_payload_bump_revision(struct bsg_payload *payload);

BSG_EXPORT extern int
bsg_payload_bounds(const struct bsg_payload *payload, point_t *bmin, point_t *bmax);

BSG_EXPORT extern struct bsg_payload *
bsg_payload_vlist_from_node(bsg_node *n);

BSG_EXPORT extern void
bsg_payload_vlist_set(struct bsg_payload *payload, struct bu_list *vhead);

BSG_EXPORT extern struct bu_list *
bsg_payload_vlist_head(const struct bsg_payload *payload);

BSG_EXPORT extern size_t
bsg_payload_vlist_count(const struct bsg_payload *payload);

BSG_EXPORT extern struct bsg_payload *
bsg_payload_wire_from_vlist(const struct bsg_payload *vlist_payload);

BSG_EXPORT extern size_t
bsg_payload_wire_polyline_count(const struct bsg_payload *wire_payload);

BSG_EXPORT extern const struct bsg_wire_polyline *
bsg_payload_wire_polyline_get(const struct bsg_payload *wire_payload, size_t idx);

BSG_EXPORT extern void
bsg_payload_mesh_set(struct bsg_payload *payload, const struct bv_mesh_lod *lod);

BSG_EXPORT extern const struct bv_mesh_lod *
bsg_payload_mesh_get(const struct bsg_payload *payload);

BSG_EXPORT extern const struct bv_mesh_lod *
bsg_payload_mesh_lod_get(const struct bsg_payload *payload);

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
 * Compatibility pre-render dispatch: inspect payload type bits in s_type_flags
 * and invoke s_update_callback for payload kinds that require update.
 *
 * @p dmp is passed as the first argument to s_update_callback (cast to void*);
 * @p v is passed as the second.  Both may be NULL when calling from tests.
 *
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_payload_dispatch(void *dmp, bsg_node *node, struct bview *v);

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
