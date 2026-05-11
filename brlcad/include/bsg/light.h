/*                      L I G H T . H
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
 * Phase 7 light data model for BSG.
 *
 * Lights are scene or view-scoped BSG objects.  bsg_light describes a
 * single light source; bsg_light_set holds an array of lights for
 * consumption by renderers.
 *
 * The existing dm_set_light() / dm_get_light() boolean remains the
 * compatibility control that enables a default OpenGL light setup.
 * The BSG light API adds a renderer-neutral description layer on top:
 *   - bsg_light_set_create_default() produces a standard one-ambient +
 *     one-directional default light set matching the legacy GL behavior.
 *   - bsg_scene_light_set_get() retrieves (and optionally creates) the
 *     light set attached to a draw-root node.
 *   - bsg_scene_light_enable() mirrors dm_set_light() semantics into the
 *     per-root BSG light state.
 */
/** @{ */
/* @file bsg/light.h */

#ifndef BSG_LIGHT_H
#define BSG_LIGHT_H

#include "common.h"

#include "vmath.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * Kind of light source.
 */
enum bsg_light_kind {
    BSG_LIGHT_AMBIENT     = 0, /**< @brief ambient/hemisphere light */
    BSG_LIGHT_DIRECTIONAL = 1, /**< @brief infinite directional light */
    BSG_LIGHT_POINT       = 2, /**< @brief point light at a position */
    BSG_LIGHT_SPOT        = 3  /**< @brief spot light (cone) */
};

/**
 * A single light source description.
 *
 * Color and intensity are stored separately so renderers that work with
 * linear HDR values can use intensity directly.
 */
struct bsg_light {
    enum bsg_light_kind kind;       /**< @brief light kind */
    int                 enabled;    /**< @brief non-zero when light is active */
    int                 view_scoped; /**< @brief non-zero: position/dir in view space; 0: model/world space */

    fastf_t  color[3];      /**< @brief RGB color [0..1] */
    fastf_t  intensity;     /**< @brief multiplier applied to color */
    fastf_t  ambient[3];    /**< @brief separate ambient contribution [0..1] */

    point_t  position;      /**< @brief position (used by POINT and SPOT) */
    vect_t   direction;     /**< @brief direction (used by DIRECTIONAL and SPOT; unit vector) */

    /* Spot-light parameters */
    fastf_t  spot_cutoff;   /**< @brief half-angle of spot cone in degrees */
    fastf_t  spot_exponent; /**< @brief falloff exponent for spot */
};

/**
 * An ordered set of bsg_light records for a scene or view.
 * Do not access _lights/_cap directly; use the accessor API.
 */
struct bsg_light_set {
    size_t          count;    /**< @brief number of valid lights */
    size_t          _cap;     /**< @brief allocated capacity */
    struct bsg_light *_lights; /**< @brief owned array; do not use directly */
    int             enabled;  /**< @brief overall lighting on/off (mirrors dm_set_light) */
};


/* ---------------------------------------------------------------------- */
/* Light lifecycle                                                          */
/* ---------------------------------------------------------------------- */

/**
 * Initialize @p light to a disabled ambient default.
 * No-op if @p light is NULL.
 */
BSG_EXPORT extern void
bsg_light_init(struct bsg_light *light);

/**
 * Allocate and return an empty bsg_light_set.
 * Returns NULL on allocation failure.
 * Free with bsg_light_set_destroy().
 */
BSG_EXPORT extern struct bsg_light_set *
bsg_light_set_create(void);

/**
 * Free @p ls and all memory associated with it.
 * No-op if @p ls is NULL.
 */
BSG_EXPORT extern void
bsg_light_set_destroy(struct bsg_light_set *ls);

/**
 * Remove all lights from @p ls without destroying the set.
 * No-op if @p ls is NULL.
 */
BSG_EXPORT extern void
bsg_light_set_clear(struct bsg_light_set *ls);

/**
 * Add a copy of @p light to @p ls.
 * Returns 1 on success, 0 on failure.
 */
BSG_EXPORT extern int
bsg_light_set_add(struct bsg_light_set *ls, const struct bsg_light *light);

/**
 * Return the number of lights in @p ls.  Returns 0 if NULL.
 */
BSG_EXPORT extern size_t
bsg_light_set_count(const struct bsg_light_set *ls);

/**
 * Return a pointer to the light at @p index in @p ls.
 * Returns NULL if @p ls is NULL or @p index is out of range.
 * The pointer is valid until the set is modified.
 */
BSG_EXPORT extern const struct bsg_light *
bsg_light_set_get(const struct bsg_light_set *ls, size_t index);


/* ---------------------------------------------------------------------- */
/* Default light helpers                                                    */
/* ---------------------------------------------------------------------- */

/**
 * Populate @p ls with a default light set matching the legacy GL single-
 * light setup:
 *   - one ambient light  (color 0.2/0.2/0.2, intensity 1.0)
 *   - one directional key light (color 1.0/1.0/1.0, intensity 1.0,
 *     direction in view space (0, 0, -1) by convention)
 *
 * Existing lights in @p ls are cleared first.
 * @p ls->enabled is set to 1.
 * No-op if @p ls is NULL.
 */
BSG_EXPORT extern void
bsg_light_set_create_default(struct bsg_light_set *ls);


/* ---------------------------------------------------------------------- */
/* Scene-root light set registry (Phase 7C)                                */
/* ---------------------------------------------------------------------- */

/**
 * Look up the bsg_light_set registered on @p root.
 * When @p create is non-zero the set is created (with defaults) if not
 * present.  Returns NULL when the set does not exist and @p create is 0,
 * or on failure.
 */
BSG_EXPORT extern struct bsg_light_set *
bsg_scene_light_set_get(bsg_node *root, int create);

/**
 * Enable or disable lighting on the scene light set attached to @p root.
 * When @p enable is non-zero the default light set is created if absent
 * and ls->enabled is set to 1.  When @p enable is 0 ls->enabled is set
 * to 0 (lights are disabled but not destroyed).
 * No-op if @p root is NULL.
 */
BSG_EXPORT extern void
bsg_scene_light_enable(bsg_node *root, int enable);

/**
 * Return the current lighting-enabled state for @p root, or 0 if @p root
 * has no registered light set.
 */
BSG_EXPORT extern int
bsg_scene_light_is_enabled(const bsg_node *root);

__END_DECLS

#endif /* BSG_LIGHT_H */

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
