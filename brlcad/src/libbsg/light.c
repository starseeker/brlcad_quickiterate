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
 * Phase 7: Renderer-neutral light model.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "vmath.h"

#include "bsg/defines.h"
#include "bsg/light.h"


struct bsg_light *
bsg_light_create(int type)
{
    struct bsg_light *light;
    BU_ALLOC(light, struct bsg_light);
    memset(light, 0, sizeof(struct bsg_light));

    light->type       = type;
    light->intensity  = 1.0;
    light->active     = 1;

    VSETALL(light->position,  0.0);
    VSETALL(light->direction, 0.0);
    light->direction[2] = -1.0;  /* default: pointing down -Z */

    light->diffuse[0] = light->diffuse[1] = light->diffuse[2]   = 1.0f;
    light->specular[0] = light->specular[1] = light->specular[2] = 1.0f;
    light->ambient[0] = light->ambient[1] = light->ambient[2]   = 0.2f;

    light->attenuation[0] = 1.0;  /* constant */
    light->attenuation[1] = 0.0;  /* linear */
    light->attenuation[2] = 0.0;  /* quadratic */

    light->spot_cutoff   = 45.0;
    light->spot_exponent = 1.0;

    return light;
}


void
bsg_light_destroy(struct bsg_light *light)
{
    if (!light)
	return;
    bu_free(light, "bsg_light");
}


void
bsg_light_set_position(struct bsg_light *light, const point_t pos)
{
    if (!light) return;
    VMOVE(light->position, pos);
}


void
bsg_light_get_position(const struct bsg_light *light, point_t pos)
{
    if (!light) { VSETALL(pos, 0.0); return; }
    VMOVE(pos, light->position);
}


void
bsg_light_set_direction(struct bsg_light *light, const vect_t dir)
{
    if (!light) return;
    VMOVE(light->direction, dir);
}


void
bsg_light_get_direction(const struct bsg_light *light, vect_t dir)
{
    if (!light) { VSETALL(dir, 0.0); return; }
    VMOVE(dir, light->direction);
}


void
bsg_light_set_diffuse(struct bsg_light *light, float r, float g, float b)
{
    if (!light) return;
    light->diffuse[0] = r;
    light->diffuse[1] = g;
    light->diffuse[2] = b;
}


void
bsg_light_get_diffuse(const struct bsg_light *light,
		      float *r, float *g, float *b)
{
    if (!light) { if (r) *r=0; if (g) *g=0; if (b) *b=0; return; }
    if (r) *r = light->diffuse[0];
    if (g) *g = light->diffuse[1];
    if (b) *b = light->diffuse[2];
}


void
bsg_light_set_specular(struct bsg_light *light, float r, float g, float b)
{
    if (!light) return;
    light->specular[0] = r;
    light->specular[1] = g;
    light->specular[2] = b;
}


void
bsg_light_get_specular(const struct bsg_light *light,
		       float *r, float *g, float *b)
{
    if (!light) { if (r) *r=0; if (g) *g=0; if (b) *b=0; return; }
    if (r) *r = light->specular[0];
    if (g) *g = light->specular[1];
    if (b) *b = light->specular[2];
}


void
bsg_light_set_ambient(struct bsg_light *light, float r, float g, float b)
{
    if (!light) return;
    light->ambient[0] = r;
    light->ambient[1] = g;
    light->ambient[2] = b;
}


void
bsg_light_get_ambient(const struct bsg_light *light,
		      float *r, float *g, float *b)
{
    if (!light) { if (r) *r=0; if (g) *g=0; if (b) *b=0; return; }
    if (r) *r = light->ambient[0];
    if (g) *g = light->ambient[1];
    if (b) *b = light->ambient[2];
}


void
bsg_light_set_intensity(struct bsg_light *light, fastf_t intensity)
{
    if (!light) return;
    light->intensity = intensity;
}


fastf_t
bsg_light_get_intensity(const struct bsg_light *light)
{
    if (!light) return 0.0;
    return light->intensity;
}


void
bsg_light_set_attenuation(struct bsg_light *light,
			  fastf_t constant,
			  fastf_t linear,
			  fastf_t quadratic)
{
    if (!light) return;
    light->attenuation[0] = constant;
    light->attenuation[1] = linear;
    light->attenuation[2] = quadratic;
}


void
bsg_light_set_spot_cutoff(struct bsg_light *light, fastf_t degrees)
{
    if (!light) return;
    light->spot_cutoff = degrees;
}


fastf_t
bsg_light_get_spot_cutoff(const struct bsg_light *light)
{
    if (!light) return 0.0;
    return light->spot_cutoff;
}


void
bsg_light_set_active(struct bsg_light *light, int active)
{
    if (!light) return;
    light->active = active;
}


int
bsg_light_get_active(const struct bsg_light *light)
{
    if (!light) return 0;
    return light->active;
}


int
bsg_light_get_type(const struct bsg_light *light)
{
    if (!light) return -1;
    return light->type;
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
