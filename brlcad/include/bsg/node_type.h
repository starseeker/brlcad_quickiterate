/*                   N O D E _ T Y P E . H
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
 * BSG-owned core node type definition.
 *
 * This header defines @c struct @c bsg_node and the associated node
 * constants (@c BSG_NODE_CORE_MAGIC, @c BSG_NODE_REV_MAX).  It is the
 * authoritative definition point for the BSG core node type.
 *
 * It intentionally depends only on standard BRL-CAD utility headers
 * (bu/list.h, bu/ptbl.h, bu/vls.h) so that BSG headers can include it
 * without pulling in @c bv/defines.h or any other scene-object header.
 *
 * @c bv/defines.h includes this header so that the legacy scene-object
 * allocation can embed @c struct @c bsg_node as its first member during the
 * migration period.  The dependency is therefore: bsg/node_type.h is
 * independent; bv/defines.h depends on bsg/node_type.h (one-way).
 *
 * Phase A (bv_scene_obj_migrate.txt): moved here from bv/defines.h.
 */
/** @{ */
/* @file bsg/node_type.h */

#ifndef BSG_NODE_TYPE_H
#define BSG_NODE_TYPE_H

#include "common.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "bu/vls.h"

__BEGIN_DECLS

/**
 * Magic number for an initialized bsg_node core.
 * Value encodes 'b','n','c','o' in ASCII.
 */
#define BSG_NODE_CORE_MAGIC 0x626e636fUL

/**
 * Maximum number of per-node revision counter slots.
 */
#define BSG_NODE_REV_MAX    8

/*
 * Forward declaration for struct bsg_settings.
 * The full definition lives in bsg/settings_types.h; only pointer-level
 * access is needed here.
 */
struct bsg_settings;

/**
 * @brief BSG core scene-graph node.
 *
 * struct bsg_node is the first-class BSG scene-graph node type.
 *
 * It MUST have @c struct @c bu_list @c l as its first field for bu_list
 * pointer compatibility.  During the migration period the legacy scene-object
 * allocation embeds this as its first member.
 *
 * Design constraints:
 *  - Only basic C types and BU utility structs so that this header does not
 *    need to include BSG-API headers, keeping the include graph acyclic.
 *  - Identity and revision counters are stored inline (no heap allocation).
 *  - Material, appearance, and payload are stored as @c void * pointers; they
 *    are cast to the appropriate BSG types only inside libbsg code.
 *  - @c bsg_core_free_fn is called by @c bsg_node_destroy() before storage is
 *    released, allowing libbsg to release any heap data it allocated.
 */
struct bsg_node {
    struct bu_list l;              /**< @brief list linkage — MUST be first */
    unsigned long long bsg_kind;   /**< @brief BSG_NODE_* flags */
    struct bu_vls bsg_name;        /**< @brief object name */
    struct bsg_node *bsg_parent;   /**< @brief parent node */
    struct bu_ptbl bsg_children;   /**< @brief child node table */
    char bsg_flag;                 /**< @brief UP=visible, DOWN=invisible */
    char bsg_iflag;                /**< @brief UP=illuminated, DOWN=regular */
    int bsg_force_draw;            /**< @brief 1=always draw, overrides bsg_flag */
    /* Absorbed from bsg_node_core: */
    uint32_t bsg_magic;            /**< @brief BSG_NODE_CORE_MAGIC when initialized */
    int have_identity;
    uint64_t identity_node_id;
    uint64_t identity_part_id;
    uint64_t identity_instance_id;
    int identity_source_kind;      /**< @brief enum bsg_source_kind value */
    uint64_t revisions[BSG_NODE_REV_MAX];
    struct bsg_settings *settings_local;     /**< @brief transitional BSG-owned local settings snapshot */
    struct bsg_settings *settings_effective; /**< @brief transitional BSG-owned effective settings snapshot */
    void *material;
    void *appearance;
    void *payload;
    void (*bsg_core_free_fn)(struct bsg_node *);
    /**
     * Opaque pointer to heap-allocated user-pointer storage.
     * Only allocated when a caller stores a non-NULL value via
     * bsg_node_uptr_set().  Freed by _bsg_core_release().
     * Callers must use bsg_node_uptr_get/set(); do NOT access directly.
     */
    void *_uptr_impl;
};

__END_DECLS

#endif /* BSG_NODE_TYPE_H */

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
