/*                      L I G H T . C
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
/** @file libbsg/light.c
 *
 * Phase 7B/7C: BSG light data model and scene-root light-set registry.
 *
 * Light sets are stored in a global hash map keyed by root node pointer,
 * matching the material/selection side-car pattern used by earlier phases.
 *
 * bsg_scene_light_enable() provides the Phase 7C compatibility bridge:
 * callers (e.g. libdm) can mirror dm_set_light() semantics into the BSG
 * light state without knowing BSG internals.
 */

#include "common.h"

#include <string.h>

#include "bu/hash.h"
#include "bu/malloc.h"
#include "bsg/light.h"


/* ---------------------------------------------------------------------- */
/* bsg_light helpers                                                        */
/* ---------------------------------------------------------------------- */

void
bsg_light_init(struct bsg_light *light)
{
    if (!light)
	return;
    memset(light, 0, sizeof(*light));
    light->kind        = BSG_LIGHT_AMBIENT;
    light->enabled     = 0;
    light->view_scoped = 0;
    light->intensity   = 1.0;
    light->color[0]    = 1.0;
    light->color[1]    = 1.0;
    light->color[2]    = 1.0;
    light->spot_cutoff    = 45.0;
    light->spot_exponent  = 0.0;
}


/* ---------------------------------------------------------------------- */
/* bsg_light_set lifecycle                                                  */
/* ---------------------------------------------------------------------- */

struct bsg_light_set *
bsg_light_set_create(void)
{
    struct bsg_light_set *ls;
    BU_ALLOC(ls, struct bsg_light_set);
    ls->count   = 0;
    ls->_cap    = 0;
    ls->_lights = NULL;
    ls->enabled = 0;
    return ls;
}


void
bsg_light_set_destroy(struct bsg_light_set *ls)
{
    if (!ls)
	return;
    if (ls->_lights)
	bu_free(ls->_lights, "bsg_light_set lights array");
    bu_free(ls, "bsg_light_set");
}


void
bsg_light_set_clear(struct bsg_light_set *ls)
{
    if (!ls)
	return;
    ls->count = 0;
    /* keep allocated capacity to avoid churn on re-populate */
}


int
bsg_light_set_add(struct bsg_light_set *ls, const struct bsg_light *light)
{
    if (!ls || !light)
	return 0;

    if (ls->count >= ls->_cap) {
	size_t new_cap = (ls->_cap == 0) ? 4 : ls->_cap * 2;
	struct bsg_light *nl =
	    (struct bsg_light *)bu_realloc(ls->_lights,
		    new_cap * sizeof(struct bsg_light),
		    "bsg_light_set grow");
	if (!nl)
	    return 0;
	ls->_lights = nl;
	ls->_cap    = new_cap;
    }

    ls->_lights[ls->count] = *light;
    ls->count++;
    return 1;
}


size_t
bsg_light_set_count(const struct bsg_light_set *ls)
{
    return ls ? ls->count : 0;
}


const struct bsg_light *
bsg_light_set_get(const struct bsg_light_set *ls, size_t index)
{
    if (!ls || index >= ls->count)
	return NULL;
    return &ls->_lights[index];
}


/* ---------------------------------------------------------------------- */
/* Default light helpers                                                    */
/* ---------------------------------------------------------------------- */

void
bsg_light_set_create_default(struct bsg_light_set *ls)
{
    struct bsg_light ambient;
    struct bsg_light key;

    if (!ls)
	return;

    bsg_light_set_clear(ls);

    /* Ambient light — matches GL_LIGHT_MODEL_AMBIENT (0.2, 0.2, 0.2) */
    bsg_light_init(&ambient);
    ambient.kind        = BSG_LIGHT_AMBIENT;
    ambient.enabled     = 1;
    ambient.color[0]    = 0.2f;
    ambient.color[1]    = 0.2f;
    ambient.color[2]    = 0.2f;
    ambient.intensity   = 1.0;
    ambient.ambient[0]  = 0.2f;
    ambient.ambient[1]  = 0.2f;
    ambient.ambient[2]  = 0.2f;
    bsg_light_set_add(ls, &ambient);

    /*
     * Key directional light — matches the legacy GL LIGHT0 setup
     * (diffuse/specular = 1,1,1; direction from view = 0,0,-1).
     * view_scoped=1 keeps the light fixed in view space like a
     * camera-mounted light.
     */
    bsg_light_init(&key);
    key.kind         = BSG_LIGHT_DIRECTIONAL;
    key.enabled      = 1;
    key.view_scoped  = 1;
    key.color[0]     = 1.0;
    key.color[1]     = 1.0;
    key.color[2]     = 1.0;
    key.intensity    = 1.0;
    VSET(key.direction, 0.0, 0.0, -1.0);
    bsg_light_set_add(ls, &key);

    ls->enabled = 1;
}


/* ---------------------------------------------------------------------- */
/* Scene-root light set registry                                            */
/* ---------------------------------------------------------------------- */

static bu_hash_tbl *_bsg_light_map = NULL;

static void
_light_map_ensure(void)
{
    if (!_bsg_light_map)
	_bsg_light_map = bu_hash_create(64);
}

static struct bsg_light_set *
_bsg_root_light_set_get(const bsg_node *root)
{
    if (!root || !_bsg_light_map)
	return NULL;
    return (struct bsg_light_set *)bu_hash_get(_bsg_light_map,
	    (const uint8_t *)&root, sizeof(root));
}

static struct bsg_light_set *
_bsg_root_light_set_get_or_create(bsg_node *root)
{
    struct bsg_light_set *ls = NULL;

    if (!root)
	return NULL;

    _light_map_ensure();
    ls = _bsg_root_light_set_get(root);
    if (ls)
	return ls;

    ls = bsg_light_set_create();
    if (!ls)
	return NULL;
    bsg_light_set_create_default(ls);

    bu_hash_set(_bsg_light_map,
		(const uint8_t *)&root, sizeof(root),
		(void *)ls);
    return ls;
}


struct bsg_light_set *
bsg_scene_light_set_get(bsg_node *root, int create)
{
    if (!root)
	return NULL;
    if (create)
	return _bsg_root_light_set_get_or_create(root);
    return _bsg_root_light_set_get(root);
}


void
bsg_scene_light_enable(bsg_node *root, int enable)
{
    struct bsg_light_set *ls;

    if (!root)
	return;

    ls = _bsg_root_light_set_get_or_create(root);
    if (!ls)
	return;
    ls->enabled = enable ? 1 : 0;
}


int
bsg_scene_light_is_enabled(const bsg_node *root)
{
    const struct bsg_light_set *ls;

    if (!root)
	return 0;
    ls = _bsg_root_light_set_get(root);
    return (ls && ls->enabled) ? 1 : 0;
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
