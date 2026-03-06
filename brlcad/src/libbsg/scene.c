/*                       S C E N E . C
 * BRL-CAD
 *
 * Copyright (c) 2024-2025 United States Government as represented by
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
/** @file libbsg/scene.c
 *
 * @brief Scene-root creation and view-parameter binding.
 *
 * Implements:
 *   - @c libbsg_camera_node_alloc() — allocate a BSG_NODE_CAMERA node
 *   - @c libbsg_camera_node_get()   — read camera data from a camera node
 *   - @c libbsg_camera_node_set()   — write camera data into a camera node
 *   - @c libbsg_scene_root_camera() — find the camera child of a root
 *   - @c libbsg_scene_root_create() — create a fresh separator root + camera
 *   - @c libbsg_view_bind()         — sync view params into an existing root
 */

#include "common.h"

#include <string.h>   /* memcpy, memset */

#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bn/mat.h"
#include "vmath.h"

#include "libbsg/libbsg.h"

/* ====================================================================== *
 * Camera node helpers                                                     *
 * ====================================================================== */

/**
 * @brief Destructor for a heap-allocated bsg_camera stored in a node's payload.
 */
static void
camera_payload_free(void *p)
{
    if (p)
	bu_free(p, "bsg_camera payload");
}

bsg_node *
libbsg_camera_node_alloc(const bsg_camera *cam)
{
    bsg_node *node;
    bsg_camera *copy;

    node = libbsg_node_alloc(BSG_NODE_CAMERA);
    if (!node)
	return NULL;

    /* Allocate and copy the camera struct */
    copy = (bsg_camera *)bu_malloc(sizeof(bsg_camera), "bsg_camera payload");
    if (cam)
	memcpy(copy, cam, sizeof(bsg_camera));
    else
	memset(copy, 0, sizeof(bsg_camera));

    node->payload      = copy;
    node->free_payload = camera_payload_free;

    return node;
}

int
libbsg_camera_node_get(const bsg_node *node, bsg_camera *out)
{
    if (!node || !out)
	return -1;
    if (!(node->type_flags & BSG_NODE_CAMERA))
	return -1;
    if (!node->payload)
	return -1;
    memcpy(out, node->payload, sizeof(bsg_camera));
    return 0;
}

void
libbsg_camera_node_set(bsg_node *node, const bsg_camera *cam)
{
    if (!node || !cam)
	return;
    if (!(node->type_flags & BSG_NODE_CAMERA))
	return;

    if (!node->payload) {
	/* Allocate the payload if it doesn't exist yet */
	node->payload      = bu_malloc(sizeof(bsg_camera), "bsg_camera payload");
	node->free_payload = camera_payload_free;
    }
    memcpy(node->payload, cam, sizeof(bsg_camera));
}

/* ====================================================================== *
 * Scene root helpers                                                      *
 * ====================================================================== */

bsg_node *
libbsg_scene_root_camera(const bsg_node *root)
{
    size_t i;
    if (!root)
	return NULL;
    for (i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	bsg_node *child = (bsg_node *)BU_PTBL_GET(&root->children, i);
	if (child->type_flags & BSG_NODE_CAMERA)
	    return child;
    }
    return NULL;
}

bsg_node *
libbsg_scene_root_create(const libbsg_view_params *params)
{
    bsg_node *root;
    bsg_node *cam_node;

    /* Create the separator root */
    root = libbsg_node_alloc(BSG_NODE_SEPARATOR);
    if (!root)
	return NULL;

    /* Create and prepend a camera node */
    cam_node = libbsg_camera_node_alloc(params ? &params->camera : NULL);
    if (!cam_node) {
	libbsg_node_free(root, 0);
	return NULL;
    }

    libbsg_node_add_child(root, cam_node);
    return root;
}

/* ====================================================================== *
 * View binding                                                            *
 * ====================================================================== */

void
libbsg_view_bind(bsg_node *root, const libbsg_view_params *params)
{
    bsg_node *cam_node;

    if (!root || !params)
	return;

    cam_node = libbsg_scene_root_camera(root);
    if (cam_node) {
	/* Update the existing camera node in-place */
	libbsg_camera_node_set(cam_node, &params->camera);
    } else {
	/* No camera node yet — add one as a child.
	 * libbsg_scene_root_camera() searches linearly so the position
	 * in the list does not affect correctness. */
	cam_node = libbsg_camera_node_alloc(&params->camera);
	if (!cam_node)
	    return;
	libbsg_node_add_child(root, cam_node);
    }
}

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
