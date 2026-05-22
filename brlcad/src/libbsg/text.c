/*                        T E X T . C
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
/** @file libbsg/text.c
 *
 * Slice 6 (bv_scene_obj_migrate): BSG text/label adornment payload.
 *
 * Provides the create/destroy and accessor implementations for
 * BSG_PAYLOAD_TYPE_TEXT payloads.  Also provides
 * bsg_payload_text_build_vlist() which strokes the stored text into a
 * bv_vlist command stream using the bv_vlist_3string() vector-font
 * helper.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "bu/vls.h"
#include "bv/vlist.h"
#include "bsg/defines.h"
#include "bsg/payload.h"
#include "bsg/text.h"
#include "bsg/vlist.h"

/* ------------------------------------------------------------------ */
/* Private implementation struct                                       */
/* ------------------------------------------------------------------ */

struct _bsg_payload_text {
    struct bsg_payload base;
    struct bu_vls text;
    point_t origin;
    mat_t rot;
    double scale;
    int anchor;
    int line_flag;
    point_t target;
    int arrow;
};

/* ------------------------------------------------------------------ */
/* Lifecycle helpers                                                   */
/* ------------------------------------------------------------------ */

static void
_payload_text_free(struct bsg_payload *payload)
{
    struct _bsg_payload_text *tp = (struct _bsg_payload_text *)payload;
    if (!tp)
	return;
    bu_vls_free(&tp->text);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

struct bsg_payload *
bsg_payload_text_create(const char *text, const point_t origin, double scale)
{
    struct _bsg_payload_text *tp = NULL;
    struct bsg_payload *p = NULL;

    BU_ALLOC(tp, struct _bsg_payload_text);
    if (!tp)
	return NULL;
    memset(tp, 0, sizeof(*tp));
    BU_VLS_INIT(&tp->text);

    p = &tp->base;
    p->type = BSG_PAYLOAD_TYPE_TEXT;
    p->free_fn = _payload_text_free;

    if (text)
	bu_vls_sprintf(&tp->text, "%s", text);

    if (origin)
	VMOVE(tp->origin, origin);

    tp->scale = (scale > 0.0) ? scale : 1.0;
    MAT_IDN(tp->rot);
    tp->anchor = BSG_TEXT_ANCHOR_AUTO;

    return p;
}

void
bsg_payload_text_set(struct bsg_payload *text_payload, const char *text)
{
    struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return;
    tp = (struct _bsg_payload_text *)text_payload;
    bu_vls_trunc(&tp->text, 0);
    if (text)
	bu_vls_sprintf(&tp->text, "%s", text);
}

const char *
bsg_payload_text_get(const struct bsg_payload *text_payload)
{
    const struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return NULL;
    tp = (const struct _bsg_payload_text *)text_payload;
    return bu_vls_cstr(&tp->text);
}

void
bsg_payload_text_origin_set(struct bsg_payload *text_payload, const point_t origin)
{
    struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return;
    if (!origin)
	return;
    tp = (struct _bsg_payload_text *)text_payload;
    VMOVE(tp->origin, origin);
}

void
bsg_payload_text_origin_get(const struct bsg_payload *text_payload, point_t out)
{
    const struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return;
    if (!out)
	return;
    tp = (const struct _bsg_payload_text *)text_payload;
    VMOVE(out, tp->origin);
}

void
bsg_payload_text_scale_set(struct bsg_payload *text_payload, double scale)
{
    struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return;
    if (scale <= 0.0)
	return;
    tp = (struct _bsg_payload_text *)text_payload;
    tp->scale = scale;
}

double
bsg_payload_text_scale_get(const struct bsg_payload *text_payload)
{
    const struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return 0.0;
    tp = (const struct _bsg_payload_text *)text_payload;
    return tp->scale;
}

void
bsg_payload_text_rot_set(struct bsg_payload *text_payload, const mat_t rot)
{
    struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return;
    if (!rot)
	return;
    tp = (struct _bsg_payload_text *)text_payload;
    MAT_COPY(tp->rot, rot);
}

void
bsg_payload_text_rot_get(const struct bsg_payload *text_payload, mat_t out)
{
    const struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return;
    if (!out)
	return;
    tp = (const struct _bsg_payload_text *)text_payload;
    MAT_COPY(out, tp->rot);
}

void
bsg_payload_text_anchor_set(struct bsg_payload *text_payload, int anchor)
{
    struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return;
    tp = (struct _bsg_payload_text *)text_payload;
    tp->anchor = anchor;
}

int
bsg_payload_text_anchor_get(const struct bsg_payload *text_payload)
{
    const struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return BSG_TEXT_ANCHOR_AUTO;
    tp = (const struct _bsg_payload_text *)text_payload;
    return tp->anchor;
}

void
bsg_payload_text_line_flag_set(struct bsg_payload *text_payload, int flag)
{
    struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return;
    tp = (struct _bsg_payload_text *)text_payload;
    tp->line_flag = flag;
}

int
bsg_payload_text_line_flag_get(const struct bsg_payload *text_payload)
{
    const struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return 0;
    tp = (const struct _bsg_payload_text *)text_payload;
    return tp->line_flag;
}

void
bsg_payload_text_target_set(struct bsg_payload *text_payload, const point_t target)
{
    struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return;
    if (!target)
	return;
    tp = (struct _bsg_payload_text *)text_payload;
    VMOVE(tp->target, target);
}

void
bsg_payload_text_target_get(const struct bsg_payload *text_payload, point_t out)
{
    const struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return;
    if (!out)
	return;
    tp = (const struct _bsg_payload_text *)text_payload;
    VMOVE(out, tp->target);
}

void
bsg_payload_text_arrow_set(struct bsg_payload *text_payload, int flag)
{
    struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return;
    tp = (struct _bsg_payload_text *)text_payload;
    tp->arrow = flag;
}

int
bsg_payload_text_arrow_get(const struct bsg_payload *text_payload)
{
    const struct _bsg_payload_text *tp = NULL;
    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return 0;
    tp = (const struct _bsg_payload_text *)text_payload;
    return tp->arrow;
}

int
bsg_payload_text_build_vlist(struct bsg_payload *text_payload,
			     struct bu_list *vhead,
			     struct bu_list *vlfree)
{
    const struct _bsg_payload_text *tp = NULL;
    const char *str = NULL;

    if (!text_payload || text_payload->type != BSG_PAYLOAD_TYPE_TEXT)
	return -1;
    if (!vhead || !vlfree)
	return -1;

    tp = (const struct _bsg_payload_text *)text_payload;
    str = bu_vls_cstr(&tp->text);
    if (!str || !str[0])
	return -1;

    bsg_vlist_3string(vhead, vlfree, str, tp->origin, tp->rot, tp->scale);
    return 0;
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
