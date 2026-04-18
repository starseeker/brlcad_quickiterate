/*                        U N I T S . H
 * BRL-CAD
 *
 * Copyright (c) 2008-2025 United States Government as represented by
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
/** @addtogroup libanalyze
 *
 * Shared unit-conversion tables used by analysis front-ends (check, gqa, …).
 *
 */
/** @{ */
/** @file analyze/units.h */

#ifndef ANALYZE_UNITS_H
#define ANALYZE_UNITS_H

#include "common.h"
#include "bu/vls.h"
#include "analyze/defines.h"

__BEGIN_DECLS

/**
 * One entry in a unit-conversion table.
 *
 * val  - conversion factor from this unit to the library-internal unit
 *        (mm for length, mm^3 for volume, grams for mass).
 * name - human-readable unit name used on the command line.
 *        An empty string marks the sentinel (last) entry.
 */
struct analyze_cvt_tab {
    double val;
    char   name[32];
};

/** Index constants for the three-dimensional analyze_units_tab[][]. */
#define ANALYZE_UNITS_LENGTH 0  /**< index into analyze_units_tab for length */
#define ANALYZE_UNITS_VOLUME 1  /**< index into analyze_units_tab for volume */
#define ANALYZE_UNITS_MASS   2  /**< index into analyze_units_tab for mass   */

/**
 * Conversion tables for length (mm), volume (mm^3), and mass (grams).
 *
 * Each row is terminated by an entry whose name is an empty string.
 * The first entry in every row is the library-internal default unit.
 *
 * Usage:
 *   const struct analyze_cvt_tab *len = analyze_units_tab[ANALYZE_UNITS_LENGTH];
 */
ANALYZE_EXPORT extern const struct analyze_cvt_tab analyze_units_tab[3][40];


/**
 * Parse a floating-point value optionally followed by a unit string.
 *
 * The function reads @p buf with sscanf("%lg<units>"), looks up the unit
 * in @p cvt, multiplies by the conversion factor, and stores the result in
 * @p val.  Error messages (if any) are appended to @p msgs.
 *
 * @param msgs destination vls for error messages; may be NULL.
 * @param val  output: parsed value in internal units (mm / mm^3 / grams).
 * @param buf  input string, e.g. "5.0mm", "1in", "0.5".
 * @param cvt  conversion table row (one of analyze_units_tab[*]).
 * @return 0 on success, 1 on parse error.
 */
ANALYZE_EXPORT extern int
analyze_parse_units_double(struct bu_vls *msgs, double *val,
			   const char *buf,
			   const struct analyze_cvt_tab *cvt);

__END_DECLS

#endif /* ANALYZE_UNITS_H */

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
