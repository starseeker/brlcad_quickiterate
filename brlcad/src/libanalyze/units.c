/*                        U N I T S . C
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
/** @file libanalyze/units.c
 *
 * Shared unit-conversion tables and parsing utility used by analysis
 * front-ends (check, gqa, gchecker, …).
 *
 * The tables were previously duplicated verbatim in both check_private.h
 * and gqa.cpp; this file provides a single authoritative definition that
 * both tools reference via the analyze_units_tab[] extern.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/str.h"
#include "bu/vls.h"
#include "analyze/units.h"

/**
 * analyze_units_tab - shared unit-conversion tables for length, volume, mass.
 *
 * Row 0 (ANALYZE_UNITS_LENGTH): conversion factors from named units to mm.
 * Row 1 (ANALYZE_UNITS_VOLUME): conversion factors from named units to mm^3.
 * Row 2 (ANALYZE_UNITS_MASS):   conversion factors from named units to grams.
 *
 * The first entry in every row is the library-internal default unit.
 * Each row is terminated by an entry with an empty name string.
 */
const struct analyze_cvt_tab analyze_units_tab[3][40] = {
    {
	/* ---- length (to mm) ---- */
	{1.0,		"mm"},		/* default */
	{1.0e-7,	"angstrom"},
	{1.0e-7,	"decinanometer"},
	{1.0e-6,	"nm"},
	{1.0e-6,	"nanometer"},
	{1.0e-3,	"um"},
	{1.0e-3,	"micrometer"},
	{1.0e-3,	"micron"},
	{1.0,		"millimeter"},
	{10.0,		"cm"},
	{10.0,		"centimeter"},
	{1000.0,	"m"},
	{1000.0,	"meter"},
	{1000000.0,	"km"},
	{1000000.0,	"kilometer"},
	{25.4,		"in"},
	{25.4,		"inch"},
	{25.4,		"inches"},
	{304.8,		"ft"},
	{304.8,		"foot"},
	{304.8,		"feet"},
	{456.2,		"cubit"},
	{914.4,		"yd"},
	{914.4,		"yard"},
	{5029.2,	"rd"},
	{5029.2,	"rod"},
	{1609344.0,	"mi"},
	{1609344.0,	"mile"},
	{1852000.0,	"nmile"},
	{1852000.0,	"nautical mile"},
	{1.495979e+14,	"AU"},
	{1.495979e+14,	"astronomical unit"},
	{9.460730e+18,	"lightyear"},
	{3.085678e+19,	"pc"},
	{3.085678e+19,	"parsec"},
	{0.0,		""}		/* sentinel */
    },
    {
	/* ---- volume (to mm^3) ---- */
	{1.0,		"cu mm"},	/* default */
	{1.0,		"mm"},
	{1.0,		"mm^3"},
	{1.0e3,		"cm"},
	{1.0e3,		"cm^3"},
	{1.0e3,		"cu cm"},
	{1.0e3,		"cc"},
	{1.0e6,		"l"},
	{1.0e6,		"liter"},
	{1.0e6,		"litre"},
	{1.0e9,		"m"},
	{1.0e9,		"m^3"},
	{1.0e9,		"cu m"},
	{16387.064,	"in"},
	{16387.064,	"in^3"},
	{16387.064,	"cu in"},
	{28316846.592,	"ft"},
	{28316846.592,	"ft^3"},
	{28316846.592,	"cu ft"},
	{764554857.984,	"yds"},
	{764554857.984,	"yards"},
	{764554857.984,	"cu yards"},
	{0.0,		""}		/* sentinel */
    },
    {
	/* ---- mass (to grams) ---- */
	{1.0,		"grams"},	/* default */
	{1.0,		"g"},
	{0.0648,	"gr"},
	{0.0648,	"grains"},
	{1.0e3,		"kg"},
	{1.0e3,		"kilos"},
	{1.0e3,		"kilograms"},
	{28.35,		"oz"},
	{28.35,		"ounce"},
	{453.6,		"lb"},
	{453.6,		"lbs"},
	{0.0,		""}		/* sentinel */
    }
};


int
analyze_parse_units_double(struct bu_vls *msgs, double *val,
			   const char *buf,
			   const struct analyze_cvt_tab *cvt)
{
    double a;
#define UNITS_STR_SZ 256
    char units_string[UNITS_STR_SZ + 1] = {0};
    int i;
    const struct analyze_cvt_tab *cv;

    i = sscanf(buf, "%lg %256s", &a, units_string);

    if (i < 0) {
	if (msgs)
	    bu_vls_printf(msgs, "analyze_parse_units_double: empty input\n");
	return 1;
    }

    if (i == 1) {
	*val = a;
	return 0;
    }

    if (i == 2) {
	*val = a;
	for (cv = cvt; cv->name[0] != '\0'; cv++) {
	    if (bu_strncmp(cv->name, units_string, sizeof(units_string)) == 0) {
		*val = a * cv->val;
		return 0;
	    }
	}
	if (msgs)
	    bu_vls_printf(msgs, "Bad units specifier \"%s\" on value \"%s\"\n",
			 units_string, buf);
	return 1;
    }

    if (msgs)
	bu_vls_printf(msgs, "analyze_parse_units_double: sscanf problem on \"%s\" (got %d)\n",
		      buf, i);
    return 1;
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
