/*                         G Q A . C
 * BRL-CAD
 *
 * Copyright (c) 2008-2026 United States Government as represented by
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
/** @file libged/gqa.c
 *
 * performs a set of quantitative analyses on geometry.
 *
 * XXX need to look at gap computation
 *
 * plot the points where overlaps start/stop
 *
 * Designed to be a framework for 3d sampling of the geometry volume.
 * TODO: Need to move the sample pattern logic into LIBRT.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <limits.h>			/* home of INT_MAX aka MAXINT */


#include "bu/parallel.h"
#include "bu/getopt.h"
#include "vmath.h"
#include "raytrace.h"
#include "bv/plot3.h"
#include "analyze.h"

#include "../ged_private.h"

/* bu_getopt() options */
const char *options = "A:a:de:f:g:Gm:n:N:p:P:qrS:s:t:U:u:vV:W:h?";
const char *options_str = "[-A A|a|b|c|e|g|m|o|p|s|v|w] [-a az] [-d] [-e el] [-f densityFile] [-g spacing|upper,lower|upper-lower] [-G] [-m legacy|rotated|crofton] [-n nhits] [-N nviews] [-p plotPrefix] [-P ncpus] [-q] [-r] [-S nsamples] [-t overlap_tol] [-U useair] [-u len_units vol_units wt_units] [-v] [-V volume_tol] [-W weight_tol]";

#define ANALYSIS_VOLUMES          1
#define ANALYSIS_WEIGHTS          2
#define ANALYSIS_OVERLAPS         4
#define ANALYSIS_ADJ_AIR          8 /* adjacent air */
#define ANALYSIS_GAPS            16 /* space between regions */
#define ANALYSIS_EXP_AIR         32 /* exposed air */
#define ANALYSIS_BBOX            64 /* overall bounding box */
#define ANALYSIS_INTERFACES     128
#define ANALYSIS_CENTROIDS      256
#define ANALYSIS_MOMENTS        512
#define ANALYSIS_PLOT_OVERLAPS 1024
/* Surface area: separate libanalyze pass (distinct bit from the above) */
#define GQA_ANALYSIS_SURF_AREA 2048

/* Mask of analysis types handled by gqa's own ray loop.
 * GQA_ANALYSIS_SURF_AREA is NOT in this mask; it runs via a separate
 * libanalyze perform_raytracing() pass after the main loop. */
#define GQA_NATIVE_FLAGS (ANALYSIS_VOLUMES | ANALYSIS_WEIGHTS | ANALYSIS_OVERLAPS | \
			  ANALYSIS_ADJ_AIR | ANALYSIS_GAPS | ANALYSIS_EXP_AIR | \
			  ANALYSIS_BBOX | ANALYSIS_CENTROIDS | ANALYSIS_MOMENTS | \
			  ANALYSIS_PLOT_OVERLAPS)

#define MAX_MATERIAL_ID  32768

/* Sampling method codes */
#define GQA_METHOD_LEGACY  0  /**< original axis-aligned triple grid */
#define GQA_METHOD_ROTATED 1  /**< rotated triple grid using -a/-e orientation */
#define GQA_METHOD_CROFTON 2  /**< Crofton isotropic random sampling */

/* Note: struct parsing requires no space after the commas.  take care
 * when formatting this file.  if the compile breaks here, it means
 * that spaces got inserted incorrectly.
 */
#define COMMA ','

static const double GRIDSPACING_STEP = 1.0 / 2.0;

/* Plot line colors (immutable, OK as static constants) */
static const int overlap_color[3] = { 255, 255, 0 };   /* yellow */
static const int gap_color[3]     = { 128, 192, 255 };  /* cyan */
static const int adjAir_color[3]  = { 128, 255, 192 };  /* pale green */
static const int expAir_color[3]  = { 255, 128, 255 };  /* magenta */

#define DLOG if (state->debug) bu_vls_printf

/* Some defines for re-using the values from the application structure
 * for other purposes
 */
#define A_LENDEN a_color[0]
#define A_LEN a_color[1]
#define A_STATE a_uptr

/* per-object accumulation table (local to gqa, distinct from libanalyze's) */
struct gqa_per_obj_data {
    const char *o_name;
    double *o_len;
    double *o_lenDensity;
    double *o_volume;
    double *o_weight;
    fastf_t *o_lenTorque; /* torque vector for each view */
    fastf_t *o_moi;       /* one vector per view for collecting the partial moments of inertia calculation */
    fastf_t *o_poi;       /* one vector per view for collecting the partial products of inertia calculation */
};

/* per-region accumulation table (local to gqa, distinct from libanalyze's) */
struct gqa_per_region_data {
    unsigned long hits;
    double *r_lenDensity; /* for per-region per-view weight computation */
    double *r_len;        /* for per-region, per-view computation */
    double *r_weight;
    double *r_volume;
    struct gqa_per_obj_data *optr;
};


struct cstate {
    struct ged *gedp;
    int curr_view; /* the "view" number we are shooting */
    int u_axis;    /* these 3 are in the range 0..2 inclusive and indicate which axis (X, Y, or Z) */
    int v_axis;    /* is being used for the U, V, or invariant vector direction */
    int i_axis;

    int sem_lists;
    int sem_worker;
    int sem_plot;

    /* sem_worker protects this */
    int v;         /* indicates how many "grid_size" steps in the v direction have been taken */

    int sem_stats;

    /* sem_stats protects this */
    double *m_lenDensity;
    double *m_len;
    double *m_volume;
    double *m_weight;
    unsigned long *shots;
    int first;     /* this is the first time we've computed a set of views */

    vect_t u_dir;  /* direction of U vector for "current view" */
    vect_t v_dir;  /* direction of V vector for "current view" */
    struct rt_i *rtip;
    long steps[3]; /* this is per-dimension, not per-view */
    vect_t span;   /* How much space does the geometry span in each of X, Y, Z directions */
    vect_t area;   /* area of the view for view with invariant at index */

    fastf_t *m_lenTorque; /* torque vector for each view */
    fastf_t *m_moi;       /* one vector per view for collecting the partial moments of inertia calculation */
    fastf_t *m_poi;       /* one vector per view for collecting the partial products of inertia calculation */

    struct resource *resp;

    /* --- Per-invocation state (moved from global statics) --- */
    int analysis_flags;
    int multiple_analyses;
    double azimuth_deg;
    double elevation_deg;
    char *densityFileName;
    double gridSpacing;
    double gridSpacingLimit;
    char makeOverlapAssemblies;
    size_t require_num_hits;
    int ncpu;
    int max_cpus;
    double Samples_per_model_axis;
    double overlap_tolerance;
    double volume_tolerance;
    double weight_tolerance;
    int aborted;
    int print_per_region_stats;
    int max_region_name_len;
    int use_air;
    int num_objects;
    int num_views;
    int verbose;
    int quiet_missed_report;
    int debug;

    const char *plot_prefix;
    FILE *plot_weight;
    FILE *plot_volume;
    FILE *plot_overlaps;
    FILE *plot_adjair;
    FILE *plot_gaps;
    FILE *plot_expair;

    /* Plot VLIST for in-GED overlap display */
    struct bv_vlblock *plot_vbp;
    struct bu_list *plot_vhead;

    /* Region pair lists (protected by sem_lists) */
    struct region_pair gapList;
    struct region_pair adjAirList;
    struct region_pair exposedAirList;
    struct region_pair overlapList;

    /* Per-object / per-region accumulation tables */
    struct gqa_per_obj_data *obj_tbl;
    struct gqa_per_region_data *reg_tbl;

    /* Units conversion pointers */
    const struct cvt_tab *units[3];

    /* Density table */
    struct analyze_densities *densities;
    char *densities_source;

    /* --- Advanced sampling mode --- */
    int analysis_method; /* GQA_METHOD_LEGACY / GQA_METHOD_ROTATED / GQA_METHOD_CROFTON */
    struct rotated_grid rot_grid[3]; /* pre-computed rotated views, one per state->num_views */
};


/* Access to these lists should be in sections
 * of code protected by state->sem_lists
 * (lists now per-invocation inside struct cstate)
 */


/**
 * This structure holds the name of a unit value, and the conversion
 * factor necessary to convert from/to BRL-CAD standard units.
 *
 * The standard units are millimeters, cubic millimeters, and grams.
 *
 * XXX this section should be extracted to libbu/units.c
 */
struct cvt_tab {
    double val;
    char name[32];
};


static const struct cvt_tab units_tab[3][40] = {
    {
	/* length, stolen from bu/units.c with the "none" value
	 * removed Values for converting from given units to mm
	 */
	{1.0,		"mm"}, /* default */
	/* {0.0,		"none"}, */ /* this is removed to force a certain
					     * amount of error checking for the user
					     */
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
	{25.4,		"inches"},		/* for plural */
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
	{0.0,		""}			/* LAST ENTRY */
    },
    {
	/* volume
	 * Values for converting from given units to mm^3
	 */
	{1.0, "cu mm"}, /* default */

	{1.0, "mm"},
	{1.0, "mm^3"},

	{1.0e3, "cm"},
	{1.0e3, "cm^3"},
	{1.0e3, "cu cm"},
	{1.0e3, "cc"},

	{1.0e6, "l"},
	{1.0e6, "liter"},
	{1.0e6, "litre"},

	{1.0e9, "m"},
	{1.0e9, "m^3"},
	{1.0e9, "cu m"},

	{16387.064, "in"},
	{16387.064, "in^3"},
	{16387.064, "cu in"},

	{28316846.592, "ft"},

	{28316846.592, "ft^3"},
	{28316846.592, "cu ft"},

	{764554857.984, "yds"},
	{764554857.984, "yards"},
	{764554857.984, "cu yards"},

	{0.0,		""}			/* LAST ENTRY */
    },
    {
	/* weight
	 * Values for converting given units to grams
	 */
	{1.0, "grams"}, /* default */

	{1.0, "g"},
	{0.0648, "gr"},
	{0.0648, "grains"},

	{1.0e3, "kg"},
	{1.0e3, "kilos"},
	{1.0e3, "kilograms"},

	{28.35, "oz"},
	{28.35, "ounce"},

	{453.6, "lb"},
	{453.6, "lbs"},
	{0.0,		""}			/* LAST ENTRY */
    }
};


/* this table keeps track of the "current" or "user selected units and
 * the associated conversion values
 */
#define LINE 0
#define VOL 1
#define WGT 2

/* Default units (also initialised per-invocation in ged_gqa_core) */
static const struct cvt_tab * const units_tab_defaults[3] = {
    &units_tab[0][0],	/* linear */
    &units_tab[1][0],	/* volume */
    &units_tab[2][0]	/* weight */
};

/**
 * _gqa_read_units_double
 *
 * Read a non-negative floating point value with optional units
 *
 * Return
 * 1 Failure
 * 0 Success
 */
int
_gqa_read_units_double(struct ged *gedp, double *val, char *buf, const struct cvt_tab *cvt)
{
    double a;
#define UNITS_STRING_SZ 256
    char units_string[UNITS_STRING_SZ+1] = {0};
    int i;


    i = sscanf(buf, "%lg" CPP_SCAN(UNITS_STRING_SZ), &a, units_string);

    if (i < 0) return 1;

    if (i == 1) {
	*val = a;

	return 0;
    }
    if (i == 2) {
	*val = a;
	for (; cvt->name[0] != '\0';) {
	    if (!bu_strncmp(cvt->name, units_string, sizeof(units_string))) {
		goto found_units;
	    } else {
		cvt++;
	    }
	}
	bu_vls_printf(gedp->ged_result_str, "Bad units specifier \"%s\" on value \"%s\"\n", units_string, buf);
	return 1;

    found_units:
	*val = a * cvt->val;
	return 0;
    }
    bu_vls_printf(gedp->ged_result_str, "%s sscanf problem on \"%s\" got %d\n", CPP_FILELINE, buf, i);
    return 1;
}


/* the above should be extracted to libbu/units.c */


/**
 * Parse through command line flags
 */
static int
parse_args(struct ged *gedp, struct cstate *state, int ac, char *av[])
{
    int c;
    int i;
    double a;
    char *p;

    /* Turn off getopt's error messages */
    bu_opterr = 0;
    bu_optind = 1;

    /* get all the option flags from the command line */
    while ((c=bu_getopt(ac, av, options)) != -1) {
	switch (c) {
	    case 'A':
		{
		    state->analysis_flags = 0;
		    state->multiple_analyses = 0;
		    for (p = bu_optarg; *p; p++) {
			switch (*p) {
			    case 'A' :
				state->multiple_analyses = 1;
				state->analysis_flags = state->analysis_flags \
				| ANALYSIS_ADJ_AIR \
				| ANALYSIS_BBOX \
				| ANALYSIS_CENTROIDS \
				| ANALYSIS_EXP_AIR \
				| ANALYSIS_GAPS \
				| ANALYSIS_MOMENTS \
				| ANALYSIS_OVERLAPS \
				| ANALYSIS_VOLUMES \
				| ANALYSIS_WEIGHTS \
				| GQA_ANALYSIS_SURF_AREA;
				break;
			    case 'a' :
				if (state->analysis_flags)
				    state->multiple_analyses = 1;

				state->analysis_flags |= ANALYSIS_ADJ_AIR;

				break;
			    case 'b' :
				if (state->analysis_flags)
				    state->multiple_analyses = 1;

				state->analysis_flags |= ANALYSIS_BBOX;

				break;
			    case 'c' :
				if (state->analysis_flags)
				    state->multiple_analyses = 1;

				state->analysis_flags |= ANALYSIS_WEIGHTS;
				state->analysis_flags |= ANALYSIS_CENTROIDS;

				break;
			    case 'e' :
				if (state->analysis_flags)
				    state->multiple_analyses = 1;

				state->analysis_flags |= ANALYSIS_EXP_AIR;
				break;
			    case 'g' :
				if (state->analysis_flags)
				    state->multiple_analyses = 1;

				state->analysis_flags |= ANALYSIS_GAPS;
				break;
			    case 'm' :
				if (state->analysis_flags)
				    state->multiple_analyses = 1;

				state->analysis_flags |= ANALYSIS_WEIGHTS;
				state->analysis_flags |= ANALYSIS_CENTROIDS;
				state->analysis_flags |= ANALYSIS_MOMENTS;

				break;
			    case 'o' :
				if (state->analysis_flags)
				    state->multiple_analyses = 1;

				state->analysis_flags |= ANALYSIS_OVERLAPS;
				break;
			    case 'p' :
				if (state->analysis_flags)
				    state->multiple_analyses = 1;

				state->analysis_flags |= ANALYSIS_OVERLAPS;
				state->analysis_flags |= ANALYSIS_PLOT_OVERLAPS;
				break;
			    case 'v' :
				if (state->analysis_flags)
				    state->multiple_analyses = 1;

				state->analysis_flags |= ANALYSIS_VOLUMES;
				break;
			    case 'w' :
				if (state->analysis_flags)
				    state->multiple_analyses = 1;

				state->analysis_flags |= ANALYSIS_WEIGHTS;
				break;
			    case 's' :
				if (state->analysis_flags)
				    state->multiple_analyses = 1;

				state->analysis_flags |= GQA_ANALYSIS_SURF_AREA;
				break;
			    default:
				bu_vls_printf(gedp->ged_result_str, "Unknown analysis type \"%c\" requested.\n", *p);
				return -1;
			}
		    }
		    break;
		}
	    case 'a':
		if (bn_decode_angle(&state->azimuth_deg, bu_optarg) == 0) {
		    bu_vls_printf(gedp->ged_result_str, "error parsing azimuth \"%s\"\n", bu_optarg);
		    return -1;
		}
		/* Switch to rotated-grid mode when az/el are specified */
		if (state->analysis_method == GQA_METHOD_LEGACY)
		    state->analysis_method = GQA_METHOD_ROTATED;
		break;
	    case 'e':
		if (bn_decode_angle(&state->elevation_deg, bu_optarg) == 0) {
		    bu_vls_printf(gedp->ged_result_str, "error parsing elevation \"%s\"\n", bu_optarg);
		    return -1;
		}
		/* Switch to rotated-grid mode when az/el are specified */
		if (state->analysis_method == GQA_METHOD_LEGACY)
		    state->analysis_method = GQA_METHOD_ROTATED;
		break;
	    case 'd': state->debug = 1; break;

	    case 'f': state->densityFileName = bu_optarg; break;

	    case 'g':
		{
		    double value1, value2;

		    /* find out if we have two or one args; user can
		     * separate them with , or - delimiter
		     */
		    p = strchr(bu_optarg, COMMA);
		    if (p)
			*p++ = '\0';
		    else {
			p = strchr(bu_optarg, '-');
			if (p)
			    *p++ = '\0';
		    }


		    if (_gqa_read_units_double(gedp, &value1, bu_optarg, units_tab[0])) {
			bu_vls_printf(gedp->ged_result_str, "error parsing grid spacing value \"%s\"\n", bu_optarg);
			return -1;
		    }

		    if (p) {
			/* we've got 2 values, they are upper limit
			 * and lower limit.
			 */
			if (_gqa_read_units_double(gedp, &value2, p, units_tab[0])) {
			    bu_vls_printf(gedp->ged_result_str, "error parsing grid spacing limit value \"%s\"\n", p);
			    return -1;
			}

			state->gridSpacing = value1;
			state->gridSpacingLimit = value2;
		    } else {
			state->gridSpacingLimit = value1;

			state->gridSpacing = 0.0; /* flag it */
		    }
		    break;
		}
	    case 'G':
		state->makeOverlapAssemblies = 1;
		bu_vls_printf(gedp->ged_result_str, "-G option unimplemented\n");
		return -1;
	    case 'm':
		/* method selection: legacy, rotated, crofton */
		if (BU_STR_EQUAL(bu_optarg, "legacy")) {
		    state->analysis_method = GQA_METHOD_LEGACY;
		} else if (BU_STR_EQUAL(bu_optarg, "rotated")) {
		    state->analysis_method = GQA_METHOD_ROTATED;
		} else if (BU_STR_EQUAL(bu_optarg, "crofton")) {
		    state->analysis_method = GQA_METHOD_CROFTON;
		} else {
		    bu_vls_printf(gedp->ged_result_str, "unknown method \"%s\"; valid: legacy, rotated, crofton\n", bu_optarg);
		    return -1;
		}
		break;
	    case 'n':
		if (sscanf(bu_optarg, "%d", &c) != 1 || c < 0) {
		    bu_vls_printf(gedp->ged_result_str, "num_hits must be integer value >= 0, not \"%s\"\n", bu_optarg);
		    return -1;
		}

		state->require_num_hits = (size_t)c;
		break;

	    case 'N':
		state->num_views = atoi(bu_optarg);
		break;
	    case 'p':
		state->plot_prefix = bu_optarg;
		break;
	    case 'P':
		/* cannot ask for more cpu's than the machine has */
		c = atoi(bu_optarg);
		if (c > 0 && c <= state->max_cpus)
		    state->ncpu = c;
		break;
	    case 'q':
		state->quiet_missed_report = 1;
		break;
	    case 'r':
		state->print_per_region_stats = 1;
		break;
	    case 'S':
		if (sscanf(bu_optarg, "%lg", &a) != 1 || a <= 1.0) {
		    bu_vls_printf(gedp->ged_result_str, "error in specifying minimum samples per model axis: \"%s\"\n", bu_optarg);
		    break;
		}
		state->Samples_per_model_axis = a + 1;
		break;
	    case 't':
		if (_gqa_read_units_double(gedp, &state->overlap_tolerance, bu_optarg, units_tab[0])) {
		    bu_vls_printf(gedp->ged_result_str, "error in overlap tolerance distance \"%s\"\n", bu_optarg);
		    return -1;
		}
		break;
	    case 'v':
		state->verbose = 1;
		break;
	    case 'V':
		if (_gqa_read_units_double(gedp, &state->volume_tolerance, bu_optarg, units_tab[1])) {
		    bu_vls_printf(gedp->ged_result_str, "error in volume tolerance \"%s\"\n", bu_optarg);
		    return -1;
		}
		break;
	    case 'W':
		if (_gqa_read_units_double(gedp, &state->weight_tolerance, bu_optarg, units_tab[2])) {
		    bu_vls_printf(gedp->ged_result_str, "error in weight tolerance \"%s\"\n", bu_optarg);
		    return -1;
		}
		break;

	    case 'U':
		errno = 0;
		state->use_air = strtol(bu_optarg, (char **)NULL, 10);
		if (errno == ERANGE || errno == EINVAL) {
		    bu_vls_printf(gedp->ged_result_str, "error in air argument %s\n", bu_optarg);
		    return -1;
		}
		break;
	    case 'u':
		{
		    char *ptr = bu_optarg;
		    const struct cvt_tab *cv;
		    static const char *dim[3] = {"length", "volume", "weight"};
		    char *units_name[3] = {NULL, NULL, NULL};
		    char **units_ap;

		    /* fill in units_name with the names we parse out */
		    units_ap = units_name;

		    /* acquire unit names */
		    for (i = 0; i < 3 && ptr; i++) {
			int found_unit;

			if (i == 0) {
			    *units_ap = strtok(ptr, CPP_XSTR(COMMA));
			} else {
			    *units_ap = strtok(NULL, CPP_XSTR(COMMA));
			}

			/* got something? */
			if (*units_ap == NULL)
			    break;

			/* got something valid? */
			found_unit = 0;
			for (cv = &units_tab[i][0]; cv->name[0] != '\0'; cv++) {
			    if (units_name[i] && BU_STR_EQUAL(cv->name, units_name[i])) {
				state->units[i] = cv;
				found_unit = 1;
				break;
			    }
			}

			if (!found_unit) {
			    bu_vls_printf(gedp->ged_result_str, "Units \"%s\" not found in conversion table\n", units_name[i]);
			    return -1;
			}

			++units_ap;
		    }

		    bu_vls_printf(gedp->ged_result_str, "Units: ");
		    for (i = 0; i < 3; i++) {
			bu_vls_printf(gedp->ged_result_str, " %s: %s", dim[i], state->units[i]->name);
		    }
		    bu_vls_printf(gedp->ged_result_str, "\n");
		}
		break;

	    default: /* '?' 'h' */
		return -1;
	}
    }

    return bu_optind;
}

/**
 * Write end points of partition to the standard output.  If this
 * routine return !0, this partition will be dropped from the boolean
 * evaluation.
 *
 * Returns:
 * 0 to eliminate partition with overlap entirely
 * 1 to retain partition in output list, claimed by reg1
 * 2 to retain partition in output list, claimed by reg2
 *
 * This routine must be prepared to run in parallel
 */
int
_gqa_overlap(struct application *ap,
	     struct partition *pp,
	     struct region *reg1,
	     struct region *reg2,
	     struct partition *hp)
{
    struct cstate *state = (struct cstate *)ap->A_STATE;
    struct ged *gedp = state->gedp;
    struct xray *rp = &ap->a_ray;
    struct hit *ihitp = pp->pt_inhit;
    struct hit *ohitp = pp->pt_outhit;
    point_t ihit;
    point_t ohit;
    double depth;

    if (!hp) /* unexpected */
	return 0;

    /* if one of the regions is air, let it loose */
    if (reg1->reg_aircode && ! reg2->reg_aircode)
	return 2;
    if (reg2->reg_aircode && ! reg1->reg_aircode)
	return 1;

    depth = ohitp->hit_dist - ihitp->hit_dist;

    if (depth < state->overlap_tolerance)
	/* too small to matter, pick one or none */
	return 1;

    VJOIN1(ihit, rp->r_pt, ihitp->hit_dist, rp->r_dir);
    VJOIN1(ohit, rp->r_pt, ohitp->hit_dist, rp->r_dir);

    if (state->plot_overlaps) {
	bu_semaphore_acquire(state->sem_plot);
	pl_color(state->plot_overlaps, V3ARGS(overlap_color));
	pdv_3line(state->plot_overlaps, ihit, ohit);
	bu_semaphore_release(state->sem_plot);
    }

    if (state->analysis_flags & ANALYSIS_PLOT_OVERLAPS) {
	bu_semaphore_acquire(state->sem_worker);
	BV_ADD_VLIST(state->plot_vbp->free_vlist_hd, state->plot_vhead, ihit, BV_VLIST_LINE_MOVE);
	BV_ADD_VLIST(state->plot_vbp->free_vlist_hd, state->plot_vhead, ohit, BV_VLIST_LINE_DRAW);
	bu_semaphore_release(state->sem_worker);
    }

    if (state->analysis_flags & ANALYSIS_OVERLAPS) {
	bu_semaphore_acquire(state->sem_lists);
	add_unique_pair(&state->overlapList, reg1, reg2, depth, ihit);
	bu_semaphore_release(state->sem_lists);

	if (state->plot_overlaps) {
	    bu_semaphore_acquire(state->sem_plot);
	    pl_color(state->plot_overlaps, V3ARGS(overlap_color));
	    pdv_3line(state->plot_overlaps, ihit, ohit);
	    bu_semaphore_release(state->sem_plot);
	}
    } else {
	bu_semaphore_acquire(state->sem_worker);
	bu_vls_printf(gedp->ged_result_str, "overlap %s %s\n", reg1->reg_name, reg2->reg_name);
	bu_semaphore_release(state->sem_worker);
    }

    /* XXX We should somehow flag the volume/weight calculations as invalid */

    /* since we have no basis to pick one over the other, just pick */
    return 1;	/* No further consideration to this partition */
}


/**
 * Does nothing.
 */
void
logoverlap(struct application *ap,
	   const struct partition *pp,
	   const struct bu_ptbl *regiontable,
	   const struct partition *InputHdp)
{
    RT_CK_AP(ap);
    RT_CK_PT(pp);
    BU_CK_PTBL(regiontable);
    if (!InputHdp)
	return;

    /* do nothing */

    return;
}


void _gqa_exposed_air(struct application *ap,
		      struct partition *pp,
		      point_t last_out_point,
		      point_t in_pt,
		      point_t out_pt)
{
    struct cstate *state = (struct cstate *)ap->A_STATE;

    /* this shouldn't be air */

    bu_semaphore_acquire(state->sem_lists);
    add_unique_pair(&state->exposedAirList,
		    pp->pt_regionp,
		    (struct region *)NULL,
		    DIST_PNT_PNT(in_pt, out_pt), /* thickness */
		    last_out_point); /* location */
    bu_semaphore_release(state->sem_lists);

    if (state->plot_expair) {
	bu_semaphore_acquire(state->sem_plot);
	pl_color(state->plot_expair, V3ARGS(expAir_color));
	pdv_3line(state->plot_expair, in_pt, out_pt);
	bu_semaphore_release(state->sem_plot);
    }
}


/**
 * rt_shootray() was told to call this on a hit.  It passes the
 * application structure which describes the state of the world (see
 * raytrace.h), and a circular linked list of partitions, each one
 * describing one in and out segment of one region.
 *
 * this routine must be prepared to run in parallel
 */
int
_gqa_hit(struct application *ap, struct partition *PartHeadp, struct seg *segs)
{
    /* see raytrace.h for all of these guys */
    struct partition *pp;
    point_t pt, opt, last_out_point;
    int last_air = 0;  /* what was the aircode of the last item */
    int air_first = 1; /* are we in an air before a solid */
    double dist;       /* the thickness of the partition */
    double last_out_dist = -1.0;
    double val;
    struct cstate *state = (struct cstate *)ap->A_STATE;
    struct ged *gedp = state->gedp;

    if (!segs) /* unexpected */
	return 0;

    if (PartHeadp->pt_forw == PartHeadp) return 1;


    /* examine each partition until we get back to the head */
    for (pp=PartHeadp->pt_forw; pp != PartHeadp; pp = pp->pt_forw) {

	long int material_id = pp->pt_regionp->reg_gmater;
	fastf_t grams_per_cu_mm = analyze_densities_density(state->densities, material_id);

	/* inhit info */
	dist = pp->pt_outhit->hit_dist - pp->pt_inhit->hit_dist;
	VJOIN1(pt, ap->a_ray.r_pt, pp->pt_inhit->hit_dist, ap->a_ray.r_dir);
	VJOIN1(opt, ap->a_ray.r_pt, pp->pt_outhit->hit_dist, ap->a_ray.r_dir);

	if (state->debug) {
	    bu_semaphore_acquire(state->sem_worker);
	    bu_vls_printf(gedp->ged_result_str, "%s %g->%g\n",
			  pp->pt_regionp->reg_name,
			  pp->pt_inhit->hit_dist,
			  pp->pt_outhit->hit_dist);
	    bu_semaphore_release(state->sem_worker);
	}

	/* checking for air sticking out of the model.  This is done
	 * here because there may be any number of air regions
	 * sticking out of the model along the ray.  This check only
	 * tests the front exposure; and a second test below checks
	 * the exit exposure.
	 */
	if (state->analysis_flags & ANALYSIS_EXP_AIR) {

	    /* FIXME: verify that the next partition is never
	     * overlapping numerically with the current partition.
	     * otherwise, we'll need to account for it. This state->debug
	     * statement should be removed after confirming.
	     * CSM@20220516
	     */
	    if (pp->pt_forw != PartHeadp) {
		double next_dist = pp->pt_forw->pt_inhit->hit_dist - pp->pt_inhit->hit_dist;
		if (next_dist < dist) {
		    bu_log("DEBUG: next partition's entry is prior to current partition's exit\n");
		    VJOIN1(opt, ap->a_ray.r_pt, pp->pt_forw->pt_inhit->hit_dist, ap->a_ray.r_dir);
		}
	    }

	    /* if air is first on the ray */
	    if (pp->pt_regionp->reg_aircode && air_first) {
		_gqa_exposed_air(ap, pp, last_out_point, pt, opt);
	    } else {
		air_first = 0;
	    }
	}

	/* looking for voids in the model */
	if (state->analysis_flags & ANALYSIS_GAPS) {
	    if (pp->pt_back != PartHeadp) {
		double gap_dist;

		/* if this entry point is further than the previous
		 * exit point then we have a void
		 */
		gap_dist = pp->pt_inhit->hit_dist - last_out_dist;

		if (gap_dist > state->overlap_tolerance) {

		    /* like overlaps, we only want to report unique pairs */
		    bu_semaphore_acquire(state->sem_lists);
		    add_unique_pair(&state->gapList,
				    pp->pt_regionp,
				    pp->pt_back->pt_regionp,
				    gap_dist,
				    pt);
		    bu_semaphore_release(state->sem_lists);

		    /* like overlaps, let's plot */
		    if (state->plot_gaps) {
			vect_t gapEnd;
			VJOIN1(gapEnd, pt, -gap_dist, ap->a_ray.r_dir);

			bu_semaphore_acquire(state->sem_plot);
			pl_color(state->plot_gaps, V3ARGS(gap_color));
			pdv_3line(state->plot_gaps, pt, gapEnd);
			bu_semaphore_release(state->sem_plot);
		    }
		}
	    }
	}

	/* computing the weight of the objects */
	if (state->analysis_flags & ANALYSIS_WEIGHTS) {
	    if (state->debug) {
		bu_semaphore_acquire(state->sem_worker);
		bu_vls_printf(gedp->ged_result_str, "Hit %s doing weight\n", pp->pt_regionp->reg_name);
		bu_semaphore_release(state->sem_worker);
	    }

	    /* make sure mater index is within range of densities */
	    if (pp->pt_regionp->reg_gmater < 0) {
		bu_semaphore_acquire(state->sem_worker);
		bu_vls_printf(gedp->ged_result_str, "density index %d on region %s is outside of range\nSet GIFTmater on region or add entry to density table\n",
			      pp->pt_regionp->reg_gmater,
			      pp->pt_regionp->reg_name);
		bu_semaphore_release(state->sem_worker);
		return BRLCAD_ERROR;
	    } else {

		struct gqa_per_region_data *prd;
		vect_t cmass;
		vect_t lenTorque;
		fastf_t Lx_sq;
		fastf_t Ly_sq;
		fastf_t Lz_sq;
		fastf_t cell_area = state->gridSpacing*state->gridSpacing;
		int los;

		switch (state->i_axis) {
		    case 0:
			Lx_sq = dist*pp->pt_regionp->reg_los*0.01;
			Lx_sq *= Lx_sq;
			Ly_sq = cell_area;
			Lz_sq = cell_area;
			break;
		    case 1:
			Lx_sq = cell_area;
			Ly_sq = dist*pp->pt_regionp->reg_los*0.01;
			Ly_sq *= Ly_sq;
			Lz_sq = cell_area;
			break;
		    case 2:
		    default:
			Lx_sq = cell_area;
			Ly_sq = cell_area;
			Lz_sq = dist*pp->pt_regionp->reg_los*0.01;
			Lz_sq *= Lz_sq;
			break;
		}

		/* factor in the density of this object weight
		 * computation, factoring in the LOS percentage
		 * material of the object
		 */
		los = pp->pt_regionp->reg_los;

		if (los < 1) {
		    const int MAX_PRINT = 10;
		    static int printed = 0;
		    static int warned = 0;
		    if (printed < MAX_PRINT) {
			bu_semaphore_acquire(state->sem_worker);
			bu_vls_printf(gedp->ged_result_str, "bad LOS (%d) on %s\n", los, pp->pt_regionp->reg_name);
			printed++;
			bu_semaphore_release(state->sem_worker);
		    } else if (!warned) {
			bu_vls_printf(gedp->ged_result_str, "Additional bad LOS warnings will be suppressed.\n");
			warned++;
		    }
		}

		/* accumulate the total weight values */
		val = grams_per_cu_mm * dist * (pp->pt_regionp->reg_los * 0.01);
		ap->A_LENDEN += val;

		prd = ((struct gqa_per_region_data *)pp->pt_regionp->reg_udata);

		// ensure we have an object and minimize reporting when we have errors
		if (prd->optr == NULL) {
		    static size_t reported = 0;
		    if (reported < 20) {
		    	bu_log("INTERNAL ERROR: %s does not have parent tracking\n", pp->pt_regionp->reg_name);
		    } else if (reported == 20) {
		        bu_log("INTERNAL ERROR: too many tracking errors, suppressing further reporting\n");
		    }
		    reported++;
		    continue;
		}

		/* accumulate the per-region per-view weight values */
		bu_semaphore_acquire(state->sem_stats);
		prd->r_lenDensity[state->i_axis] += val;

		/* accumulate the per-object per-view weight values */
		prd->optr->o_lenDensity[state->i_axis] += val;

		if (state->analysis_flags & ANALYSIS_CENTROIDS) {
		    /* calculate the center of mass for this partition */
		    VJOIN1(cmass, pt, dist*0.5, ap->a_ray.r_dir);

		    /* calculate the lenTorque for this partition (i.e. centerOfMass * lenDensity) */
		    VSCALE(lenTorque, cmass, val);

		    /* accumulate per-object per-view torque values */
		    VADD2(&prd->optr->o_lenTorque[state->i_axis*3], &prd->optr->o_lenTorque[state->i_axis*3], lenTorque);

		    /* accumulate the total lenTorque */
		    VADD2(&state->m_lenTorque[state->i_axis*3], &state->m_lenTorque[state->i_axis*3], lenTorque);

		    if (state->analysis_flags & ANALYSIS_MOMENTS) {
			vectp_t moi = NULL;
			vectp_t poi = NULL;
			fastf_t dx_sq = cmass[X]*cmass[X];
			fastf_t dy_sq = cmass[Y]*cmass[Y];
			fastf_t dz_sq = cmass[Z]*cmass[Z];
			fastf_t mass = val * cell_area;
			static const fastf_t ONE_TWELFTH = 1.0 / 12.0;

			/* Collect moments and products of inertia for the current object */
			moi = &prd->optr->o_moi[state->i_axis*3];
			moi[X] += ONE_TWELFTH*mass*(Ly_sq + Lz_sq) + mass*(dy_sq + dz_sq);
			moi[Y] += ONE_TWELFTH*mass*(Lx_sq + Lz_sq) + mass*(dx_sq + dz_sq);
			moi[Z] += ONE_TWELFTH*mass*(Lx_sq + Ly_sq) + mass*(dx_sq + dy_sq);
			poi = &prd->optr->o_poi[state->i_axis*3];
			poi[X] -= mass*cmass[X]*cmass[Y];
			poi[Y] -= mass*cmass[X]*cmass[Z];
			poi[Z] -= mass*cmass[Y]*cmass[Z];

			/* Collect moments and products of inertia for all objects */
			moi = &state->m_moi[state->i_axis*3];
			moi[X] += ONE_TWELFTH*mass*(Ly_sq + Lz_sq) + mass*(dy_sq + dz_sq);
			moi[Y] += ONE_TWELFTH*mass*(Lx_sq + Lz_sq) + mass*(dx_sq + dz_sq);
			moi[Z] += ONE_TWELFTH*mass*(Lx_sq + Ly_sq) + mass*(dx_sq + dy_sq);
			poi = &state->m_poi[state->i_axis*3];
			poi[X] -= mass*cmass[X]*cmass[Y];
			poi[Y] -= mass*cmass[X]*cmass[Z];
			poi[Z] -= mass*cmass[Y]*cmass[Z];
		    }
		}

		bu_semaphore_release(state->sem_stats);
	    }
	}

	/* compute the volume of the object */
	if (state->analysis_flags & ANALYSIS_VOLUMES) {
	    struct gqa_per_region_data *prd = ((struct gqa_per_region_data *)pp->pt_regionp->reg_udata);
	    ap->A_LEN += dist; /* add to total volume */
	    {
		// ensure we have an object and minimize reporting when we have errors
		if (prd->optr == NULL) {
		    static size_t reported = 0;
		    if (reported < 20) {
		    	bu_log("INTERNAL ERROR: %s does not have parent tracking\n", pp->pt_regionp->reg_name);
		    } else if (reported == 20) {
		        bu_log("INTERNAL ERROR: too many tracking errors, suppressing further reporting\n");
		    }
		    reported++;
		    continue;
		}

		bu_semaphore_acquire(state->sem_stats);

		/* add to region volume */
		prd->r_len[state->curr_view] += dist;

		/* add to object volume */
		prd->optr->o_len[state->curr_view] += dist;

		bu_semaphore_release(state->sem_stats);
	    }
	    if (state->debug) {
		bu_semaphore_acquire(state->sem_worker);
		bu_vls_printf(gedp->ged_result_str, "\t\tvol hit %s oDist:%g objVol:%g %s\n",
			      pp->pt_regionp->reg_name, dist, prd->optr->o_len[state->curr_view], prd->optr->o_name);
		bu_semaphore_release(state->sem_worker);
	    }

	    if (state->plot_volume) {
		VJOIN1(opt, ap->a_ray.r_pt, pp->pt_outhit->hit_dist, ap->a_ray.r_dir);

		bu_semaphore_acquire(state->sem_plot);
		if (ap->a_user & 1) {
		    pl_color(state->plot_volume, V3ARGS(gap_color));
		} else {
		    pl_color(state->plot_volume, V3ARGS(adjAir_color));
		}

		pdv_3line(state->plot_volume, pt, opt);
		bu_semaphore_release(state->sem_plot);
	    }
	}


	/* look for two adjacent air regions */
	if (state->analysis_flags & ANALYSIS_ADJ_AIR) {
	    if (last_air && pp->pt_regionp->reg_aircode &&
		pp->pt_regionp->reg_aircode != last_air) {

		double d = pp->pt_outhit->hit_dist - pp->pt_inhit->hit_dist;
		point_t aapt;

		bu_semaphore_acquire(state->sem_lists);
		add_unique_pair(&state->adjAirList, pp->pt_back->pt_regionp, pp->pt_regionp, 0.0, pt);
		bu_semaphore_release(state->sem_lists);

		d *= 0.25;
		VJOIN1(aapt, pt, d, ap->a_ray.r_dir);

		bu_semaphore_acquire(state->sem_plot);
		pl_color(state->plot_adjair, V3ARGS(adjAir_color));
		pdv_3line(state->plot_adjair, pt, aapt);
		bu_semaphore_release(state->sem_plot);
	    }
	}

	/* note that this region has been seen */
	((struct gqa_per_region_data *)pp->pt_regionp->reg_udata)->hits++;

	last_air = pp->pt_regionp->reg_aircode;
	last_out_dist = pp->pt_outhit->hit_dist;
	VJOIN1(last_out_point, ap->a_ray.r_pt, pp->pt_outhit->hit_dist, ap->a_ray.r_dir);
    }


    /* This checks the last partition was exposed air.  A check above
     * checks the partition entry for exposed air.
     */
    if (state->analysis_flags & ANALYSIS_EXP_AIR && last_air) {
	pp = PartHeadp->pt_back;
	_gqa_exposed_air(ap, pp, last_out_point, pt, opt);
    }


    /* This value is returned by rt_shootray a hit usually returns 1,
     * miss 0.
     */
    return 1;
}


/**
 * rt_shootray() was told to call this on a miss.
 *
 * This routine must be prepared to run in parallel
 */
int
_gqa_miss(struct application *ap)
{
    RT_CK_APPLICATION(ap);

    return 0;
}


/**
 * This routine must be prepared to run in parallel
 */
int
get_next_row(struct cstate *state)
{
    int v;
    /* look for more work */
    bu_semaphore_acquire(state->sem_worker);

    if (state->v < state->steps[state->v_axis])
	v = state->v++;	/* get a row to work on */
    else
	v = 0; /* signal end of work */

    bu_semaphore_release(state->sem_worker);

    return v;
}


/**
 * This routine must be prepared to run in parallel.
 *
 * Rotated-grid variant: each thread grabs the next ray from the rotated
 * grid using an atomic row counter, then shoots it.  The grid was
 * pre-computed by rotated_grid_setup() / rotated_grid_setup_ae() before
 * bu_parallel() was called.
 */
void
rotated_plane_worker(int cpu, void *ptr)
{
    struct application ap;
    struct cstate *state = (struct cstate *)ptr;
    unsigned long shot_cnt = 0;

    if (state->aborted)
	return;

    RT_APPLICATION_INIT(&ap);
    ap.a_rt_i     = (struct rt_i *)state->rtip;
    ap.a_hit      = _gqa_hit;
    ap.a_miss     = _gqa_miss;
    ap.a_logoverlap = logoverlap;
    ap.a_overlap  = _gqa_overlap;
    ap.a_resource = &state->resp[cpu];
    ap.A_LENDEN   = 0.0;
    ap.A_LEN      = 0.0;
    ap.A_STATE    = ptr;

    int view = state->curr_view;
    struct rotated_grid *rg = &state->rot_grid[view];

    while (1) {
	struct xray ray;

	/* atomically grab the next ray */
	bu_semaphore_acquire(state->sem_worker);
	int done = rotated_grid_generator(&ray, rg);
	bu_semaphore_release(state->sem_worker);

	if (done || state->aborted)
	    break;

	VMOVE(ap.a_ray.r_pt,  ray.r_pt);
	VMOVE(ap.a_ray.r_dir, ray.r_dir);

	ap.a_user = (int)rg->current; /* row-equivalent for plotting */
	(void)rt_shootray(&ap);
	shot_cnt++;
    }

    bu_semaphore_acquire(state->sem_stats);
    state->shots[state->curr_view] += shot_cnt;
    state->m_lenDensity[state->curr_view] += ap.A_LENDEN;
    state->m_len[state->curr_view]        += ap.A_LEN;
    bu_semaphore_release(state->sem_stats);
}


/**
 * This routine must be prepared to run in parallel
 */
void
plane_worker(int cpu, void *ptr)
{
    struct application ap;
    int u, v;
    double v_coord;
    struct cstate *state = (struct cstate *)ptr;
    unsigned long shot_cnt;
    struct ged *gedp = state->gedp;

    if (state->aborted)
	return;

    RT_APPLICATION_INIT(&ap);
    ap.a_rt_i = (struct rt_i *)state->rtip;	/* application uses this instance */
    ap.a_hit = _gqa_hit;    /* where to go on a hit */
    ap.a_miss = _gqa_miss;  /* where to go on a miss */
    ap.a_logoverlap = logoverlap;
    ap.a_overlap = _gqa_overlap;
    ap.a_resource = &state->resp[cpu];
    ap.A_LENDEN = 0.0; /* really the cumulative length*density for weight computation*/
    ap.A_LEN = 0.0;    /* really the cumulative length for volume computation */

    /* gross hack */
    ap.a_ray.r_dir[state->u_axis] = ap.a_ray.r_dir[state->v_axis] = 0.0;
    ap.a_ray.r_dir[state->i_axis] = 1.0;

    ap.A_STATE = ptr; /* really copying the state ptr to the a_uptr */

    u = -1;

    v = get_next_row(state);

    shot_cnt = 0;
    while (v) {

	v_coord = v * state->gridSpacing;
	if (state->debug) {
	    bu_semaphore_acquire(state->sem_worker);
	    bu_vls_printf(gedp->ged_result_str, "  v = %d v_coord=%g\n", v, v_coord);
	    bu_semaphore_release(state->sem_worker);
	}

	if ((v&1) || state->first) {
	    /* shoot all the rays in this row.  This is either the
	     * first time a view has been computed or it is an odd
	     * numbered row in a grid refinement
	     */
	    for (u=1; u < state->steps[state->u_axis]; u++) {
		ap.a_ray.r_pt[state->u_axis] = ap.a_rt_i->mdl_min[state->u_axis] + u*state->gridSpacing;
		ap.a_ray.r_pt[state->v_axis] = ap.a_rt_i->mdl_min[state->v_axis] + v_coord;
		ap.a_ray.r_pt[state->i_axis] = ap.a_rt_i->mdl_min[state->i_axis];

		if (state->debug) {
		    bu_semaphore_acquire(state->sem_worker);
		    bu_vls_printf(gedp->ged_result_str, "%5g %5g %5g -> %g %g %g\n", V3ARGS(ap.a_ray.r_pt),
				  V3ARGS(ap.a_ray.r_dir));
		    bu_semaphore_release(state->sem_worker);
		}
		ap.a_user = v;
		(void)rt_shootray(&ap);

		if (state->aborted)
		    return;

		shot_cnt++;
	    }
	} else {
	    /* shoot only the rays we need to on this row.  Some of
	     * them have been computed in a previous iteration.
	     */
	    for (u=1; u < state->steps[state->u_axis]; u+=2) {
		ap.a_ray.r_pt[state->u_axis] = ap.a_rt_i->mdl_min[state->u_axis] + u*state->gridSpacing;
		ap.a_ray.r_pt[state->v_axis] = ap.a_rt_i->mdl_min[state->v_axis] + v_coord;
		ap.a_ray.r_pt[state->i_axis] = ap.a_rt_i->mdl_min[state->i_axis];

		if (state->debug) {
		    bu_semaphore_acquire(state->sem_worker);
		    bu_vls_printf(gedp->ged_result_str, "%5g %5g %5g -> %g %g %g\n", V3ARGS(ap.a_ray.r_pt),
				  V3ARGS(ap.a_ray.r_dir));
		    bu_semaphore_release(state->sem_worker);
		}
		ap.a_user = v;
		(void)rt_shootray(&ap);

		if (state->aborted)
		    return;

		shot_cnt++;

		if (state->debug) {
		    if (u+1 < state->steps[state->u_axis]) {
			bu_semaphore_acquire(state->sem_worker);
			bu_vls_printf(gedp->ged_result_str, "  ---\n");
			bu_semaphore_release(state->sem_worker);
		    }
		}
	    }
	}

	/* iterate */
	v = get_next_row(state);
    }

    if (state->debug && (u == -1)) {
	bu_semaphore_acquire(state->sem_worker);
	bu_vls_printf(gedp->ged_result_str, "didn't shoot any rays\n");
	bu_semaphore_release(state->sem_worker);
    }

    /* There's nothing else left to work on in this view.  It's time
     * to add the values we have accumulated to the totals for the
     * view and return.  When all threads have been through here,
     * we'll have returned to serial computation.
     */
    bu_semaphore_acquire(state->sem_stats);
    state->shots[state->curr_view] += shot_cnt;
    state->m_lenDensity[state->curr_view] += ap.A_LENDEN; /* add our length*density value */
    state->m_len[state->curr_view] += ap.A_LEN; /* add our volume value */
    bu_semaphore_release(state->sem_stats);
}


struct gqa_per_obj_data*
find_cmd_line_obj(struct ged *gedp, int objc, struct gqa_per_obj_data *obj_rpt, const char *name)
{
    /* name is full region path ie /a/b/c
     * user specified either a or b or c or /a or /a/b or /a/b/c
     */
    int i;

    for (i = 0; i < objc; i++) {
    	const char* curr = name;

	do {
	    const char* oname = obj_rpt[i].o_name;
	    if (oname[0] != '/') {
		curr++;
	    }
	    int len = strlen(oname);
	    int comp = bu_strncmp(curr, oname, len);
	    if (comp == 0 && (curr[len] == '/' || curr[len] == '\0')) {
		return &obj_rpt[i];
	    }
	} while ((curr = strchr(curr+1, '/')));
    }

    bu_vls_printf(gedp->ged_result_str, "INTERNAL ERROR: Didn't find object named \"%s\" in %d command line entries\n", name, objc);

    return NULL;
}


/**
 * Allocate data structures for tracking statistics on a per-view
 * basis for each of the view, object and region levels.
 */
void
allocate_per_region_data(struct ged *gedp, struct cstate *state, int start, int ac, const char *av[])
{
    struct region *regp;
    struct rt_i *rtip = state->rtip;
    int i;
    int m;

    if (start > ac) {
	/* what? */
	bu_log("WARNING: Internal error (start:%d > ac:%d).\n", start, ac);
	return;
    }

    if (state->num_objects < 1) {
	/* what?? */
	bu_log("WARNING: No objects remaining.\n");
	return;
    }

    if (state->num_views == 0) {
	/* crap. */
	bu_log("WARNING: No views specified.\n");
	return;
    }

    if (rtip->stats.nregions == 0) {
	/* dammit! */
	bu_log("WARNING: No regions remaining.\n");
	return;
    }

    state->m_lenDensity = (double *)bu_calloc(state->num_views, sizeof(double), "densityLen");
    state->m_len = (double *)bu_calloc(state->num_views, sizeof(double), "volume");
    state->m_volume = (double *)bu_calloc(state->num_views, sizeof(double), "volume");
    state->m_weight = (double *)bu_calloc(state->num_views, sizeof(double), "volume");
    state->shots = (unsigned long *)bu_calloc(state->num_views, sizeof(unsigned long), "volume");
    state->m_lenTorque = (fastf_t *)bu_calloc(state->num_views, sizeof(vect_t), "lenTorque");
    state->m_moi = (fastf_t *)bu_calloc(state->num_views, sizeof(vect_t), "moments of inertia");
    state->m_poi = (fastf_t *)bu_calloc(state->num_views, sizeof(vect_t), "products of inertia");

    /* build data structures for the list of objects the user
     * specified on the command line
     */
    state->obj_tbl = (struct gqa_per_obj_data *)bu_calloc(state->num_objects, sizeof(struct gqa_per_obj_data), "report tables");
    for (i = 0; i < state->num_objects; i++) {
	state->obj_tbl[i].o_name = av[start+i];
	state->obj_tbl[i].o_len = (double *)bu_calloc(state->num_views, sizeof(double), "o_len");
	state->obj_tbl[i].o_lenDensity = (double *)bu_calloc(state->num_views, sizeof(double), "o_lenDensity");
	state->obj_tbl[i].o_volume = (double *)bu_calloc(state->num_views, sizeof(double), "o_volume");
	state->obj_tbl[i].o_weight = (double *)bu_calloc(state->num_views, sizeof(double), "o_weight");
	state->obj_tbl[i].o_lenTorque = (fastf_t *)bu_calloc(state->num_views, sizeof(vect_t), "lenTorque");
	state->obj_tbl[i].o_moi = (fastf_t *)bu_calloc(state->num_views, sizeof(vect_t), "moments of inertia");
	state->obj_tbl[i].o_poi = (fastf_t *)bu_calloc(state->num_views, sizeof(vect_t), "products of inertia");
    }

    /* build objects for each region */
    state->reg_tbl = (struct gqa_per_region_data *)bu_calloc(rtip->stats.nregions, sizeof(struct gqa_per_region_data), "per_region_data");


    for (i = 0, BU_LIST_FOR (regp, region, &(rtip->HeadRegion)), i++) {
	regp->reg_udata = &state->reg_tbl[i];

	state->reg_tbl[i].r_lenDensity = (double *)bu_calloc(state->num_views, sizeof(double), "r_lenDensity");
	state->reg_tbl[i].r_len = (double *)bu_calloc(state->num_views, sizeof(double), "r_len");
	state->reg_tbl[i].r_volume = (double *)bu_calloc(state->num_views, sizeof(double), "len");
	state->reg_tbl[i].r_weight = (double *)bu_calloc(state->num_views, sizeof(double), "len");

	m = (int)strlen(regp->reg_name);
	if (m > state->max_region_name_len) state->max_region_name_len = m;
	state->reg_tbl[i].optr = find_cmd_line_obj(gedp, state->num_objects, state->obj_tbl, regp->reg_name);
    }
}


/**
 * list_report
 */
void
list_report(struct ged *gedp, struct cstate *state, struct region_pair *list)
{
    struct region_pair *rp;

    if (BU_LIST_IS_EMPTY(&list->l)) {
	bu_vls_printf(gedp->ged_result_str, "No %s\n", (char *)list->r.name);

	return;
    }

    bu_vls_printf(gedp->ged_result_str, "list %s:\n", (char *)list->r.name);

    for (BU_LIST_FOR (rp, region_pair, &(list->l))) {
	if (rp->r2) {
	    bu_vls_printf(gedp->ged_result_str, "%s %s count:%lu dist:%g%s @ (%g %g %g)\n",
			  rp->r.r1->reg_name, rp->r2->reg_name, rp->count,
			  rp->max_dist / state->units[LINE]->val, state->units[LINE]->name, V3ARGS(rp->coord));
	} else {
	    bu_vls_printf(gedp->ged_result_str, "%s count:%lu dist:%g%s @ (%g %g %g)\n",
			  rp->r.r1->reg_name, rp->count,
			  rp->max_dist / state->units[LINE]->val, state->units[LINE]->name, V3ARGS(rp->coord));
	}
    }
}


/**
 * Do some computations prior to raytracing based upon options the
 * user has specified
 *
 * Returns:
 * 0 continue, ready to go
 * !0 error encountered, terminate processing
 */
int
options_prep(struct ged *gedp, struct cstate *state, struct rt_i *UNUSED(rtip), vect_t span)
{
    double newGridSpacing = state->gridSpacing;
    int axis;

    /* figure out where the density values are coming from and get
     * them.
     */
    if (state->analysis_flags & ANALYSIS_WEIGHTS) {
	if (state->densityFileName) {
	    DLOG(gedp->ged_result_str, "density from file\n");
	    if (_ged_read_densities(&state->densities, &state->densities_source, gedp, state->densityFileName, 0) != BRLCAD_OK) {
		return BRLCAD_ERROR;
	    }
	} else {
	    DLOG(gedp->ged_result_str, "density from db\n");
	    if (_ged_read_densities(&state->densities, &state->densities_source, gedp, NULL, 0) != BRLCAD_OK) {
		return BRLCAD_ERROR;
	    }
	}
	// iterate through the db and find all materials
	{
	    struct directory *dp;
	    FOR_ALL_DIRECTORY_START(dp, gedp->dbip)
		struct rt_db_internal intern;
		struct rt_material_internal *material_ip;
		if (rt_db_get_internal(&intern, dp, gedp->dbip, NULL) >= 0) {
		    if (intern.idb_minor_type == DB5_MINORTYPE_BRLCAD_MATERIAL) {
			// if the material has an id and density, add it to the density table
			material_ip = (struct rt_material_internal *)intern.idb_ptr;

			const char *id_string = bu_avs_get(&material_ip->physicalProperties, "id");
			if (id_string == NULL) {
			    continue;
			}
			int id = strtol(id_string, NULL, 10);

			const char *density_string = bu_avs_get(&material_ip->physicalProperties, "density");
			if (density_string == NULL) {
			    continue;
			}
			double density_double = strtod(density_string, NULL);
			/* since BRL-CAD does computation in mm, but the table is in
			 * grams / (cm^3) we convert the table on input
			 */
			density_double = density_double / 1000.0;

			char *name = bu_vls_strdup(&material_ip->name);
			struct bu_vls result_str = BU_VLS_INIT_ZERO;
			if (analyze_densities_set(state->densities, id, density_double, name, &result_str) < 0) {
			    bu_vls_printf(&result_str, "Error inserting density %d,%g,%s\n", id, density_double, name);
			}
			bu_vls_free(&result_str);
		    }
		}
	    FOR_ALL_DIRECTORY_END;
	}
    }
    /* refine the grid spacing if the user has set a lower bound on
     * the number of rays per model axis
     */
    for (axis=0; axis < 3; axis++) {
	if (span[axis] < newGridSpacing*state->Samples_per_model_axis) {
	    /* along this axis, the state->gridSpacing is larger than the
	     * model span.  We need to refine.
	     */
	    newGridSpacing = span[axis] / state->Samples_per_model_axis;
	}
    }

    if (!ZERO(newGridSpacing - state->gridSpacing)) {
	bu_log("Initial grid spacing %g %s does not allow %g samples per axis.\n",
	       state->gridSpacing / state->units[LINE]->val, state->units[LINE]->name, state->Samples_per_model_axis - 1);

	bu_log("Adjusted initial grid spacing to %g %s to get %g samples per model axis.\n",
	       newGridSpacing / state->units[LINE]->val, state->units[LINE]->name, state->Samples_per_model_axis);

	state->gridSpacing = newGridSpacing;
    }

    /* if the vol/weight tolerances are not set, pick something */
    if (state->analysis_flags & ANALYSIS_VOLUMES) {
	if (state->volume_tolerance < 0.0) {
	    /* using 1/1000th the volume as a default tolerance, no particular reason */
	    state->volume_tolerance = span[X] * span[Y] * span[Z] * 0.001;
	    bu_log("Using estimated volume tolerance %g %s\n", state->volume_tolerance / state->units[VOL]->val, state->units[VOL]->name);
	} else
	    bu_log("Using volume tolerance %g %s\n", state->volume_tolerance / state->units[VOL]->val, state->units[VOL]->name);
	if (state->plot_prefix) {
	    struct bu_vls vp = BU_VLS_INIT_ZERO;
	    bu_vls_printf(&vp, "%svolume.plot3", state->plot_prefix);
	    bu_log("Plotting volumes to %s\n", bu_vls_cstr(&vp));
	    state->plot_volume = fopen(bu_vls_cstr(&vp), "wb");
	    if (state->plot_volume == (FILE *)NULL) {
		bu_vls_printf(gedp->ged_result_str, "cannot open plot file %s\n", bu_vls_cstr(&vp));
		/* not a critical failure */
	    }
	    bu_vls_free(&vp);
	}
    }
    if (state->analysis_flags & ANALYSIS_WEIGHTS) {
	if (state->weight_tolerance < 0.0) {
	    double max_den = 0.0;
	    long int curr_id = -1;
	    while ((curr_id = analyze_densities_next(state->densities, curr_id)) != -1) {
		if (analyze_densities_density(state->densities, curr_id) > max_den)
		    max_den = analyze_densities_density(state->densities, curr_id);
	    }
	    state->weight_tolerance = span[X] * span[Y] * span[Z] * 0.1 * max_den;
	    bu_vls_printf(gedp->ged_result_str, "setting weight tolerance to %g %s\n",
			  state->weight_tolerance / state->units[WGT]->val,
			  state->units[WGT]->name);
	} else {
	    bu_vls_printf(gedp->ged_result_str, "weight tolerance   %g\n", state->weight_tolerance);
	}
    }
    if (state->analysis_flags & ANALYSIS_GAPS) {
	if (state->plot_prefix) {
	    struct bu_vls vp = BU_VLS_INIT_ZERO;
	    bu_vls_printf(&vp, "%sgaps.plot3", state->plot_prefix);
	    bu_log("Plotting gaps to %s\n", bu_vls_cstr(&vp));
	    state->plot_gaps = fopen(bu_vls_cstr(&vp), "wb");
	    if (state->plot_gaps == (FILE *)NULL) {
		bu_vls_printf(gedp->ged_result_str, "cannot open plot file %s\n", bu_vls_cstr(&vp));
		/* not a critical failure */
	    }
	    bu_vls_free(&vp);
	}
    }
    if (state->analysis_flags & ANALYSIS_OVERLAPS) {
	if (!ZERO(state->overlap_tolerance))
	    bu_vls_printf(gedp->ged_result_str, "overlap tolerance to %g\n", state->overlap_tolerance);
	if (state->plot_prefix) {
	    struct bu_vls vp = BU_VLS_INIT_ZERO;
	    bu_vls_printf(&vp, "%soverlaps.plot3", state->plot_prefix);
	    bu_log("Plotting overlaps to %s\n", bu_vls_cstr(&vp));
	    state->plot_overlaps = fopen(bu_vls_cstr(&vp), "wb");
	    if (state->plot_overlaps == (FILE *)NULL) {
		bu_vls_printf(gedp->ged_result_str, "cannot open plot file %s\n", bu_vls_cstr(&vp));
		/* not a critical failure */
	    }
	    bu_vls_free(&vp);
	}
    }

    if (state->print_per_region_stats)
	if ((state->analysis_flags & (ANALYSIS_VOLUMES|ANALYSIS_WEIGHTS)) == 0)
	    bu_vls_printf(gedp->ged_result_str, "Note: -r option ignored: neither volume or weight options requested\n");

    if (state->analysis_flags & ANALYSIS_ADJ_AIR)
	if (state->plot_prefix) {
	    struct bu_vls vp = BU_VLS_INIT_ZERO;
	    bu_vls_printf(&vp, "%sadj_air.plot3", state->plot_prefix);
	    bu_log("Plotting adjacent air to %s\n", bu_vls_cstr(&vp));
	    state->plot_adjair = fopen(bu_vls_cstr(&vp), "wb");
	    if (state->plot_adjair == (FILE *)NULL) {
		bu_vls_printf(gedp->ged_result_str, "cannot open plot file %s\n", bu_vls_cstr(&vp));
		/* not a critical failure */
	    }
	    bu_vls_free(&vp);
	}

    if (state->analysis_flags & ANALYSIS_EXP_AIR)
	if (state->plot_prefix) {
	    struct bu_vls vp = BU_VLS_INIT_ZERO;
	    bu_vls_printf(&vp, "%sexp_air.plot3", state->plot_prefix);
	    bu_log("Plotting exposed air to %s\n", bu_vls_cstr(&vp));
	    state->plot_expair = fopen(bu_vls_cstr(&vp), "wb");
	    if (state->plot_expair == (FILE *)NULL) {
		bu_vls_printf(gedp->ged_result_str, "cannot open plot file %s\n", bu_vls_cstr(&vp));
		/* not a critical failure */
	    }
	    bu_vls_free(&vp);
	}


    if ((state->analysis_flags & (ANALYSIS_ADJ_AIR|ANALYSIS_EXP_AIR)) && ! state->use_air) {
	bu_vls_printf(gedp->ged_result_str, "Error:  Air regions discarded but air analysis requested!\nSet state->use_air non-zero or eliminate air analysis\n");
	return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}


int
densities_prep(struct ged *gedp, struct cstate *state, struct rt_i *rtip)
{
    analyze_densities_create(&state->densities);
    int found_densities = 0;

    /* figure out where the density values are coming from and get
     * them.
     */
    if (state->analysis_flags & ANALYSIS_WEIGHTS) {
	if (state->densityFileName) {
	    DLOG(gedp->ged_result_str, "density from file\n");
	    if (_ged_read_densities(&state->densities, &state->densities_source, gedp, state->densityFileName, 0) == BRLCAD_OK) {
		found_densities = 1;
	    }
	} else {
	    DLOG(gedp->ged_result_str, "density from db\n");
	    if (_ged_read_densities(&state->densities, &state->densities_source, gedp, NULL, 0) == BRLCAD_OK) {
		found_densities = 1;
	    }
	}

	// iterate through the db and find all materials
	int next_available_id = MAX_MATERIAL_ID - 1;
	{
	    struct directory *dp;
	    FOR_ALL_DIRECTORY_START(dp, rtip->rti_dbip)
		struct rt_db_internal intern;
		struct rt_material_internal *material_ip;
		if (dp->d_major_type == DB5_MAJORTYPE_BRLCAD) {
		    if (rt_db_get_internal(&intern, dp, rtip->rti_dbip, NULL) >= 0) {
			if (intern.idb_minor_type == DB5_MINORTYPE_BRLCAD_MATERIAL) {
			    // if the material has a density, add it to the density table
			    material_ip = (struct rt_material_internal *) intern.idb_ptr;

			    const char *density_string = bu_avs_get(&material_ip->physicalProperties, "density");
			    if (density_string == NULL) {
				continue;
			    }

			    double density_double = strtod(density_string, NULL);
			    /* since BRL-CAD does computation in mm, but the table is in
			     * grams / (cm^3) we convert the table on input
			     */
			    density_double = density_double / 1000.0;
			    found_densities = 1;

			    const char *id_string = bu_avs_get(&material_ip->physicalProperties, "id");
			    int id;
			    if (id_string == NULL) {
				// assign id for materials without ids in the density table
				// start from the max material id and work backwards
				id = next_available_id;
				next_available_id--;
			    } else {
				id = strtol(id_string, NULL, 10);
			    }

			    char *density_table_name = bu_vls_strdup(&material_ip->name);
			    if (analyze_densities_set(state->densities, id, density_double, density_table_name, gedp->ged_result_str) < 0) {
				bu_vls_printf(gedp->ged_result_str, "Error inserting density %d,%g,%s\n", id, density_double, density_table_name);
				analyze_densities_clear(state->densities);
				return BRLCAD_ERROR;
			    }
			}
		    }
		}
	    FOR_ALL_DIRECTORY_END;
	}

	if (!found_densities) {
	    bu_vls_printf(gedp->ged_result_str, "Could not find any density information.\n");
	    analyze_densities_clear(state->densities);
	    return BRLCAD_ERROR;
	}

	// look for objects with material_name set and set the material_id
	// analyze_densities_get
	{
	    struct directory *dp;
	    FOR_ALL_DIRECTORY_START(dp, rtip->rti_dbip)
		if (dp->d_major_type == DB5_MAJORTYPE_BRLCAD) {
		    struct bu_attribute_value_set avs = BU_AVS_INIT_ZERO;

		    if (db5_get_attributes(rtip->rti_dbip, &avs, dp) == 0) {
			const char *material_name = bu_avs_get(&avs, "material_name");

			if (material_name != NULL && !BU_STR_EQUAL(material_name, "(null)") && !BU_STR_EQUAL(material_name, "del")) {
			    struct directory *material_dp = db_lookup(rtip->rti_dbip, material_name, LOOKUP_QUIET);

			    if (material_dp != NULL) {
				struct rt_db_internal material_intern;
				struct rt_material_internal *material_ip;
				if (rt_db_get_internal(&material_intern, material_dp, rtip->rti_dbip, NULL) >= 0) {
				    if (material_intern.idb_minor_type == DB5_MINORTYPE_BRLCAD_MATERIAL) {
					// the material_ip->name field is the name in the density table
					// not just the material_name (they could be different)
					material_ip = (struct rt_material_internal *) material_intern.idb_ptr;
					char *density_table_name = bu_vls_strdup(&material_ip->name);
					long int wids[1];

					// get the id from the density table
					analyze_densities_id((long int *)wids, 1, state->densities, density_table_name);

					// update the region->reg_mater field for the given region
					struct region *regp = REGION_NULL;
					for (BU_LIST_FOR(regp, region, &(rtip->HeadRegion))) {
					    RT_CK_REGION(regp);

					    // by default the regp->reg_name holds the path to the region
					    // we just want the name so we remove the path before the name
					    const char *reg_name = strrchr(regp->reg_name, '/') + 1;

					    // if its the region we're looking for, set the reg_mater field
					    if (BU_STR_EQUAL(reg_name, dp->d_namep)) {
						regp->reg_gmater = wids[0];
					    }
					}
				    }
				}
			    } else {
				bu_vls_printf(gedp->ged_result_str, "WARNING: material_name %s is not in the database\n", material_name);
			    }
			}
		    } else {
			bu_vls_printf(gedp->ged_result_str, "Error: failed to load attributes for %s\n", dp->d_namep);
			analyze_densities_clear(state->densities);
			return BRLCAD_ERROR;
		    }
		}
	    FOR_ALL_DIRECTORY_END;
	}
    }

    return BRLCAD_OK;
}


void
view_reports(struct ged *gedp, struct cstate *state)
{
    if (state->analysis_flags & ANALYSIS_VOLUMES) {
	int obj;
	int view;

	/* for each object, compute the volume for all views */
	for (obj = 0; obj < state->num_objects; obj++) {
	    double val;
	    /* compute volume of object for given view */
	    view = state->curr_view;

	    /* compute the per-view volume of this object */

	    if (state->shots[view] > 0) {
		val = state->obj_tbl[obj].o_volume[view] =
		state->obj_tbl[obj].o_len[view] * (state->area[view] / state->shots[view]);

		if (state->verbose)
		    bu_vls_printf(gedp->ged_result_str, "\t%s volume %g %s\n",
				  state->obj_tbl[obj].o_name,
				  val / state->units[VOL]->val,
				  state->units[VOL]->name);
	    }
	}
    }
    if (state->analysis_flags & ANALYSIS_WEIGHTS) {
	int obj;
	int view = state->curr_view;

	for (obj = 0; obj < state->num_objects; obj++) {
	    double grams_per_cu_mm = state->obj_tbl[obj].o_lenDensity[view] *
	    (state->area[view] / state->shots[view]);


	    if (state->verbose)
		bu_vls_printf(gedp->ged_result_str, "\t%s %g %s\n",
			      state->obj_tbl[obj].o_name,
			      grams_per_cu_mm / state->units[WGT]->val,
			      state->units[WGT]->name);
	}
    }
}


/**
 * These checks are unique because they must both be completed.  Early
 * termination before they are done is not an option.  The results
 * computed here are used later.
 *
 * Returns:
 * 0 terminate
 * 1 continue processing
 */
static int
weight_volume_terminate(struct ged *gedp, struct cstate *state)
{
    /* Both weight and volume computations rely on this routine to
     * compute values that are printed in summaries.  Hence, both
     * checks must always be done before this routine exits.  So we
     * store the status (can we terminate processing?) in this
     * variable and act on it once both volume and weight computations
     * are done.
     */
    int can_terminate = 1;

    double low, hi, val, delta;

    if (state->analysis_flags & ANALYSIS_WEIGHTS) {
	/* for each object, compute the weight for all views */
	int obj;

	for (obj = 0; obj < state->num_objects; obj++) {
	    int view;
	    double tmp;

	    if (state->verbose)
		bu_vls_printf(gedp->ged_result_str, "object %d\n", obj);

	    /* compute weight of object for given view */
	    low = INFINITY;
	    hi = -INFINITY;
	    tmp = 0.0;
	    for (view = 0; view < state->num_views; view++) {
		val = state->obj_tbl[obj].o_weight[view] =
		state->obj_tbl[obj].o_lenDensity[view] * (state->area[view] / state->shots[view]);
		V_MIN(low, val);
		V_MAX(hi, val);
		tmp += val;
	    }
	    delta = hi - low;

	    if (state->verbose)
		bu_vls_printf(gedp->ged_result_str,
			      "\t%s running avg weight %g %s hi=(%g) low=(%g)\n",
			      state->obj_tbl[obj].o_name,
			      (tmp / state->num_views) / state->units[WGT]->val,
			      state->units[WGT]->name,
			      hi / state->units[WGT]->val,
			      low / state->units[WGT]->val);

	    if (delta > state->weight_tolerance) {
		/* this object differs too much in each view, so we
		 * need to refine the grid. signal that we cannot
		 * terminate.
		 */
		can_terminate = 0;
		if (state->verbose)
		    bu_vls_printf(gedp->ged_result_str, "\t%s differs too much in weight per view.\n",
				  state->obj_tbl[obj].o_name);
	    }
	}
	if (can_terminate) {
	    if (state->verbose)
		bu_vls_printf(gedp->ged_result_str, "all objects within tolerance on weight calculation\n");
	}
    }

    if (state->analysis_flags & ANALYSIS_VOLUMES) {
	/* find the range of values for object volumes */
	int obj;

	/* for each object, compute the volume for all views */
	for (obj = 0; obj < state->num_objects; obj++) {
	    int view;
	    double tmp;

	    /* compute volume of object for given view */
	    low = INFINITY;
	    hi = -INFINITY;
	    tmp = 0.0;
	    for (view = 0; view < state->num_views; view++) {
		val = state->obj_tbl[obj].o_volume[view] =
		state->obj_tbl[obj].o_len[view] * (state->area[view] / state->shots[view]);
		V_MIN(low, val);
		V_MAX(hi, val);
		tmp += val;
	    }
	    delta = hi - low;

	    if (state->verbose)
		bu_vls_printf(gedp->ged_result_str,
			      "\t%s running avg volume %g %s hi=(%g) low=(%g)\n",
			      state->obj_tbl[obj].o_name,
			      (tmp / state->num_views) / state->units[VOL]->val, state->units[VOL]->name,
			      hi / state->units[VOL]->val,
			      low / state->units[VOL]->val);

	    if (delta > state->volume_tolerance) {
		/* this object differs too much in each view, so we
		 * need to refine the grid.
		 */
		can_terminate = 0;
		if (state->verbose)
		    bu_vls_printf(gedp->ged_result_str, "\tvolume tol not met on %s.  Refine grid\n",
				  state->obj_tbl[obj].o_name);
		break;
	    }
	}
    }

    if (can_terminate) {
	return 0; /* signal we don't want to go onward */
    }
    return 1;
}


/**
 * Check to see if we are done processing due to some user specified
 * limit being achieved.
 *
 * Returns:
 * 0 Terminate
 * 1 Continue processing
 */
int
terminate_check(struct ged *gedp, struct cstate *state)
{
    int wv_status;
    int view;
    int obj;

    DLOG(gedp->ged_result_str, "terminate_check\n");
    RT_CK_RTI(state->rtip);

    if (state->plot_overlaps) fflush(state->plot_overlaps);
    if (state->plot_weight) fflush(state->plot_weight);
    if (state->plot_volume) fflush(state->plot_volume);
    if (state->plot_adjair) fflush(state->plot_adjair);
    if (state->plot_gaps) fflush(state->plot_gaps);
    if (state->plot_expair) fflush(state->plot_expair);

    /* this computation is done first, because there are side effects
     * that must be obtained whether we terminate or not
     */
    wv_status = weight_volume_terminate(gedp, state);


    /* if we've reached the grid limit, we're done, no matter what */
    if (state->gridSpacing < state->gridSpacingLimit) {
	bu_vls_printf(gedp->ged_result_str, "NOTE: Stopped, grid spacing refined to %g (below lower limit %g).\n",
		      state->gridSpacing, state->gridSpacingLimit);
	return 0;
    }

    /* If no gqa-native analyses are requested (e.g., only GQA_ANALYSIS_SURF_AREA
     * which runs its own separate pass), terminate after this first pass. */
    if (!(state->analysis_flags & GQA_NATIVE_FLAGS))
	return 0;

    /* if we are doing one of the "Error" checking operations:
     * Overlap, gap, adj_air, exp_air, then we ALWAYS go to the grid
     * spacing limit and we ALWAYS terminate on first error/list-entry
     */
    if ((state->analysis_flags & ANALYSIS_OVERLAPS)) {
	if (BU_LIST_NON_EMPTY(&state->overlapList.l)) {
	    /* since we've found an overlap, we can quit */
	    return 0;
	} else {
	    bu_vls_printf(gedp->ged_result_str, "overlaps list at %gmm is empty\n", state->gridSpacing / GRIDSPACING_STEP);
	}
    }
    if ((state->analysis_flags & ANALYSIS_GAPS)) {
	if (BU_LIST_NON_EMPTY(&state->gapList.l)) {
	    /* since we've found a gap, we can quit */
	    return 0;
	}
    }
    if ((state->analysis_flags & ANALYSIS_ADJ_AIR)) {
	if (BU_LIST_NON_EMPTY(&state->adjAirList.l)) {
	    /* since we've found adjacent air, we can quit */
	    return 0;
	}
    }
    if ((state->analysis_flags & ANALYSIS_EXP_AIR)) {
	if (BU_LIST_NON_EMPTY(&state->exposedAirList.l)) {
	    /* since we've found exposed air, we can quit */
	    return 0;
	}
    }


    if (state->analysis_flags & (ANALYSIS_WEIGHTS|ANALYSIS_VOLUMES)) {
	/* volume/weight checks only get to terminate processing if
	 * there are no "error" check computations being done
	 */
	if (state->analysis_flags & (ANALYSIS_GAPS|ANALYSIS_ADJ_AIR|ANALYSIS_OVERLAPS|ANALYSIS_EXP_AIR)) {
	    if (state->verbose)
		bu_vls_printf(gedp->ged_result_str, "Volume/Weight tolerance met.  Cannot terminate calculation due to error computations\n");
	} else {
	    struct region *regp;
	    int all_hit = 1;
	    size_t hits;

	    if (state->require_num_hits > 0) {
		/* check to make sure every region was hit at least once */
		for (BU_LIST_FOR (regp, region, &(state->rtip->HeadRegion))) {
		    RT_CK_REGION(regp);

		    hits = (size_t)((struct gqa_per_region_data *)regp->reg_udata)->hits;
		    if (hits < state->require_num_hits) {
			all_hit = 0;
			if (state->verbose) {
			    if (hits == 0 && !state->quiet_missed_report) {
				bu_vls_printf(gedp->ged_result_str, "%s was not hit\n", regp->reg_name);
			    } else if (hits) {
				bu_vls_printf(gedp->ged_result_str, "%s hit only %zu times (< %zu)\n",
					      regp->reg_name, hits, state->require_num_hits);
			    }
			}
		    }
		}

		if (all_hit && wv_status == 0) {
		    if (state->verbose)
			bu_vls_printf(gedp->ged_result_str, "%s: Volume/Weight tolerance met. Terminate\n", CPP_FILELINE);
		    return 0; /* terminate */
		}
	    } else {
		if (wv_status == 0) {
		    if (state->verbose)
			bu_vls_printf(gedp->ged_result_str, "%s: Volume/Weight tolerance met. Terminate\n", CPP_FILELINE);
		    return 0; /* terminate */
		}
	    }
	}
    }

    for (view=0; view < state->num_views; view++) {
	for (obj = 0; obj < state->num_objects; obj++) {
	    VSCALE(&state->obj_tbl[obj].o_moi[view*3], &state->obj_tbl[obj].o_moi[view*3], 0.25);
	    VSCALE(&state->obj_tbl[obj].o_poi[view*3], &state->obj_tbl[obj].o_poi[view*3], 0.25);
	}

	VSCALE(&state->m_moi[view*3], &state->m_moi[view*3], 0.25);
	VSCALE(&state->m_poi[view*3], &state->m_poi[view*3], 0.25);
    }

    return 1;
}


/**
 * summary_reports
 */
void
summary_reports(struct ged *gedp, struct cstate *state)
{
    int view;
    int obj;
    double avg_mass;
    struct region *regp;

    if (state->multiple_analyses)
	bu_vls_printf(gedp->ged_result_str, "Summaries (%gmm grid spacing):\n", state->gridSpacing / GRIDSPACING_STEP);
    else
	bu_vls_printf(gedp->ged_result_str, "Summary (%gmm grid spacing):\n", state->gridSpacing / GRIDSPACING_STEP);

    if (state->analysis_flags & ANALYSIS_WEIGHTS) {
	bu_vls_printf(gedp->ged_result_str, "Weight:\n");
	for (obj = 0; obj < state->num_objects; obj++) {
	    avg_mass = 0.0;

	    for (view=0; view < state->num_views; view++) {
		/* computed in terminate_check() */
		avg_mass += state->obj_tbl[obj].o_weight[view];
	    }
	    avg_mass /= state->num_views;
	    bu_vls_printf(gedp->ged_result_str, "\t%*s %g %s\n", -state->max_region_name_len, state->obj_tbl[obj].o_name,
			  avg_mass / state->units[WGT]->val, state->units[WGT]->name);

	    if (state->analysis_flags & ANALYSIS_CENTROIDS &&
		!ZERO(avg_mass)) {
		vect_t centroid = VINIT_ZERO;
		fastf_t Dx_sq, Dy_sq, Dz_sq;
		fastf_t inv_total_mass = 1.0/avg_mass;

		for (view=0; view < state->num_views; view++) {
		    vect_t torque;
		    fastf_t cell_area = state->area[view] / state->shots[view];

		    VSCALE(torque, &state->obj_tbl[obj].o_lenTorque[view*3], cell_area);
		    VADD2(centroid, centroid, torque);
		}

		VSCALE(centroid, centroid, 1.0/(fastf_t)state->num_views);
		VSCALE(centroid, centroid, inv_total_mass);
		bu_vls_printf(gedp->ged_result_str,
			      "\t\tcentroid: (%g %g %g) mm\n", V3ARGS(centroid));

		/* Do the final calculations for the moments of
		 * inertia for the current object.
		 */
		if (state->analysis_flags & ANALYSIS_MOMENTS) {
		    struct bu_vls title = BU_VLS_INIT_ZERO;
		    mat_t tmat; /* total mat */

		    MAT_ZERO(tmat);
		    for (view=0; view < state->num_views; view++) {
			vectp_t moi = &state->obj_tbl[obj].o_moi[view*3];
			vectp_t poi = &state->obj_tbl[obj].o_poi[view*3];

			tmat[MSX] += moi[X];
			tmat[MSY] += moi[Y];
			tmat[MSZ] += moi[Z];
			tmat[1] += poi[X];
			tmat[2] += poi[Y];
			tmat[6] += poi[Z];
		    }

		    tmat[MSX] /= (fastf_t)state->num_views;
		    tmat[MSY] /= (fastf_t)state->num_views;
		    tmat[MSZ] /= (fastf_t)state->num_views;
		    tmat[1] /= (fastf_t)state->num_views;
		    tmat[2] /= (fastf_t)state->num_views;
		    tmat[6] /= (fastf_t)state->num_views;

		    /* Lastly, apply the parallel axis theorem */
		    Dx_sq = centroid[X]*centroid[X];
		    Dy_sq = centroid[Y]*centroid[Y];
		    Dz_sq = centroid[Z]*centroid[Z];
		    tmat[MSX] -= avg_mass*(Dy_sq + Dz_sq);
		    tmat[MSY] -= avg_mass*(Dx_sq + Dz_sq);
		    tmat[MSZ] -= avg_mass*(Dx_sq + Dy_sq);
		    tmat[1] += avg_mass*centroid[X]*centroid[Y];
		    tmat[2] += avg_mass*centroid[X]*centroid[Z];
		    tmat[6] += avg_mass*centroid[Y]*centroid[Z];

		    tmat[4] = tmat[1];
		    tmat[8] = tmat[2];
		    tmat[9] = tmat[6];

		    bu_vls_printf(&title, "For the Moments and Products of Inertia For %s", state->obj_tbl[obj].o_name);
		    bn_mat_print_vls(bu_vls_addr(&title), tmat, gedp->ged_result_str);
		    bu_vls_free(&title);
		}
	    }
	}


	if (state->print_per_region_stats) {
	    double *wv;
	    bu_vls_printf(gedp->ged_result_str, "\tregions:\n");
	    for (BU_LIST_FOR (regp, region, &(state->rtip->HeadRegion))) {
		double low = INFINITY;
		double hi = -INFINITY;

		avg_mass = 0.0;

		for (view=0; view < state->num_views; view++) {
		    wv = &((struct gqa_per_region_data *)regp->reg_udata)->r_weight[view];

		    *wv = ((struct gqa_per_region_data *)regp->reg_udata)->r_lenDensity[view] *
		    (state->area[view]/state->shots[view]);

		    *wv /= state->units[WGT]->val;

		    avg_mass += *wv;

		    if (*wv < low) low = *wv;
		    if (*wv > hi) hi = *wv;
		}

		avg_mass /= state->num_views;
		bu_vls_printf(gedp->ged_result_str, "\t%s %g %s +(%g) -(%g)\n",
			      regp->reg_name,
			      avg_mass,
			      state->units[WGT]->name,
			      hi - avg_mass,
			      avg_mass - low);
	    }
	}

	/* print grand totals */
	avg_mass = 0.0;
	for (view=0; view < state->num_views; view++) {
	    avg_mass += state->m_weight[view] =
	    state->m_lenDensity[view] *
	    (state->area[view] / state->shots[view]);
	}

	avg_mass /= state->num_views;
	bu_vls_printf(gedp->ged_result_str, "  Average total weight: %g %s\n", avg_mass / state->units[WGT]->val, state->units[WGT]->name);

	if (state->analysis_flags & ANALYSIS_CENTROIDS &&
	    !ZERO(avg_mass)) {
	    vect_t centroid = VINIT_ZERO;
	    fastf_t Dx_sq, Dy_sq, Dz_sq;
	    fastf_t inv_total_mass = 1.0/avg_mass;

	    for (view=0; view < state->num_views; view++) {
		vect_t torque;
		fastf_t cell_area = state->area[view] / state->shots[view];

		VSCALE(torque, &state->m_lenTorque[view*3], cell_area);
		VADD2(centroid, centroid, torque);
	    }

	    VSCALE(centroid, centroid, 1.0/(fastf_t)state->num_views);
	    VSCALE(centroid, centroid, inv_total_mass);
	    bu_vls_printf(gedp->ged_result_str,
			  "  Average centroid: (%g %g %g) mm\n", V3ARGS(centroid));

	    /* Do the final calculations for the moments of inertia
	     * for the current object.
	     */
	    if (state->analysis_flags & ANALYSIS_MOMENTS) {
		mat_t tmat; /* total mat */

		MAT_ZERO(tmat);
		for (view=0; view < state->num_views; view++) {
		    vectp_t moi = &state->m_moi[view*3];
		    vectp_t poi = &state->m_poi[view*3];

		    tmat[MSX] += moi[X];
		    tmat[MSY] += moi[Y];
		    tmat[MSZ] += moi[Z];
		    tmat[1] += poi[X];
		    tmat[2] += poi[Y];
		    tmat[6] += poi[Z];
		}

		tmat[MSX] /= (fastf_t)state->num_views;
		tmat[MSY] /= (fastf_t)state->num_views;
		tmat[MSZ] /= (fastf_t)state->num_views;
		tmat[1] /= (fastf_t)state->num_views;
		tmat[2] /= (fastf_t)state->num_views;
		tmat[6] /= (fastf_t)state->num_views;

		/* Lastly, apply the parallel axis theorem */
		Dx_sq = centroid[X]*centroid[X];
		Dy_sq = centroid[Y]*centroid[Y];
		Dz_sq = centroid[Z]*centroid[Z];
		tmat[MSX] -= avg_mass*(Dy_sq + Dz_sq);
		tmat[MSY] -= avg_mass*(Dx_sq + Dz_sq);
		tmat[MSZ] -= avg_mass*(Dx_sq + Dy_sq);
		tmat[1] += avg_mass*centroid[X]*centroid[Y];
		tmat[2] += avg_mass*centroid[X]*centroid[Z];
		tmat[6] += avg_mass*centroid[Y]*centroid[Z];

		tmat[4] = tmat[1];
		tmat[8] = tmat[2];
		tmat[9] = tmat[6];

		bn_mat_print_vls("For the Moments and Products of Inertia For\n\tAll Specified Objects",
				 tmat, gedp->ged_result_str);
	    }
	}
    }


    if (state->analysis_flags & ANALYSIS_VOLUMES) {
	bu_vls_printf(gedp->ged_result_str, "Volume:\n");

	/* print per-object */
	for (obj = 0; obj < state->num_objects; obj++) {
	    avg_mass = 0.0;

	    for (view=0; view < state->num_views; view++)
		avg_mass += state->obj_tbl[obj].o_volume[view];

	    avg_mass /= state->num_views;
	    bu_vls_printf(gedp->ged_result_str, "\t%*s %g %s\n", -state->max_region_name_len, state->obj_tbl[obj].o_name,
			  avg_mass / state->units[VOL]->val, state->units[VOL]->name);
	}

	if (state->print_per_region_stats) {
	    double *vv;

	    bu_vls_printf(gedp->ged_result_str, "\tregions:\n");
	    for (BU_LIST_FOR (regp, region, &(state->rtip->HeadRegion))) {
		double low = INFINITY;
		double hi = -INFINITY;
		avg_mass = 0.0;

		for (view=0; view < state->num_views; view++) {
		    vv = &((struct gqa_per_region_data *)regp->reg_udata)->r_volume[view];

		    /* convert view length to a volume */
		    *vv = ((struct gqa_per_region_data *)regp->reg_udata)->r_len[view] *
		    (state->area[view] / state->shots[view]);

		    /* convert to user's units */
		    *vv /= state->units[VOL]->val;

		    /* find limits of values */
		    if (*vv < low) low = *vv;
		    if (*vv > hi) hi = *vv;

		    avg_mass += *vv;
		}

		avg_mass /= state->num_views;

		bu_vls_printf(gedp->ged_result_str, "\t%s volume:%g %s +(%g) -(%g)\n",
			      regp->reg_name,
			      avg_mass,
			      state->units[VOL]->name,
			      hi - avg_mass,
			      avg_mass - low);
	    }
	}


	/* print grand totals */
	avg_mass = 0.0;
	for (view=0; view < state->num_views; view++) {
	    avg_mass += state->m_volume[view] =
	    state->m_len[view] * (state->area[view] / state->shots[view]);
	}

	avg_mass /= state->num_views;
	bu_vls_printf(gedp->ged_result_str, "  Average total volume: %g %s\n", avg_mass / state->units[VOL]->val, state->units[VOL]->name);
    }
    if (state->analysis_flags & ANALYSIS_OVERLAPS) list_report(gedp, state, &state->overlapList);
    if (state->analysis_flags & ANALYSIS_ADJ_AIR) list_report(gedp, state, &state->adjAirList);
    if (state->analysis_flags & ANALYSIS_GAPS) list_report(gedp, state, &state->gapList);
    if (state->analysis_flags & ANALYSIS_EXP_AIR) list_report(gedp, state, &state->exposedAirList);

    for (BU_LIST_FOR (regp, region, &(state->rtip->HeadRegion))) {
	size_t hits;
	struct region_pair *rp;
	int is_overlap_only_hit;

	RT_CK_REGION(regp);
	hits = (size_t)((struct gqa_per_region_data *)regp->reg_udata)->hits;
	if (hits < state->require_num_hits) {
	    if (hits == 0 && !state->quiet_missed_report) {
		is_overlap_only_hit = 0;
		if (state->analysis_flags & ANALYSIS_OVERLAPS) {
		    /* If the region is in the overlap list, it has
		     * been hit even though the hit count is zero.
		     * Do not report zero hit regions if they are in
		     * the overlap list.
		     */
		    for (BU_LIST_FOR (rp, region_pair, &(state->overlapList.l))) {
			if (rp->r.r1->reg_name == regp->reg_name) {
			    is_overlap_only_hit = 1;
			    break;
			} else if (rp->r2) {
			    if (rp->r2->reg_name == regp->reg_name) {
				is_overlap_only_hit = 1;
				break;
			    }
			}
		    }
		}
		if (!is_overlap_only_hit) {
		    bu_vls_printf(gedp->ged_result_str, "%s was not hit\n", regp->reg_name);
		}
	    } else if (hits) {
		bu_vls_printf(gedp->ged_result_str, "%s hit only %zu times (< %zu)\n",
			      regp->reg_name, hits, state->require_num_hits);
	    }
	}
    }
}



/**
 * Perform a surface area analysis pass through libanalyze and append the
 * results to gedp->ged_result_str.
 *
 * This runs as a separate perform_raytracing() call rather than being folded
 * into gqa's own raytracing loop.  This is a deliberate transitional design:
 * once the gqa core is migrated to use libanalyze's backend uniformly, the
 * extra pass will be eliminated.
 *
 * TODO: merge with the main gqa compute pass once the libanalyze backend
 * unification is complete.
 */
static void
gqa_surf_area_pass(struct ged *gedp, struct cstate *state,
		   int start_objs, int argc, const char *argv[])
{
    int i;
    int n_objs = argc - start_objs;
    struct current_state *lib_state;
    char **names;
    double units2;

    if (n_objs <= 0)
	return;

    /* Build a mutable copy of the object name list (perform_raytracing
     * takes char *[], not const char *[]). */
    names = (char **)bu_calloc(n_objs, sizeof(char *), "gqa surf_area names");
    for (i = 0; i < n_objs; i++)
	names[i] = bu_strdup(argv[start_objs + i]);

    /* Create a libanalyze state and configure it to match gqa's settings */
    lib_state = analyze_current_state_init();
    analyze_set_grid_spacing(lib_state, state->gridSpacing, state->gridSpacingLimit);
    analyze_set_ncpu(lib_state, state->ncpu);
    analyze_set_use_air(lib_state, state->use_air);
    if (state->densityFileName)
	analyze_set_densityfile(lib_state, state->densityFileName);

    /* ANALYZE_SURF_AREA == 4, matching libanalyze's ANALYSIS_SURF_AREA */
    if (perform_raytracing(lib_state, gedp->dbip, names, n_objs, ANALYZE_SURF_AREA) == 0) {
	units2 = state->units[LINE]->val * state->units[LINE]->val;

	bu_vls_printf(gedp->ged_result_str, "\nSurface Area:\n");
	for (i = 0; i < n_objs; i++) {
	    fastf_t sa = analyze_surf_area(lib_state, names[i]);
	    bu_vls_printf(gedp->ged_result_str, "\t%s %g %s^2\n",
			 names[i], sa / units2, state->units[LINE]->name);
	}
	bu_vls_printf(gedp->ged_result_str,
		      "\n  Average total surface area: %g %s^2\n",
		      analyze_total_surf_area(lib_state) / units2,
		      state->units[LINE]->name);

	if (state->print_per_region_stats) {
	    int num_regions = analyze_get_num_regions(lib_state);
	    bu_vls_printf(gedp->ged_result_str, "\tregions:\n");
	    for (i = 0; i < num_regions; i++) {
		char *reg_name = NULL;
		double surf_area, high, low;
		analyze_surf_area_region(lib_state, i, &reg_name, &surf_area, &high, &low);
		bu_vls_printf(gedp->ged_result_str,
			      "\t%s surf_area:%g %s^2 +(%g) -(%g)\n",
			      reg_name,
			      surf_area / units2, state->units[LINE]->name,
			      high / units2, low / units2);
	    }
	}
    } else {
	bu_vls_printf(gedp->ged_result_str, "surface area analysis failed\n");
    }

    for (i = 0; i < n_objs; i++)
	bu_free(names[i], "gqa surf_area name");
    bu_free(names, "gqa surf_area names");
    analyze_free_current_state(lib_state);
}


extern "C" int
ged_gqa_core(struct ged *gedp, int argc, const char *argv[])
{
    int arg_count;
    struct rt_i *rtip;
    int i;
    struct cstate state_val;
    struct cstate *state = &state_val;
    memset(state, 0, sizeof(*state));
    state->gedp = gedp;
    int start_objs; /* index in command line args where geom object list starts */
    struct region_pair *rp;
    struct region *regp;
    static const char *usage = "object [object ...]";
    struct resource resp[MAX_PSW];	/* memory resources for multi-cpu processing */
    struct bu_list *vlfree = &rt_vlfree;

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);
    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s %s", argv[0], options_str, usage);
	return GED_HELP;
    }

    state->analysis_flags = ANALYSIS_VOLUMES | ANALYSIS_OVERLAPS | ANALYSIS_WEIGHTS |
    ANALYSIS_EXP_AIR | ANALYSIS_ADJ_AIR | ANALYSIS_GAPS | ANALYSIS_CENTROIDS | ANALYSIS_MOMENTS |
    GQA_ANALYSIS_SURF_AREA;
    state->multiple_analyses = 1;
    state->azimuth_deg = 0.0;
    state->elevation_deg = 0.0;
    state->densityFileName = (char *)0;

    /* FIXME: this is completely arbitrary, should probably be based
     * on the model size.
     */
    state->gridSpacing = 50.0;

    /* default grid spacing limit is based on the current distance
     * tolerance, one order of magnitude greater.
     *
     * FIXME: should probably be based on the model size.
     */
    state->gridSpacingLimit = 10.0 * wdbp->wdb_tol.dist;

    state->makeOverlapAssemblies = 0;
    state->require_num_hits = 1;
    state->max_cpus = state->ncpu = (int)bu_avail_cpus();
    state->Samples_per_model_axis = 2.0;
    state->overlap_tolerance = 0.0;
    state->volume_tolerance = -1.0;
    state->weight_tolerance = -1.0;
    state->print_per_region_stats = 0;
    state->max_region_name_len = 0;
    state->use_air = 1;
    state->num_objects = 0;
    state->num_views = 3;
    state->verbose = 0;
    state->quiet_missed_report = 0;
    state->plot_prefix = NULL;
    state->plot_weight = (FILE *)0;
    state->plot_volume = (FILE *)0;
    state->plot_overlaps = (FILE *)0;
    state->plot_adjair = (FILE *)0;
    state->plot_gaps = (FILE *)0;
    state->plot_expair = (FILE *)0;
    state->debug = 0;
    state->analysis_method = GQA_METHOD_LEGACY;

    /* Initialise per-invocation region-pair lists */
    BU_LIST_INIT_MAGIC(&state->gapList.l, BU_LIST_HEAD_MAGIC);
    state->gapList.r.name = "Gaps";
    BU_LIST_INIT_MAGIC(&state->adjAirList.l, BU_LIST_HEAD_MAGIC);
    state->adjAirList.r.name = "Adjacent Air";
    BU_LIST_INIT_MAGIC(&state->exposedAirList.l, BU_LIST_HEAD_MAGIC);
    state->exposedAirList.r.name = "Exposed Air";
    BU_LIST_INIT_MAGIC(&state->overlapList.l, BU_LIST_HEAD_MAGIC);
    state->overlapList.r.name = "Overlaps";

    /* Default units */
    state->units[LINE] = units_tab_defaults[LINE];
    state->units[VOL]  = units_tab_defaults[VOL];
    state->units[WGT]  = units_tab_defaults[WGT];

    /* parse command line arguments */
    arg_count = parse_args(gedp, state, argc, (char **)argv);

    if (arg_count < 0 || (argc-arg_count) < 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s %s", argv[0], options_str, usage);
	return BRLCAD_ERROR;
    }

    if (state->analysis_flags & ANALYSIS_PLOT_OVERLAPS) {
	state->plot_vbp = bv_vlblock_init(vlfree, 32);
	state->plot_vhead = bv_vlblock_find(state->plot_vbp, 0xFF, 0xFF, 0x00);
    }

    rtip = rt_new_rti(gedp->dbip);
    rtip->useair = state->use_air;

    start_objs = arg_count;
    state->num_objects = argc - arg_count;

    /* Initialize all the per-CPU memory resources.  The number of
     * processors can change at runtime, init them all.
     */
    memset(resp, 0, sizeof(resp));
    for (i = 0; i < MAX_PSW; i++) {
	rt_init_resource(&resp[i], i, rtip);
    }
    state->resp = resp;

    /* Walk trees.  Here we identify any object trees in the database
     * that the user wants included in the ray trace.
     */
    for (; arg_count < argc; arg_count++) {
	if (rt_gettree(rtip, argv[arg_count]) < 0) {
	    fprintf(stderr, "rt_gettree(%s) FAILED\n", argv[arg_count]);
	    return BRLCAD_ERROR;
	}
    }

    if (densities_prep(gedp, state, rtip) != BRLCAD_OK) return BRLCAD_ERROR;

    /* This gets the database ready for ray tracing.  (it precomputes
     * some values, sets up space partitioning, etc.)
     */
    rt_prep_parallel(rtip, state->ncpu);

    /* we now have to subdivide space */
    VSUB2(state->span, rtip->mdl_max, rtip->mdl_min);
    state->area[0] = state->span[1] * state->span[2];
    state->area[1] = state->span[2] * state->span[0];
    state->area[2] = state->span[0] * state->span[1];

    if (state->analysis_flags & ANALYSIS_BBOX) {
	bu_vls_printf(gedp->ged_result_str, "bounding box: %g %g %g  %g %g %g\n",
		      V3ARGS(rtip->mdl_min), V3ARGS(rtip->mdl_max));

	bu_vls_printf(gedp->ged_result_str, "Area: (%g, %g, %g)\n", state->area[X], state->area[Y], state->area[Z]);
    }
    if (state->verbose) bu_vls_printf(gedp->ged_result_str, "state->ncpu: %d\n", state->ncpu);

    /* if the user did not specify the initial grid spacing limit, we
     * need to compute a reasonable one for them.
     */
    if (ZERO(state->gridSpacing)) {
	double min_span = MAX_FASTF;
	VPRINT("span", state->span);

	V_MIN(min_span, state->span[X]);
	V_MIN(min_span, state->span[Y]);
	V_MIN(min_span, state->span[Z]);

	state->gridSpacing = state->gridSpacingLimit;
	do {
	    state->gridSpacing *= 2.0;
	} while (state->gridSpacing < min_span);

	/* dial it back a little bit */
	state->gridSpacing *= 0.25;
	V_MAX(state->gridSpacing, state->gridSpacingLimit);

	bu_log("Trying estimated initial grid spacing: %g %s\n",
	       state->gridSpacing / state->units[LINE]->val, state->units[LINE]->name);
    } else {
	bu_log("Trying initial grid spacing: %g %s\n",
	       state->gridSpacing / state->units[LINE]->val, state->units[LINE]->name);
    }

    bu_log("Using grid spacing lower limit: %g %s\n",
	   state->gridSpacingLimit / state->units[LINE]->val, state->units[LINE]->name);

    if (options_prep(gedp, state, rtip, state->span) != BRLCAD_OK) return BRLCAD_ERROR;

    /* initialize some stuff */
    state->sem_worker = bu_semaphore_register("gqa_sem_worker");
    state->sem_stats = bu_semaphore_register("gqa_sem_stats");
    state->sem_lists = bu_semaphore_register("gqa_sem_lists");
    state->sem_plot = bu_semaphore_register("gqa_sem_plot");
    state->rtip = rtip;
    state->first = 1;
    allocate_per_region_data(gedp, state, start_objs, argc, argv);

    /* ------------------------------------------------------------------
     * Crofton sampler: delegate entirely to analyze_run() which provides
     * the isotropic random ray backend.  Results are printed below and
     * then the function jumps past the legacy compute loop.
     * ------------------------------------------------------------------ */
    if (state->analysis_method == GQA_METHOD_CROFTON) {
	int n_objs = argc - start_objs;
	char **names = (char **)bu_calloc(n_objs, sizeof(char *), "gqa crofton names");
	int ar_flags = 0;
	struct analyze_config cfg = ANALYZE_CONFIG_INIT_ZERO;
	struct analyze_results *ar_res;
	double units2 = state->units[LINE]->val * state->units[LINE]->val;
	double units3 = units2 * state->units[LINE]->val;
	size_t k;

	for (i = 0; i < n_objs; i++)
	    names[i] = bu_strdup(argv[start_objs + i]);

	/* Map gqa analysis_flags to public ANALYZE_* flags. */
	if (state->analysis_flags & ANALYSIS_VOLUMES)   ar_flags |= ANALYZE_VOLUME;
	if (state->analysis_flags & ANALYSIS_WEIGHTS)   ar_flags |= ANALYZE_MASS;
	if (state->analysis_flags & ANALYSIS_OVERLAPS)  ar_flags |= ANALYZE_OVERLAPS;
	if (state->analysis_flags & ANALYSIS_GAPS)      ar_flags |= ANALYZE_GAP;
	if (state->analysis_flags & ANALYSIS_EXP_AIR)   ar_flags |= ANALYZE_EXP_AIR;
	if (state->analysis_flags & ANALYSIS_ADJ_AIR)   ar_flags |= ANALYZE_ADJ_AIR;
	if (state->analysis_flags & ANALYSIS_CENTROIDS) ar_flags |= ANALYZE_CENTROIDS;
	if (state->analysis_flags & ANALYSIS_MOMENTS)   ar_flags |= ANALYZE_MOMENTS;
	if (state->analysis_flags & GQA_ANALYSIS_SURF_AREA) ar_flags |= ANALYZE_SURF_AREA;

	cfg.sampler       = ANALYZE_SAMPLER_CROFTON;
	cfg.grid_spacing  = state->gridSpacing;
	cfg.grid_spacing_min = state->gridSpacingLimit;
	cfg.ncpu          = state->ncpu;
	cfg.use_air       = state->use_air;
	cfg.verbose       = state->verbose;
	cfg.overlap_tol   = state->overlap_tolerance;
	cfg.volume_tol    = state->volume_tolerance;
	cfg.mass_tol      = state->weight_tolerance;
	if (state->densityFileName)
	    cfg.density_file = state->densityFileName;

	ar_res = analyze_run(&cfg, gedp->dbip, names, n_objs, ar_flags);
	if (!ar_res) {
	    bu_vls_printf(gedp->ged_result_str, "Crofton analysis failed.\n");
	} else {
	    bu_vls_printf(gedp->ged_result_str, "\n=== Crofton Analysis ===\n");

	    if (ar_flags & ANALYZE_VOLUME)
		bu_vls_printf(gedp->ged_result_str,
			      "  Total volume:       %g %s^3\n",
			      ar_res->total_volume / units3,
			      state->units[LINE]->name);
	    if (ar_flags & ANALYZE_MASS)
		bu_vls_printf(gedp->ged_result_str,
			      "  Total mass:         %g %s\n",
			      ar_res->total_mass / state->units[WGT]->val,
			      state->units[WGT]->name);
	    if (ar_flags & ANALYZE_SURF_AREA)
		bu_vls_printf(gedp->ged_result_str,
			      "  Total surface area: %g %s^2\n",
			      ar_res->total_surf_area / units2,
			      state->units[LINE]->name);

	    if (ar_res->n_regions > 0 && state->print_per_region_stats) {
		bu_vls_printf(gedp->ged_result_str, "\n  Per-region:\n");
		for (k = 0; k < ar_res->n_regions; k++) {
		    const struct analyze_region_result *rr = &ar_res->regions[k];
		    bu_vls_printf(gedp->ged_result_str, "    %s", rr->name);
		    if (ar_flags & ANALYZE_VOLUME)
			bu_vls_printf(gedp->ged_result_str,
				      "  V=%g %s^3",
				      rr->volume / units3,
				      state->units[LINE]->name);
		    if (ar_flags & ANALYZE_MASS)
			bu_vls_printf(gedp->ged_result_str,
				      "  M=%g %s",
				      rr->mass / state->units[WGT]->val,
				      state->units[WGT]->name);
		    if (ar_flags & ANALYZE_SURF_AREA)
			bu_vls_printf(gedp->ged_result_str,
				      "  SA=%g %s^2",
				      rr->surf_area / units2,
				      state->units[LINE]->name);
		    bu_vls_printf(gedp->ged_result_str, "\n");
		}
	    }

	    if (ar_flags & ANALYZE_OVERLAPS) {
		size_t nov = BU_PTBL_LEN(&ar_res->overlaps);
		bu_vls_printf(gedp->ged_result_str,
			      "\n  Overlaps detected: %zu\n", nov);
		for (k = 0; k < nov; k++) {
		    struct analyze_overlap_record *ov =
			(struct analyze_overlap_record *)BU_PTBL_GET(&ar_res->overlaps, k);
		    bu_vls_printf(gedp->ged_result_str,
				  "    %s / %s  count=%lu  max_depth=%g mm\n",
				  ov->region1, ov->region2 ? ov->region2 : "?",
				  ov->count, ov->max_dist);
		}
	    }

	    if (ar_flags & ANALYZE_GAP) {
		size_t ngaps = BU_PTBL_LEN(&ar_res->gaps);
		if (ngaps > 0) {
		    bu_vls_printf(gedp->ged_result_str,
				  "\n  Gaps detected: %zu\n", ngaps);
		    for (k = 0; k < ngaps; k++) {
			struct analyze_overlap_record *g =
			    (struct analyze_overlap_record *)BU_PTBL_GET(&ar_res->gaps, k);
			bu_vls_printf(gedp->ged_result_str,
				      "    near %s  count=%lu  max_gap=%g mm\n",
				      g->region1, g->count, g->max_dist);
		    }
		}
	    }

	    analyze_results_free(ar_res);
	}

	for (i = 0; i < n_objs; i++)
	    bu_free(names[i], "gqa crofton name");
	bu_free(names, "gqa crofton names");
	goto gqa_aborted;
    }

    /* compute */
    do {
	double inv_spacing = 1.0/state->gridSpacing;
	int view;

	VSCALE(state->steps, state->span, inv_spacing);
	state->steps[0] += 1;
	state->steps[1] += 1;
	state->steps[2] += 1;

	bu_log("Processing with grid spacing %g %s %ld x %ld x %ld\n",
	       state->gridSpacing / state->units[LINE]->val,
	       state->units[LINE]->name,
	       state->steps[0]-1,
	       state->steps[1]-1,
	       state->steps[2]-1);

	/* When rotated-grid mode is active, pre-compute the grids for all
	 * views.  View 0 uses the user-specified az/el; views 1 and 2 use
	 * two orthogonal directions derived from view 0's ray direction.
	 */
	if (state->analysis_method == GQA_METHOD_ROTATED) {
	    /* Set up view 0 from the user az/el (default 0,0 = +X axis) */
	    rotated_grid_setup_ae(&state->rot_grid[0],
				  rtip->mdl_min, rtip->mdl_max,
				  state->azimuth_deg, state->elevation_deg,
				  state->gridSpacing);

	    if (state->num_views > 1) {
		/* view 1: perpendicular to view 0's ray direction */
		vect_t v1_dir;
		bn_vec_perp(v1_dir, state->rot_grid[0].ray_dir);
		VUNITIZE(v1_dir);
		rotated_grid_setup(&state->rot_grid[1],
				   rtip->mdl_min, rtip->mdl_max,
				   v1_dir, state->gridSpacing);
	    }

	    if (state->num_views > 2) {
		/* view 2: cross product of view 0 and view 1 directions */
		vect_t v2_dir;
		VCROSS(v2_dir, state->rot_grid[0].ray_dir, state->rot_grid[1].ray_dir);
		VUNITIZE(v2_dir);
		rotated_grid_setup(&state->rot_grid[2],
				   rtip->mdl_min, rtip->mdl_max,
				   v2_dir, state->gridSpacing);
	    }
	}

	for (view=0; view < state->num_views; view++) {

	    if (state->verbose)
		bu_vls_printf(gedp->ged_result_str, "  view %d\n", view);

	    /* gross hack.  By assuming we have <= 3 views, we can let
	     * the view # indicate a coordinate axis.  Note this is
	     * used as an index into state->area[]
	     */
	    state->i_axis = state->curr_view = view;
	    state->u_axis = (state->curr_view+1) % 3;
	    state->v_axis = (state->curr_view+2) % 3;

	    state->u_dir[state->u_axis] = 1;
	    state->u_dir[state->v_axis] = 0;
	    state->u_dir[state->i_axis] = 0;

	    state->v_dir[state->u_axis] = 0;
	    state->v_dir[state->v_axis] = 1;
	    state->v_dir[state->i_axis] = 0;
	    state->v = 1;

	    if (state->analysis_method == GQA_METHOD_ROTATED) {
		/* Reset traversal counter so each bu_parallel call starts fresh */
		state->rot_grid[view].current = 0;
		state->rot_grid[view].refine_flag = state->first ? 0 : 1;
		bu_parallel(rotated_plane_worker, state->ncpu, (void *)state);
	    } else {
		bu_parallel(plane_worker, state->ncpu, (void *)state);
	    }

	    if (state->aborted)
		goto gqa_aborted;

	    view_reports(gedp, state);
	}

	state->first = 0;
	state->gridSpacing *= GRIDSPACING_STEP;

    } while (terminate_check(gedp, state));

gqa_aborted:
    if (state->plot_overlaps) fclose(state->plot_overlaps);
    if (state->plot_weight) fclose(state->plot_weight);
    if (state->plot_volume) fclose(state->plot_volume);
    if (state->plot_adjair) fclose(state->plot_adjair);
    if (state->plot_gaps) fclose(state->plot_gaps);
    if (state->plot_expair) fclose(state->plot_expair);


    if (state->verbose)
	bu_vls_printf(gedp->ged_result_str, "Computation Done\n");

    if (!state->aborted) {
	summary_reports(gedp, state);

	/* Surface area via libanalyze (separate pass; see gqa_surf_area_pass) */
	if (state->analysis_flags & GQA_ANALYSIS_SURF_AREA)
	    gqa_surf_area_pass(gedp, state, start_objs, argc, argv);

	if (state->analysis_flags & ANALYSIS_PLOT_OVERLAPS) {
	    if (gedp->new_cmd_forms) {
		struct bview *view = gedp->ged_gvp;
		bv_vlblock_obj(state->plot_vbp, view, "gqa::overlaps");
	    } else {
		_ged_cvt_vlblock_to_solids(gedp, state->plot_vbp, "OVERLAPS", 0);
	    }
	}
    } else
	state->aborted = 0; /* reset flag */

    if (state->analysis_flags & ANALYSIS_PLOT_OVERLAPS)
	bv_vlblock_free(state->plot_vbp);

    /* Clear out the lists */
    while (BU_LIST_WHILE (rp, region_pair, &state->overlapList.l)) {
	BU_LIST_DEQUEUE(&rp->l);
	bu_free(rp, "state->overlapList items");
    }
    while (BU_LIST_WHILE (rp, region_pair, &state->adjAirList.l)) {
	BU_LIST_DEQUEUE(&rp->l);
	bu_free(rp, "state->adjAirList items");
    }
    while (BU_LIST_WHILE (rp, region_pair, &state->gapList.l)) {
	BU_LIST_DEQUEUE(&rp->l);
	bu_free(rp, "state->gapList items");
    }
    while (BU_LIST_WHILE (rp, region_pair, &state->exposedAirList.l)) {
	BU_LIST_DEQUEUE(&rp->l);
	bu_free(rp, "state->exposedAirList items");
    }

    /* Free dynamically allocated state */
    bu_free(state->m_lenDensity, "m_lenDensity");
    bu_free(state->m_len, "m_len");
    bu_free(state->m_volume, "m_volume");
    bu_free(state->m_weight, "m_weight");
    bu_free(state->shots, "m_shots");
    bu_free(state->m_lenTorque, "m_lenTorque");
    bu_free(state->m_moi, "m_moi");
    bu_free(state->m_poi, "m_poi");

    for (i = 0; i < state->num_objects; i++) {
	bu_free(state->obj_tbl[i].o_len, "o_len");
	bu_free(state->obj_tbl[i].o_lenDensity, "o_lenDensity");
	bu_free(state->obj_tbl[i].o_volume, "o_volume");
	bu_free(state->obj_tbl[i].o_weight, "o_weight");
	bu_free(state->obj_tbl[i].o_lenTorque, "o_lenTorque");
	bu_free(state->obj_tbl[i].o_moi, "o_moi");
	bu_free(state->obj_tbl[i].o_poi, "o_poi");
    }
    bu_free(state->obj_tbl, "object table");
    state->obj_tbl = NULL;

    for (i = 0, BU_LIST_FOR (regp, region, &(rtip->HeadRegion)), i++) {
	bu_free(state->reg_tbl[i].r_lenDensity, "r_lenDensity");
	bu_free(state->reg_tbl[i].r_len, "r_len");
	bu_free(state->reg_tbl[i].r_volume, "r_volume");
	bu_free(state->reg_tbl[i].r_weight, "r_weight");
    }
    bu_free(state->reg_tbl, "object table");
    state->reg_tbl = NULL;

    if (state->densities) {
	analyze_densities_destroy(state->densities);
	state->densities = NULL;
    }

    if (state->densities_source) {
	bu_free(state->densities_source, "free densities source string");
	state->densities_source = NULL;
    }

    rt_free_rti(rtip);

    return BRLCAD_OK;
}

#include "../include/plugin.h"

#define GED_GQA_COMMANDS(X, XID) \
    X(gqa, ged_gqa_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_GQA_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_gqa", 1, GED_GQA_COMMANDS)

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
