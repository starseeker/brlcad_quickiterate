/*                     S E N S O R . C
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
/** @file libbsg/sensor.c
 *
 * Phase 6-B: FieldSensor / NodeSensor / TimerSensor implementation.
 *
 * Sensors are stored in a static global registry (BSG_SENSOR_MAX entries).
 * Each entry records:
 *   - the sensor bsg_node handle (used as a unique key)
 *   - the sub-type (BSG_SENSOR_FIELD / NODE / TIMER)
 *   - the watched target node and field_id (FieldSensor / NodeSensor)
 *   - the callback pointer and user data
 *   - an active flag
 *
 * bsg_sensor_notify_field() iterates the registry and fires all matching
 * FieldSensor (target==n && field_id==fid) and NodeSensor (target==n)
 * callbacks.  Thread safety is not required; BRL-CAD's draw path is
 * single-threaded.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/field.h"
#include "bsg/sensor.h"

#define BSG_SENSOR_MAX 256

typedef struct bsg_sensor_entry {
    bsg_node *handle;          /* sensor bsg_node * (key) */
    unsigned long long stype;  /* BSG_SENSOR_FIELD / NODE / TIMER */
    bsg_node *target;
    bsg_field_id_t field_id;
    /* FieldSensor callback */
    int (*field_cb)(bsg_node *, bsg_field_id_t, void *);
    /* NodeSensor callback */
    int (*node_cb)(bsg_node *, void *);
    /* TimerSensor callback */
    int (*timer_cb)(void *);
    void *data;
    int active;
} bsg_sensor_entry;

static bsg_sensor_entry s_registry[BSG_SENSOR_MAX];
static int s_registry_init = 0;

static void
_registry_ensure_init(void)
{
    if (!s_registry_init) {
	memset(s_registry, 0, sizeof(s_registry));
	s_registry_init = 1;
    }
}

static int
_registry_add(bsg_node *handle,
	       unsigned long long stype,
	       bsg_node *target,
	       bsg_field_id_t fid,
	       int (*fcb)(bsg_node *, bsg_field_id_t, void *),
	       int (*ncb)(bsg_node *, void *),
	       int (*tcb)(void *),
	       void *data)
{
    _registry_ensure_init();
    for (int i = 0; i < BSG_SENSOR_MAX; i++) {
	if (!s_registry[i].active) {
	    s_registry[i].handle   = handle;
	    s_registry[i].stype    = stype;
	    s_registry[i].target   = target;
	    s_registry[i].field_id = fid;
	    s_registry[i].field_cb = fcb;
	    s_registry[i].node_cb  = ncb;
	    s_registry[i].timer_cb = tcb;
	    s_registry[i].data     = data;
	    s_registry[i].active   = 1;
	    return 1;
	}
    }
    return 0;  /* registry full */
}

static void
_registry_remove(bsg_node *handle)
{
    _registry_ensure_init();
    for (int i = 0; i < BSG_SENSOR_MAX; i++) {
	if (s_registry[i].active && s_registry[i].handle == handle) {
	    memset(&s_registry[i], 0, sizeof(bsg_sensor_entry));
	    return;
	}
    }
}


/* Allocate a sensor bsg_node from the owning view.  We use bsg_obj_create
 * (which does NOT insert into view tables) to keep the sensor off the draw
 * root's children list (Phase F: bsg_root->children IS gv_draw_root->children). */
static bsg_node *
_alloc_sensor_node(bsg_node *root, unsigned long long stype)
{
    if (!root)
	return NULL;

    bsg_node *r = (bsg_node *)root;
    struct bsg_view *v = r->s_v;
    if (!v)
	return NULL;

    bsg_node *s = bsg_obj_create(v, BSG_OBJ_VIEW | BSG_OBJ_LOCAL);
    if (!s)
	return NULL;

    s->s_type_flags = BSG_NODE_SENSOR | stype;
    return (bsg_node *)s;
}


bsg_node *
bsg_field_sensor_create(bsg_node *root,
			bsg_node *target,
			bsg_field_id_t fid,
			int (*cb)(bsg_node *, bsg_field_id_t, void *),
			void *data)
{
    if (!root || !target || !cb)
	return NULL;

    bsg_node *handle = _alloc_sensor_node(root, BSG_SENSOR_FIELD);
    if (!handle)
	return NULL;

    if (!_registry_add(handle, BSG_SENSOR_FIELD, target, fid,
		       cb, NULL, NULL, data)) {
	bsg_obj_put((bsg_node *)handle);
	return NULL;
    }

    return handle;
}


bsg_node *
bsg_node_sensor_create(bsg_node *root,
		       bsg_node *target,
		       int (*cb)(bsg_node *, void *),
		       void *data)
{
    if (!root || !target || !cb)
	return NULL;

    bsg_node *handle = _alloc_sensor_node(root, BSG_SENSOR_NODE);
    if (!handle)
	return NULL;

    if (!_registry_add(handle, BSG_SENSOR_NODE, target,
		       BSG_FIELD_UNKNOWN, NULL, cb, NULL, data)) {
	bsg_obj_put((bsg_node *)handle);
	return NULL;
    }

    return handle;
}


bsg_node *
bsg_timer_sensor_create(bsg_node *root,
			long UNUSED(interval_ms),
			int (*cb)(void *),
			void *data)
{
    if (!root || !cb)
	return NULL;

    bsg_node *handle = _alloc_sensor_node(root, BSG_SENSOR_TIMER);
    if (!handle)
	return NULL;

    if (!_registry_add(handle, BSG_SENSOR_TIMER, NULL,
		       BSG_FIELD_UNKNOWN, NULL, NULL, cb, data)) {
	bsg_obj_put((bsg_node *)handle);
	return NULL;
    }

    return handle;
}


void
bsg_sensor_destroy(bsg_node *sensor)
{
    if (!sensor)
	return;

    _registry_remove(sensor);
    bsg_obj_put((bsg_node *)sensor);
}


bsg_node *
bsg_sensor_target(bsg_node *sensor)
{
    if (!sensor)
	return NULL;
    _registry_ensure_init();
    for (int i = 0; i < BSG_SENSOR_MAX; i++) {
	if (s_registry[i].active && s_registry[i].handle == sensor)
	    return s_registry[i].target;
    }
    return NULL;
}


void
bsg_sensor_notify_field(bsg_node *target, bsg_field_id_t fid)
{
    if (!target)
	return;

    _registry_ensure_init();
    for (int i = 0; i < BSG_SENSOR_MAX; i++) {
	if (!s_registry[i].active)
	    continue;

	if (s_registry[i].target != target)
	    continue;

	if ((s_registry[i].stype & BSG_SENSOR_FIELD) &&
	    s_registry[i].field_id == fid &&
	    s_registry[i].field_cb) {
	    (*s_registry[i].field_cb)(target, fid, s_registry[i].data);
	    continue;
	}

	if ((s_registry[i].stype & BSG_SENSOR_NODE) &&
	    s_registry[i].node_cb) {
	    (*s_registry[i].node_cb)(target, s_registry[i].data);
	}
    }
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
