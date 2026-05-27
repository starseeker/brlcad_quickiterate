/*                       L I G H T . H
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
 * Renderer-neutral light model (Phase 7).
 *
 * A `bsg_light` represents a single light source with a type
 * (BSG_LIGHT_AMBIENT, BSG_LIGHT_DIRECTIONAL, BSG_LIGHT_POINT or
 * BSG_LIGHT_SPOT), geometric parameters (position, direction), and
 * photometric parameters (diffuse/specular color, intensity,
 * attenuation, spot cone angle).
 *
 * Lights are pure data descriptors; they are not BSG scene nodes.
 * Renderers consume them as part of the render-request contract
 * (see bsg/render.h).
 */
/** @{ */
/* @file bsg/light.h */

#ifndef BSG_LIGHT_H
#define BSG_LIGHT_H

#include "common.h"
#include "vmath.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/** Light type constants */
#define BSG_LIGHT_AMBIENT     0  /**< @brief global ambient term */
#define BSG_LIGHT_DIRECTIONAL 1  /**< @brief infinite directional light */
#define BSG_LIGHT_POINT       2  /**< @brief omnidirectional point light */
#define BSG_LIGHT_SPOT        3  /**< @brief spot light with cone */

/**
 * Renderer-neutral light descriptor.
 *
 * Allocate with bsg_light_create(); release with bsg_light_destroy().
 */
struct bsg_light {
    int     type;               /**< @brief BSG_LIGHT_* constant */
    point_t position;           /**< @brief world-space position (POINT / SPOT) */
    vect_t  direction;          /**< @brief unit direction (DIRECTIONAL / SPOT) */
    float   diffuse[3];         /**< @brief RGB diffuse color [0..1] */
    float   specular[3];        /**< @brief RGB specular color [0..1] */
    float   ambient[3];         /**< @brief RGB ambient color [0..1] (AMBIENT type) */
    fastf_t intensity;          /**< @brief overall intensity scale */
    fastf_t attenuation[3];     /**< @brief constant / linear / quadratic (POINT/SPOT) */
    fastf_t spot_cutoff;        /**< @brief half-angle of spot cone in degrees (SPOT) */
    fastf_t spot_exponent;      /**< @brief spot falloff exponent (SPOT) */
    int     active;             /**< @brief non-zero if this light is enabled */
};

/**
 * Allocate and initialise a light of @p type with sensible defaults:
 *  - white diffuse/specular (1,1,1), intensity 1.0
 *  - attenuation (1, 0, 0) — constant, no falloff
 *  - spot_cutoff 45 degrees, spot_exponent 1
 *  - active = 1
 * Returns NULL on allocation failure.
 */
BSG_EXPORT extern struct bsg_light *
bsg_light_create(int type);

/**
 * Release a light previously allocated by bsg_light_create().
 * No-op if @p light is NULL.
 */
BSG_EXPORT extern void
bsg_light_destroy(struct bsg_light *light);

/** Set world-space position (meaningful for POINT and SPOT lights). */
BSG_EXPORT extern void
bsg_light_set_position(struct bsg_light *light, const point_t pos);

/** Get world-space position. */
BSG_EXPORT extern void
bsg_light_get_position(const struct bsg_light *light, point_t pos);

/** Set unit direction vector (meaningful for DIRECTIONAL and SPOT lights). */
BSG_EXPORT extern void
bsg_light_set_direction(struct bsg_light *light, const vect_t dir);

/** Get direction vector. */
BSG_EXPORT extern void
bsg_light_get_direction(const struct bsg_light *light, vect_t dir);

/** Set diffuse RGB color components in [0..1]. */
BSG_EXPORT extern void
bsg_light_set_diffuse(struct bsg_light *light, float r, float g, float b);

/** Get diffuse RGB color. */
BSG_EXPORT extern void
bsg_light_get_diffuse(const struct bsg_light *light,
		      float *r, float *g, float *b);

/** Set specular RGB color components in [0..1]. */
BSG_EXPORT extern void
bsg_light_set_specular(struct bsg_light *light, float r, float g, float b);

/** Get specular RGB color. */
BSG_EXPORT extern void
bsg_light_get_specular(const struct bsg_light *light,
		       float *r, float *g, float *b);

/** Set ambient RGB color components in [0..1] (used for AMBIENT type). */
BSG_EXPORT extern void
bsg_light_set_ambient(struct bsg_light *light, float r, float g, float b);

/** Get ambient RGB color. */
BSG_EXPORT extern void
bsg_light_get_ambient(const struct bsg_light *light,
		      float *r, float *g, float *b);

/** Set intensity scale. */
BSG_EXPORT extern void
bsg_light_set_intensity(struct bsg_light *light, fastf_t intensity);

/** Get intensity scale. */
BSG_EXPORT extern fastf_t
bsg_light_get_intensity(const struct bsg_light *light);

/** Set attenuation coefficients (constant, linear, quadratic). */
BSG_EXPORT extern void
bsg_light_set_attenuation(struct bsg_light *light,
			  fastf_t constant,
			  fastf_t linear,
			  fastf_t quadratic);

/** Set spot cone half-angle in degrees (SPOT light). */
BSG_EXPORT extern void
bsg_light_set_spot_cutoff(struct bsg_light *light, fastf_t degrees);

/** Get spot cone half-angle in degrees. */
BSG_EXPORT extern fastf_t
bsg_light_get_spot_cutoff(const struct bsg_light *light);

/** Enable or disable the light (active != 0 → enabled). */
BSG_EXPORT extern void
bsg_light_set_active(struct bsg_light *light, int active);

/** Return non-zero if the light is currently active. */
BSG_EXPORT extern int
bsg_light_get_active(const struct bsg_light *light);

/** Return the light type (BSG_LIGHT_* constant). */
BSG_EXPORT extern int
bsg_light_get_type(const struct bsg_light *light);

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
