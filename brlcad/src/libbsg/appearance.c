/*                  A P P E A R A N C E . C
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
/** @file libbsg/appearance.c
 *
 * Phase 3: BSG appearance API and compatibility mapping over legacy
 * bv_scene_obj storage.
 */

#include "common.h"

#include "bu/hash.h"
#include "bu/malloc.h"
#include "bv/util.h"
#include "bsg/appearance.h"
#include "bsg/field.h"
#include "bsg/identity.h"


struct _bsg_appearance_sidecar {
    int have_appearance;
    struct bsg_appearance appearance;
};

static bu_hash_tbl *_bsg_appearance_map = NULL;
static int _bsg_view_obj_appearance_hook_enabled = 0;

static fastf_t
_appearance_clamp_transparency(fastf_t t)
{
    if (t < 0.0)
	return 0.0;
    if (t > 1.0)
	return 1.0;
    return t;
}

static struct _bsg_appearance_sidecar *
_bsg_appearance_sc_get(const bsg_node *n)
{
    if (!n || !_bsg_appearance_map)
	return NULL;
    return (struct _bsg_appearance_sidecar *)bu_hash_get(_bsg_appearance_map,
	    (const uint8_t *)&n, sizeof(n));
}

static struct _bsg_appearance_sidecar *
_bsg_appearance_sc_get_or_create(const bsg_node *n)
{
    struct _bsg_appearance_sidecar *sc = NULL;
    if (!n)
	return NULL;

    if (!_bsg_appearance_map)
	_bsg_appearance_map = bu_hash_create(128);

    sc = _bsg_appearance_sc_get(n);
    if (sc)
	return sc;

    BU_ALLOC(sc, struct _bsg_appearance_sidecar);
    sc->have_appearance = 0;
    bsg_appearance_init(&sc->appearance);
    bu_hash_set(_bsg_appearance_map, (const uint8_t *)&n, sizeof(n), sc);
    return sc;
}


void
bsg_appearance_init(struct bsg_appearance *a)
{
    if (!a)
	return;
    a->draw_mode = 0;
    a->line_width = 1;
    a->line_style = BSG_APPEARANCE_LINE_SOLID;
    a->transparency = 1.0;
    a->inherit_settings = 0;
    a->arrow_tip_length = 0.0;
    a->arrow_tip_width = 0.0;
    a->draw_solid_lines_only = 0;
    a->draw_non_subtract_only = 0;
}


void
bsg_appearance_from_legacy_obj_settings(const bsg_node *n, struct bsg_appearance *out)
{
    if (!out)
	return;

    bsg_appearance_init(out);
    if (!n)
	return;

    const struct bv_scene_obj *s = (const struct bv_scene_obj *)n;
    const struct bv_obj_settings *os = (s->s_os) ? s->s_os : &s->s_local_os;

    out->draw_mode = os->s_dmode;
    out->line_width = os->s_line_width;
    out->line_style = s->s_soldash ? BSG_APPEARANCE_LINE_DASHED : BSG_APPEARANCE_LINE_SOLID;
    out->transparency = _appearance_clamp_transparency(os->transparency);
    out->inherit_settings = s->s_inherit_settings ? 1 : 0;
    out->arrow_tip_length = os->s_arrow_tip_length;
    out->arrow_tip_width = os->s_arrow_tip_width;
    out->draw_solid_lines_only = os->draw_solid_lines_only;
    out->draw_non_subtract_only = os->draw_non_subtract_only;
}


void
bsg_appearance_to_legacy_obj_settings(bsg_node *n, const struct bsg_appearance *a)
{
    if (!n || !a)
	return;

    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    struct bv_obj_settings *os = (s->s_os) ? s->s_os : &s->s_local_os;

    os->s_dmode = a->draw_mode;
    os->s_line_width = a->line_width;
    os->transparency = _appearance_clamp_transparency(a->transparency);
    os->s_arrow_tip_length = a->arrow_tip_length;
    os->s_arrow_tip_width = a->arrow_tip_width;
    os->draw_solid_lines_only = a->draw_solid_lines_only;
    os->draw_non_subtract_only = a->draw_non_subtract_only;
    s->s_soldash = (a->line_style == BSG_APPEARANCE_LINE_DASHED) ? 1 : 0;
    s->s_inherit_settings = a->inherit_settings ? 1 : 0;
}


int
bsg_node_appearance_get(const bsg_node *n, struct bsg_appearance *out)
{
    struct _bsg_appearance_sidecar *sc = NULL;
    if (!n || !out)
	return 0;

    sc = _bsg_appearance_sc_get(n);
    if (sc && sc->have_appearance) {
	*out = sc->appearance;
	return 1;
    }

    bsg_appearance_from_legacy_obj_settings(n, out);
    return 0;
}


void
bsg_node_appearance_set(bsg_node *n, const struct bsg_appearance *a)
{
    struct _bsg_appearance_sidecar *sc = NULL;
    if (!n || !a)
	return;

    sc = _bsg_appearance_sc_get_or_create(n);
    if (!sc)
	return;

    sc->appearance = *a;
    sc->have_appearance = 1;
    bsg_appearance_to_legacy_obj_settings(n, a);
    bsg_node_field_touch(n, BSG_FIELD_APPEARANCE);
    (void)bsg_node_bump_revision(n, BSG_NODE_REV_APPEARANCE);
}


void
bsg_node_appearance_resolve(const bsg_node *n, const struct bsg_appearance *parent, struct bsg_appearance *out)
{
    struct bsg_appearance local;
    int have_local = 0;

    if (!out)
	return;

    bsg_appearance_init(out);
    if (parent)
	*out = *parent;
    if (!n)
	return;

    have_local = bsg_node_appearance_get(n, &local);
    if (!parent || !local.inherit_settings || have_local) {
	*out = local;
    }
}


static int
_bsg_view_obj_line_width_hook(struct bv_scene_obj *obj, int line_width)
{
    struct bsg_appearance a;
    if (!obj)
	return 0;

    (void)bsg_node_appearance_get((const bsg_node *)obj, &a);
    a.line_width = line_width;
    bsg_node_appearance_set((bsg_node *)obj, &a);
    return 1;
}


void
bsg_appearance_enable_view_obj_setters(void)
{
    if (_bsg_view_obj_appearance_hook_enabled)
	return;

    bv_view_obj_line_width_hook_set(_bsg_view_obj_line_width_hook);
    _bsg_view_obj_appearance_hook_enabled = 1;
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
