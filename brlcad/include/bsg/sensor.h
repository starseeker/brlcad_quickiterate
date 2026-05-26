/*                      S E N S O R . H
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
 * Generalized sensor/engine pattern (FieldSensor, NodeSensor, TimerSensor).
 *
 * Sensors live in a global per-process registry (max 256 entries) and do NOT
 * appear in the render children list of the scene root.  Thread safety is not
 * required; BRL-CAD's draw path is single-threaded.
 */
/** @{ */
/* @file bsg/sensor.h */

#ifndef BSG_SENSOR_H
#define BSG_SENSOR_H

#include "common.h"
#include "bsg/defines.h"
#include "bsg/field.h"

__BEGIN_DECLS

/* Sensor sub-type bits stored in s_type_flags alongside BSG_NODE_SENSOR.
 * Bits 36-38 — clear of BSG_NODE_* (bits 28-34) and BSG_PAYLOAD_* (bit 40+). */
#define BSG_SENSOR_FIELD 0x1000000000ULL  /**< @brief FieldSensor sub-type */
#define BSG_SENSOR_NODE  0x2000000000ULL  /**< @brief NodeSensor sub-type  */
#define BSG_SENSOR_TIMER 0x4000000000ULL  /**< @brief TimerSensor sub-type (stub) */

/**
 * FieldSensor: fires when a specific (target, field_id) pair is touched.
 */
struct bsg_field_sensor {
    bsg_node *target;
    bsg_field_id_t field_id;
    int (*cb)(bsg_node *, bsg_field_id_t, void *);
    void *data;
};

/**
 * NodeSensor: fires on any field change to the watched node.
 */
struct bsg_node_sensor {
    bsg_node *target;
    int (*cb)(bsg_node *, void *);
    void *data;
};

/**
 * TimerSensor (stub — callback not yet fired automatically).
 */
struct bsg_timer_sensor {
    long interval_ms;
    int (*cb)(void *);
    void *data;
};

/**
 * Register a FieldSensor that fires @p cb(target, fid, data) whenever
 * bsg_node_field_touch(target, fid) is called.  @p root supplies the owning
 * view so that a bsg_node handle can be allocated for the sensor.
 * Returns the sensor handle (BSG_NODE_SENSOR|BSG_SENSOR_FIELD node) or NULL.
 */
BSG_EXPORT extern bsg_node *
bsg_field_sensor_create(bsg_node *root,
			bsg_node *target,
			bsg_field_id_t fid,
			int (*cb)(bsg_node *, bsg_field_id_t, void *),
			void *data);

/**
 * Register a NodeSensor that fires @p cb(target, data) whenever any field
 * of @p target is touched.  Returns the sensor handle or NULL.
 */
BSG_EXPORT extern bsg_node *
bsg_node_sensor_create(bsg_node *root,
		       bsg_node *target,
		       int (*cb)(bsg_node *, void *),
		       void *data);

/**
 * Register a TimerSensor (stub; the callback is never fired automatically).
 * Returns the sensor handle or NULL.
 */
BSG_EXPORT extern bsg_node *
bsg_timer_sensor_create(bsg_node *root,
			long interval_ms,
			int (*cb)(void *),
			void *data);

/**
 * Deactivate and release the sensor identified by @p sensor.
 * Returns the node to the libbv free pool.
 * No-op if @p sensor is NULL or not found in the registry.
 */
BSG_EXPORT extern void
bsg_sensor_destroy(bsg_node *sensor);

/**
 * Return the watched target node for the sensor identified by @p sensor,
 * or NULL if @p sensor is NULL, not found in the registry, or is a
 * TimerSensor (which does not watch a target).
 *
 * This lets callers treat a registered sensor as the source of truth for
 * the identity it is tracking, rather than maintaining a parallel cache.
 */
BSG_EXPORT extern bsg_node *
bsg_sensor_target(bsg_node *sensor);

/**
 * Internal: walk the global registry and fire all FieldSensor and NodeSensor
 * callbacks watching @p target/@p fid.  Called by bsg_node_field_touch().
 * Not intended for direct use by application code.
 */
BSG_EXPORT extern void
bsg_sensor_notify_field(bsg_node *target, bsg_field_id_t fid);

__END_DECLS

#endif /* BSG_SENSOR_H */

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
