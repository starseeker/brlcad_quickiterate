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
 *
 * Phase 10C: the global _bsg_appearance_map hash table has been replaced
 * by a single heap-allocated struct bsg_appearance * stored in
 * bsg_node_core::appearance.  The pointer is owned by the core and freed
 * by _bsg_core_release() (called from bv_obj_reset).
 */

#include "common.h"

#include "bu/malloc.h"
#include "bv/util.h"
#include "bsg/appearance.h"
#include "bsg/field.h"
#include "bsg/identity.h"

#include "./bsg_private.h"


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

/* Return the appearance struct for @p n from the core, or NULL if unset. */
static struct bsg_appearance *
_bsg_appearance_sc_get(const bsg_node *n)
{
    const struct bv_scene_obj *s;

    if (!n)
	return NULL;

    s = (const struct bv_scene_obj *)n;
    if (s->bsg_core.bsg_magic != BSG_NODE_CORE_MAGIC)
	return NULL;
    return (struct bsg_appearance *)s->bsg_core.appearance;
}

/* Return (allocating if needed) the appearance struct for @p n. */
static struct bsg_appearance *
_bsg_appearance_sc_get_or_create(bsg_node *n)
{
    struct bsg_node_core *core;
    struct bsg_appearance *a;

    if (!n)
	return NULL;

    core = _bsg_core_ensure(n);
    if (!core)
	return NULL;

    if (core->appearance)
	return (struct bsg_appearance *)core->appearance;

    BU_ALLOC(a, struct bsg_appearance);
    bsg_appearance_init(a);
    core->appearance = a;
    return a;
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
    a->draw_arrows = 0;
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
    out->draw_arrows = s->s_arrow ? 1 : 0;
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
    s->s_arrow = a->draw_arrows ? 1 : 0;
}


int
bsg_node_appearance_get(const bsg_node *n, struct bsg_appearance *out)
{
    struct bsg_appearance *a;

    if (!n || !out)
	return 0;

    a = _bsg_appearance_sc_get(n);
    if (a) {
	*out = *a;
	return 1;
    }

    bsg_appearance_from_legacy_obj_settings(n, out);
    return 0;
}


void
bsg_node_appearance_set(bsg_node *n, const struct bsg_appearance *a)
{
    struct bsg_appearance *sc;

    if (!n || !a)
	return;

    sc = _bsg_appearance_sc_get_or_create(n);
    if (!sc)
	return;

    *sc = *a;
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
