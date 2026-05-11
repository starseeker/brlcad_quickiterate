/*                       A C T I O N . H
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
 * Phase 5 action/traversal APIs for bounds, search, and renderable collection.
 */
/** @{ */
/* @file bsg/action.h */

#ifndef BSG_ACTION_H
#define BSG_ACTION_H

#include "common.h"
#include "vmath.h"
#include "bu/ptbl.h"
#include "bsg/defines.h"
#include "bsg/identity.h"
#include "bsg/material.h"
#include "bsg/payload.h"

__BEGIN_DECLS

enum bsg_action_result {
    BSG_ACTION_CONTINUE = 0,
    BSG_ACTION_STOP,
    BSG_ACTION_ERROR
};

struct bsg_action {
    struct bview *view;
    int include_overlays;
    int lod_level; /* -1 means use active level from per-view cursor */
    int stopped;
    int error;
    int (*node_cb)(struct bsg_action *action, bsg_node *node, const mat_t world);
};

BSG_EXPORT extern void
bsg_action_init(struct bsg_action *action,
		int (*node_cb)(struct bsg_action *action, bsg_node *node, const mat_t world));

BSG_EXPORT extern void
bsg_action_set_view(struct bsg_action *action, struct bview *view);

BSG_EXPORT extern void
bsg_action_set_include_overlays(struct bsg_action *action, int include_overlays);

BSG_EXPORT extern void
bsg_action_set_lod_level(struct bsg_action *action, int lod_level);

BSG_EXPORT extern int
bsg_action_apply(struct bsg_action *action, bsg_node *root);

struct bsg_bbox_action {
    struct bsg_action base;
    vect_t bmin;
    vect_t bmax;
    int have_bounds;
};

BSG_EXPORT extern void
bsg_bbox_action_init(struct bsg_bbox_action *action);

BSG_EXPORT extern int
bsg_bbox_action_result(const struct bsg_bbox_action *action, vect_t *bmin, vect_t *bmax);

struct bsg_search_action {
    struct bsg_action base;

    char *name;
    unsigned long long kind_mask;
    unsigned long long payload_mask;
    struct bsg_node_id node_id;
    bsg_node *parent;
    uint64_t source_path_hash;
    enum bsg_source_kind source_path_kind;
    enum bsg_material_source material_source;

    int use_name;
    int use_kind_mask;
    int use_payload_mask;
    int use_node_id;
    int use_parent;
    int use_source_path;
    int use_material_source;

    size_t max_results;
    struct bu_ptbl *results;
};

BSG_EXPORT extern void
bsg_search_action_init(struct bsg_search_action *action);

BSG_EXPORT extern void
bsg_search_action_reset(struct bsg_search_action *action);

BSG_EXPORT extern void
bsg_search_action_add_name_criteria(struct bsg_search_action *action, const char *name);

BSG_EXPORT extern void
bsg_search_action_add_kind_criteria(struct bsg_search_action *action, unsigned long long kind_mask);

BSG_EXPORT extern void
bsg_search_action_add_payload_criteria(struct bsg_search_action *action, unsigned long long payload_mask);

BSG_EXPORT extern void
bsg_search_action_add_node_id_criteria(struct bsg_search_action *action, struct bsg_node_id node_id);

BSG_EXPORT extern void
bsg_search_action_add_parent_criteria(struct bsg_search_action *action, bsg_node *parent);

BSG_EXPORT extern void
bsg_search_action_add_source_path_criteria(struct bsg_search_action *action,
					   const char *path,
					   enum bsg_source_kind source_kind);

BSG_EXPORT extern void
bsg_search_action_add_material_source_criteria(struct bsg_search_action *action,
					       enum bsg_material_source source);

BSG_EXPORT extern void
bsg_search_action_set_max_results(struct bsg_search_action *action, size_t max_results);

BSG_EXPORT extern size_t
bsg_search_action_result_count(const struct bsg_search_action *action);

BSG_EXPORT extern bsg_node *
bsg_search_action_result_node(const struct bsg_search_action *action, size_t idx);

struct bsg_collect_shape {
    bsg_node *node;
    struct bsg_payload *payload;
    mat_t world;
    vect_t bmin;
    vect_t bmax;
};

struct bsg_collect_action {
    struct bsg_action base;
    struct bsg_collect_shape *shapes;
    size_t shape_count;
    size_t shape_capacity;
};

BSG_EXPORT extern void
bsg_collect_action_init(struct bsg_collect_action *action);

BSG_EXPORT extern void
bsg_collect_action_reset(struct bsg_collect_action *action);

BSG_EXPORT extern const struct bsg_collect_shape *
bsg_collect_action_shapes(const struct bsg_collect_action *action, size_t *shape_count);

__END_DECLS

#endif /* BSG_ACTION_H */

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
