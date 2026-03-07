/*                     B S G / A D C . H
 * BRL-CAD
 *
 * Copyright (c) 2004-2025 United States Government as represented by
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
/** @file bsg/adc.h
 *
 * @brief BSG angular-deviation cursor (ADC) overlay functions.
 *
 * Phase 1: thin wrapper around @c bv/adc.h.  When libbv is removed the
 * ADC function declarations will move here directly.
 */

#ifndef BSG_ADC_H
#define BSG_ADC_H

#include "bv/adc.h"

/* Phase 1 convenience aliases using bsg_ types */
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * @brief Update ADC model-space position from view matrix.
 * Phase 1 alias; calls through to adc_model_to_adc_view().
 */
static inline void
bsg_adc_model_to_view(bsg_adc_state *adcs, mat_t model2view, fastf_t amax)
{
    adc_model_to_adc_view(adcs, model2view, amax);
}

/**
 * @brief Update ADC grid-space position from view matrix.
 * Phase 1 alias; calls through to adc_grid_to_adc_view().
 */
static inline void
bsg_adc_grid_to_view(bsg_adc_state *adcs, mat_t model2view, fastf_t amax)
{
    adc_grid_to_adc_view(adcs, model2view, amax);
}

/**
 * @brief Update ADC view-to-grid mapping.
 * Phase 1 alias; calls through to adc_view_to_adc_grid().
 */
static inline void
bsg_adc_view_to_grid(bsg_adc_state *adcs, mat_t model2view)
{
    adc_view_to_adc_grid(adcs, model2view);
}

/**
 * @brief Reset ADC state to defaults.
 * Phase 1 alias; calls through to adc_reset().
 */
static inline void
bsg_adc_reset(bsg_adc_state *adcs, mat_t view2model, mat_t model2view)
{
    adc_reset(adcs, view2model, model2view);
}

__END_DECLS

#endif /* BSG_ADC_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
