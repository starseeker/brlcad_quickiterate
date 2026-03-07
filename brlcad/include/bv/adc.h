/*                        A D C . H
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
/** @addtogroup bv_adc
 */
/** @{ */
/** @file bv/adc.h */

#ifndef BG_ADC_H
#define BG_ADC_H

#include "common.h"
#include "vmath.h"
#include "bv/defines.h"
#include "bsg/adc.h"

__BEGIN_DECLS

#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif
static inline void
adc_model_to_adc_view(struct bv_adc_state *adcs, mat_t model2view, fastf_t amax) {
    bsg_adc_model_to_view(adcs, model2view, amax);
}
static inline void
adc_grid_to_adc_view(struct bv_adc_state *adcs, mat_t model2view, fastf_t amax) {
    bsg_adc_grid_to_view(adcs, model2view, amax);
}
static inline void
adc_view_to_adc_grid(struct bv_adc_state *adcs, mat_t model2view) {
    bsg_adc_view_to_grid(adcs, model2view);
}
static inline void
adc_reset(struct bv_adc_state *adcs, mat_t view2model, mat_t model2view) {
    bsg_adc_reset(adcs, view2model, model2view);
}
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

__END_DECLS

#endif  /* BG_ADC_H */

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
