/*                         T I G . H
 * BRL-CAD
 *
 * Copyright (c) 2004-2026 United States Government as represented by
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
/** @addtogroup bsg_plot
 *
 *  The following routines are taken from the BRL TIG-PACK
 *  (Terminal Independent Plotting Package).
 *
 *  Canonical home; bv/tig.h is a backward-compatibility bridge.
 */
/** @{ */
/** @file bsg/tig.h */

#ifndef BSG_TIG_H
#define BSG_TIG_H

#include "common.h"

#include "vmath.h"
#include "bu/defines.h"
#include "bu/color.h"
#include "bu/file.h"
#include "bsg/defines.h"
#include "bsg/plot3.h"

__BEGIN_DECLS

#define PL_FORTRAN(lc, uc)	BU_FORTRAN(lc, uc)

BSG_EXPORT extern void tp_i2list(FILE *fp, int *x, int *y, int npoints);
BSG_EXPORT extern void tp_2list(FILE *fp, double *x, double *y, int npoints);
BSG_EXPORT extern void BU_FORTRAN(f2list, F2LIST)(FILE **fpp, float *x, float *y, int *n);
BSG_EXPORT extern void tp_3list(FILE *fp, double *x, double *y, double *z, int npoints);
BSG_EXPORT extern void BU_FORTRAN(f3list, F3LIST)(FILE **fpp, float *x, float *y, float *z, int *n);
BSG_EXPORT extern void tp_2mlist(FILE *fp, double *x, double *y, int npoints, int flag, int mark, int interval, double size);
BSG_EXPORT extern void BU_FORTRAN(f2mlst, F2MLST)(FILE **fp, float *x, float *y, int *np, int *flag, int *mark, int *interval, float *size);
BSG_EXPORT extern void tp_2marker(FILE *fp, int c, double x, double y, double scale);
BSG_EXPORT extern void BU_FORTRAN(f2mark, F2MARK)(FILE **fp, int *c, float *x, float *y, float *scale);
BSG_EXPORT extern void tp_3marker(FILE *fp, int c, double x, double y, double z, double scale);
BSG_EXPORT extern void BU_FORTRAN(f3mark, F3MARK)(FILE **fp, int *c, float *x, float *y, float *z, float *scale);
BSG_EXPORT extern void tp_2number(FILE *fp, double input, int x, int y, int cscale, double theta, int digits);
BSG_EXPORT extern void BU_FORTRAN(f2numb, F2NUMB)(FILE **fp, float *input, int *x, int *y, float *cscale, float *theta, int *digits);
BSG_EXPORT extern void tp_scale(int idata[], int elements, int mode, int length, int odata[], double *min, double *dx);
BSG_EXPORT extern void BU_FORTRAN(fscale, FSCALE)(int idata[], int *elements, char *mode, int *length, int odata[], double *min, double *dx);
BSG_EXPORT extern void tp_2symbol(FILE *fp, char *string, double x, double y, double scale, double theta);
BSG_EXPORT extern void BU_FORTRAN(f2symb, F2SYMB)(FILE **fp, char *string, float *x, float *y, float *scale, float *theta);
BSG_EXPORT extern void tp_plot(FILE *fp, int xp, int yp, int xl, int yl, char xtitle[], char ytitle[], float x[], float y[], int n, double cscale);
BSG_EXPORT extern void BU_FORTRAN(fplot, FPLOT)(FILE **fp, int *xp, int *yp, int *xl, int *yl, char *xtitle, char *ytitle, float *x, float *y, int *n, float *cscale);
BSG_EXPORT extern void tp_ftoa(float x, char *s);
BSG_EXPORT extern void tp_fixsc(float *x, int npts, float size, float *xs, float *xmin, float *xmax, float *dx);
BSG_EXPORT extern void tp_sep(float x, float *coef, int *ex);
BSG_EXPORT extern double tp_ipow(double x, int n);
BSG_EXPORT extern void tp_3axis(FILE *fp, char *string, point_t origin, mat_t rot, double length, int ccw, int ndigits, double label_start, double label_incr, double tick_separation, double char_width);
BSG_EXPORT extern void BU_FORTRAN(f3axis, F3AXIS)(FILE **fp, char *string, float *x, float *y, float *z, float *length, float *theta, int *ccw, int *ndigits, float *label_start, float *label_incr, float *tick_separation, float *char_width);
BSG_EXPORT extern void tp_3symbol(FILE *fp, char *string, point_t origin, mat_t rot, double scale);
BSG_EXPORT extern void tp_3vector(FILE *plotfp, point_t from, point_t to, double fromheadfract, double toheadfract);
BSG_EXPORT extern void BU_FORTRAN(f3vect, F3VECT)(FILE **fp, float *fx, float *fy, float *fz, float *tx, float *ty, float *tz, float *fl, float *tl);

__END_DECLS

#endif /* BSG_TIG_H */

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
