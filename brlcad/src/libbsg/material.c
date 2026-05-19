/*                    M A T E R I A L . C
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
/** @file libbsg/material.c
 *
 * Phase 3: BSG material API and compatibility mapping over legacy
 * bv_scene_obj storage.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "bv/util.h"
#include "bsg/field.h"
#include "bsg/identity.h"
#include "bsg/material.h"

#include "./bsg_private.h"


/* ------------------------------------------------------------------ */
/* Phase 10C: material storage via bsg_node_core                        */
/*                                                                      */
/* The global _bsg_material_map hash table (Phase 3) has been replaced  */
/* by a single heap-allocated struct bsg_material * stored in           */
/* bsg_node_core::material.  The pointer is owned by the core and freed */
/* by _bsg_core_release() (called from bv_obj_reset).                   */
/* ------------------------------------------------------------------ */

static int _bsg_view_obj_material_hook_enabled = 0;

static fastf_t
_material_clamp_transparency(fastf_t t)
{
    if (t < 0.0)
	return 0.0;
    if (t > 1.0)
	return 1.0;
    return t;
}

/* Return the material struct for @p n from the core, or NULL if unset. */
static struct bsg_material *
_bsg_material_sc_get(const bsg_node *n)
{
    if (!n)
	return NULL;

    if (n->bsg_magic != BSG_NODE_CORE_MAGIC)
	return NULL;
    return (struct bsg_material *)n->material;
}

/* Return (allocating if needed) the material struct for @p n. */
static struct bsg_material *
_bsg_material_sc_get_or_create(bsg_node *n)
{
    bsg_node *core;
    struct bsg_material *m;

    if (!n)
	return NULL;

    core = _bsg_core_ensure(n);
    if (!core)
	return NULL;

    if (core->material)
	return (struct bsg_material *)core->material;

    BU_ALLOC(m, struct bsg_material);
    bsg_material_init(m);
    core->material = m;
    return m;
}


void
bsg_material_init(struct bsg_material *m)
{
    if (!m)
	return;
    m->rgba[0] = 255;
    m->rgba[1] = 0;
    m->rgba[2] = 0;
    m->rgba[3] = 255;
    m->transparency = 1.0;
    m->source_kind = BSG_MATERIAL_SOURCE_UNKNOWN;
    m->revision = 0;
    m->use_override_color = 0;
    m->override_rgb[0] = 255;
    m->override_rgb[1] = 0;
    m->override_rgb[2] = 0;
    m->use_geometry_default_color = 0;
}


void
bsg_material_set_rgba(struct bsg_material *m,
		      unsigned char r,
		      unsigned char g,
		      unsigned char b,
		      unsigned char a)
{
    if (!m)
	return;
    m->rgba[0] = r;
    m->rgba[1] = g;
    m->rgba[2] = b;
    m->rgba[3] = a;
    m->transparency = ((fastf_t)a) / 255.0;
}


void
bsg_material_from_legacy_obj(const bsg_node *n, struct bsg_material *out)
{
    if (!out)
	return;

    bsg_material_init(out);
    if (!n)
	return;

    const struct bv_scene_obj *s = (const struct bv_scene_obj *)n;
    const struct bsg_settings *os = (s->s_os) ? s->s_os : &s->s_local_os;

    out->rgba[0] = s->s_color[0];
    out->rgba[1] = s->s_color[1];
    out->rgba[2] = s->s_color[2];
    out->transparency = _material_clamp_transparency(os->transparency);
    out->rgba[3] = (unsigned char)(out->transparency * 255.0);
    out->revision = s->s_color_rev;

    if (os->color_override) {
	out->use_override_color = 1;
	out->override_rgb[0] = os->color[0];
	out->override_rgb[1] = os->color[1];
	out->override_rgb[2] = os->color[2];
	out->source_kind = BSG_MATERIAL_SOURCE_EXPLICIT_OVERRIDE;
    } else if (s->s_old.s_cflag) {
	out->use_geometry_default_color = 1;
	out->source_kind = BSG_MATERIAL_SOURCE_DEFAULT_GEOMETRY_COLOR;
    } else {
	out->source_kind = BSG_MATERIAL_SOURCE_LEGACY_COMPAT;
    }
}


void
bsg_material_to_legacy_obj(bsg_node *n, const struct bsg_material *m)
{
    if (!n || !m)
	return;

    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    struct bsg_settings *os = (s->s_os) ? s->s_os : &s->s_local_os;

    s->s_color[0] = m->rgba[0];
    s->s_color[1] = m->rgba[1];
    s->s_color[2] = m->rgba[2];
    s->s_color_rev = (uint32_t)m->revision;

    os->transparency = _material_clamp_transparency(m->transparency);
    if (m->use_override_color) {
	os->color_override = 1;
	os->color[0] = m->override_rgb[0];
	os->color[1] = m->override_rgb[1];
	os->color[2] = m->override_rgb[2];
    } else {
	os->color_override = 0;
    }

    s->s_old.s_cflag = m->use_geometry_default_color ? 1 : 0;
}


int
bsg_node_material_get(const bsg_node *n, struct bsg_material *out)
{
    struct bsg_material *m;

    if (!n || !out)
	return 0;

    m = _bsg_material_sc_get(n);
    if (m) {
	*out = *m;
	return 1;
    }

    bsg_material_from_legacy_obj(n, out);
    return 0;
}


void
bsg_node_material_set(bsg_node *n, const struct bsg_material *m)
{
    struct bsg_material *sc;

    if (!n || !m)
	return;

    sc = _bsg_material_sc_get_or_create(n);
    if (!sc)
	return;

    *sc = *m;
    bsg_material_to_legacy_obj(n, m);
    bsg_node_field_touch(n, BSG_FIELD_MATERIAL);
    (void)bsg_node_bump_revision(n, BSG_NODE_REV_MATERIAL);
}


void
bsg_node_material_resolve(const bsg_node *n, const struct bsg_material *parent, struct bsg_material *out)
{
    if (!out)
	return;

    bsg_material_init(out);
    if (parent)
	*out = *parent;
    if (n)
	(void)bsg_node_material_get(n, out);
}


static int
_bsg_view_obj_color_hook(struct bv_scene_obj *obj,
			 unsigned char r,
			 unsigned char g,
			 unsigned char b)
{
    struct bsg_material m;
    if (!obj)
	return 0;

    (void)bsg_node_material_get((const bsg_node *)obj, &m);
    m.rgba[0] = r;
    m.rgba[1] = g;
    m.rgba[2] = b;
    m.source_kind = BSG_MATERIAL_SOURCE_EXPLICIT_OVERRIDE;
    m.revision++;
    bsg_node_material_set((bsg_node *)obj, &m);
    return 1;
}


void
bsg_material_enable_view_obj_setters(void)
{
    if (_bsg_view_obj_material_hook_enabled)
	return;

    bv_view_obj_color_hook_set(_bsg_view_obj_color_hook);
    _bsg_view_obj_material_hook_enabled = 1;
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
