/*                       E D I T 2 . C P P
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
/** @file libged/edit2.cpp
 *
 * New-forms edit command (Phase 1 + Phase 2).
 *
 * Implements a three-pass command-line parser:
 *
 *   Pass 1 — global opts  (-S/-f/-F/-i/-h/-v) harvested before the first
 *             geometry specifier or subcommand token.
 *
 *   Pass 2 — geometry specifiers collected into ged_edit_geom_spec entries.
 *             Each token is tested as: "." batch marker → URI → db_lookup.
 *             If no specifiers are found, the active selection state is used
 *             as a fallback.  Conflicts between an explicit specifier and the
 *             active selection are detected and reported here.
 *
 *   Pass 3 — subcommand dispatch: argv[0] names the operation; the rest are
 *             its arguments, forwarded to the appropriate ged_subcmd handler.
 *
 * All geometry-altering subcommands receive a ged_edit_ctx * (u_data).
 */

#include "common.h"

#include <climits>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "./uri.hh"

#include "bu/cmd.h"
#include "bu/opt.h"
#include "bn/rand.h"
#include "rt/edit.h"

#include "../ged_private.h"
#include "../dbi.h"
#include "./ged_edit2.h"


/* ------------------------------------------------------------------ *
 * Helpers: build a ged_edit_geom_spec from a URI-parsed token
 * ------------------------------------------------------------------ */

/**
 * Attempt to resolve argv[i] as a geometry specifier via DbiState.
 * Fills *spec and returns true on success, false if the token does not
 * resolve to any known database object.
 *
 * Handles:
 *   "."         — batch marker (each object acts as its own reference)
 *   "name#frag" — URI with fragment (e.g. vertex key)
 *   "name?q"    — URI with query (e.g. wildcard feature set)
 *   "path"      — bare name or slash-separated path
 */
static bool
_resolve_geom_spec(ged_edit_geom_spec &spec, const char *token, DbiState *dbis)
{
    spec.raw = token;
    spec.is_batch = false;
    spec.dp = RT_DIR_NULL;

    /* Batch marker */
    if (BU_STR_EQUAL(token, ".")) {
	spec.is_batch = true;
	/* Batch marker is valid without a db lookup */
	return true;
    }

    /* Try URI parse: prefix with "g:" so the class sees a scheme */
    std::string path_str;
    try {
	uri obj_uri(std::string("g:") + std::string(token));
	path_str = obj_uri.get_path();

	if (obj_uri.get_fragment().length() > 0)
	    spec.fragment = obj_uri.get_fragment();
	if (obj_uri.get_query().length() > 0)
	    spec.query = obj_uri.get_query();

	if (path_str.empty())
	    path_str = token;

    } catch (const std::invalid_argument &) {
	path_str = token;
    }

    spec.path = path_str;
    spec.hashes = dbis->digest_path(path_str.c_str());

    if (spec.hashes.empty())
	return false;

    /* Single-element path — get the head dp */
    if (spec.hashes.size() == 1)
	spec.dp = dbis->get_hdp(spec.hashes[0]);

    /* Multi-element path (comb instance) — dp stays RT_DIR_NULL */

    return (spec.hashes.size() > 1) || (spec.dp != RT_DIR_NULL);
}


/* ================================================================== *
 * Phase 2 helpers
 * ================================================================== */

/**
 * Initialise a minimal stack-allocated bview for scripted (CLI) rt_edit
 * operations.  Only the fields accessed by edit_srot/edit_stra/edit_sscale
 * are set; nothing is heap-allocated so no cleanup is required.
 */
static void
_edit_cli_view_init(struct bview *v)
{
    memset(v, 0, sizeof(*v));
    v->magic            = BV_MAGIC;
    v->gv_scale         = 1.0;
    v->gv_rotate_about  = 'k';   /* rotate about keypoint — no view matrices needed */
    v->gv_coord         = 'm';   /* model-space coordinates */
    MAT_IDN(v->gv_model2view);
    MAT_IDN(v->gv_view2model);
    MAT_IDN(v->gv_rotation);
    MAT_IDN(v->gv_center);
}


/**
 * Return the world-space keypoint of a named top-level primitive by
 * loading the geometry and calling OBJ[type].ft_keypoint() directly,
 * which is cheaper than constructing and destroying an rt_edit session.
 * Falls back to (0,0,0) for primitives that have no ft_keypoint handler.
 */
static int
_edit_get_obj_keypoint(point_t *kp, const char *name, struct ged *gedp)
{
    struct directory *dp = db_lookup(gedp->dbip, name, LOOKUP_QUIET);
    if (dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    struct rt_db_internal ip;
    RT_DB_INTERNAL_INIT(&ip);
    if (rt_db_get_internal(&ip, dp, gedp->dbip, bn_mat_identity,
			   &rt_uniresource) < 0)
	return BRLCAD_ERROR;

    VSETALL(*kp, 0.0);

    if (OBJ[ip.idb_type].ft_keypoint) {
	struct bn_tol tol = BN_TOL_INIT_TOL;
	OBJ[ip.idb_type].ft_keypoint(kp, NULL, bn_mat_identity, &ip, &tol);
    }

    rt_db_free_internal(&ip);
    return BRLCAD_OK;
}


/**
 * Apply a scripted edit to dp through the temporary edit buffer.
 *
 *   1. If dp already has a live buffer entry (from a previous -i operation),
 *      reuse it; otherwise create a fresh rt_edit from the on-disk geometry.
 *   2. Install a minimal CLI bview so edit_srot() resolves its rotate-about
 *      axis without dereferencing a null vp.
 *   3. Run do_edit(s); return early on error.
 *   4. flag_i == 0 (normal): promote es_int to disk and clear buffer entry.
 *      flag_i != 0: leave the entry in the buffer for a future operation.
 */
static int
_edit_xform_apply(struct ged *gedp,
		  struct directory *dp,
		  int flag_i,
		  std::function<int(struct rt_edit *)> do_edit)
{
    if (!gedp || !dp)
	return BRLCAD_ERROR;

    struct db_full_path dfp;
    db_full_path_init(&dfp);
    db_add_node_to_full_path(&dfp, dp);

    /* Re-use existing buffer entry, or create a fresh one */
    struct rt_edit *s = ged_edit_buf_get(gedp, &dfp);
    bool is_new = (s == NULL);

    if (is_new) {
	struct bn_tol tol = BN_TOL_INIT_TOL;
	s = rt_edit_create(&dfp, gedp->dbip, &tol, NULL);
	if (!s) {
	    db_free_full_path(&dfp);
	    return BRLCAD_ERROR;
	}
    }

    /* Temporarily install a minimal CLI bview (stack-allocated) */
    struct bview cli_v;
    _edit_cli_view_init(&cli_v);
    struct bview *saved_vp = s->vp;
    s->vp = &cli_v;

    int ret = do_edit(s);

    s->vp = saved_vp;

    if (ret != BRLCAD_OK) {
	if (is_new)
	    rt_edit_destroy(s);
	db_free_full_path(&dfp);
	return BRLCAD_ERROR;
    }

    if (is_new) {
	/* Transfer ownership to the buffer */
	ged_edit_buf_set(gedp, &dfp, s);
    }
    /* s is now in the buffer */

    int result;
    if (flag_i) {
	result = BRLCAD_OK;                       /* leave in buffer */
    } else {
	result = ged_edit_buf_promote(gedp, &dfp); /* write to disk  */
    }

    db_free_full_path(&dfp);
    return result;
}


/* ================================================================== *
 * Subcommand implementations
 * ================================================================== */

/* ------------------------------------------------------------------ *
 * translate
 * ------------------------------------------------------------------ */
class cmd_translate : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] translate [-a|-r] [-x|-y|-z] [-k FROM] TO"); }
	std::string purpose() { return std::string("translate primitive or comb instance"); }
	int exec(struct ged *, void *, int, const char **);
};
static cmd_translate edit_translate_cmd;

int
cmd_translate::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    struct ged_edit_ctx *ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    argc--; argv++;   /* skip "translate" */

    /* ---- Option parsing ------------------------------------------ */
    int abs_flag = 0, rel_flag = 0;
    int x_only = 0, y_only = 0, z_only = 0, n_flag = 0;
    struct bu_vls k_vls = BU_VLS_INIT_ZERO;

    struct bu_opt_desc d[9];
    BU_OPT(d[0], "a", "absolute", "",  NULL,       &abs_flag, "Absolute position");
    BU_OPT(d[1], "r", "relative", "",  NULL,       &rel_flag, "Relative offset (default)");
    BU_OPT(d[2], "x", "",         "",  NULL,       &x_only,   "X axis only");
    BU_OPT(d[3], "y", "",         "",  NULL,       &y_only,   "Y axis only");
    BU_OPT(d[4], "z", "",         "",  NULL,       &z_only,   "Z axis only");
    BU_OPT(d[5], "k", "",         "#", bu_opt_vls, &k_vls,    "FROM reference object");
    BU_OPT(d[6], "n", "",         "",  NULL,       &n_flag,   "Natural origin");
    BU_OPT_NULL(d[7]);

    struct bu_vls opterrs = BU_VLS_INIT_ZERO;
    int nrem = bu_opt_parse(&opterrs, argc, argv, d);
    bu_vls_free(&opterrs);

    if (nrem < 0) {
	bu_vls_free(&k_vls);
	bu_vls_printf(gedp->ged_result_str, "%s\n", usage().c_str());
	return BRLCAD_ERROR;
    }

    const char *k_arg = bu_vls_strlen(&k_vls) ? bu_vls_cstr(&k_vls) : NULL;
    int n_coord_flags = x_only + y_only + z_only;

    /* ---- Parse destination coordinates / object name -------------- */
    vect_t to_vec = VINIT_ZERO;
    const char *to_name = NULL;

    if (n_coord_flags == 0) {
	/* Try as 3-component vector first */
	int vret = bu_opt_vect_t(NULL, nrem, argv, &to_vec);
	if (vret > 0) {
	    /* got coordinates */
	} else if (nrem >= 1) {
	    /* treat as object name */
	    to_name = argv[0];
	} else {
	    bu_vls_free(&k_vls);
	    bu_vls_printf(gedp->ged_result_str, "translate: missing destination\n");
	    return BRLCAD_ERROR;
	}
    } else {
	/* Read one float per selected axis, in X/Y/Z order */
	int ci = 0;
	if (x_only && ci < nrem && bu_opt_fastf_t(NULL, 1, &argv[ci], &to_vec[X]) < 0) {
	    bu_vls_free(&k_vls);
	    bu_vls_printf(gedp->ged_result_str, "translate: bad X value '%s'\n", argv[ci]);
	    return BRLCAD_ERROR;
	}
	if (x_only) ci++;
	if (y_only && ci < nrem && bu_opt_fastf_t(NULL, 1, &argv[ci], &to_vec[Y]) < 0) {
	    bu_vls_free(&k_vls);
	    bu_vls_printf(gedp->ged_result_str, "translate: bad Y value '%s'\n", argv[ci]);
	    return BRLCAD_ERROR;
	}
	if (y_only) ci++;
	if (z_only && ci < nrem && bu_opt_fastf_t(NULL, 1, &argv[ci], &to_vec[Z]) < 0) {
	    bu_vls_free(&k_vls);
	    bu_vls_printf(gedp->ged_result_str, "translate: bad Z value '%s'\n", argv[ci]);
	    return BRLCAD_ERROR;
	}
    }

    /* Capture locals for the lambda */
    int  flag_i    = ctx->flag_i;
    bool do_abs    = (abs_flag && !rel_flag);

    int ret = _edit_xform_apply(gedp, ctx->dp, flag_i,
	[&](struct rt_edit *s) -> int
	{
	    vect_t target;

	    if (to_name) {
		/* TO is an object name */
		point_t to_kp;
		if (_edit_get_obj_keypoint(&to_kp, to_name, gedp) != BRLCAD_OK) {
		    bu_vls_printf(gedp->ged_result_str,
			"translate: cannot resolve object '%s'\n", to_name);
		    return BRLCAD_ERROR;
		}
		if (k_arg) {
		    /* Move so that FROM keypoint coincides with TO keypoint */
		    point_t from_kp;
		    if (_edit_get_obj_keypoint(&from_kp, k_arg, gedp) != BRLCAD_OK) {
			bu_vls_printf(gedp->ged_result_str,
			    "translate: cannot resolve -k object '%s'\n", k_arg);
			return BRLCAD_ERROR;
		    }
		    vect_t delta;
		    VSUB2(delta, to_kp, from_kp);
		    VADD2(target, s->e_keypoint, delta);
		} else {
		    /* Absolute: move directly to the object's keypoint */
		    VMOVE(target, to_kp);
		}
	    } else if (do_abs) {
		/* Absolute translate: to_vec IS the target position */
		VMOVE(target, to_vec);
		/* Keep unspecified axes at the current keypoint */
		if (n_coord_flags > 0) {
		    if (!x_only) target[X] = s->e_keypoint[X];
		    if (!y_only) target[Y] = s->e_keypoint[Y];
		    if (!z_only) target[Z] = s->e_keypoint[Z];
		}
	    } else {
		/* Relative translate (default): to_vec is a delta */
		vect_t delta = VINIT_ZERO;
		if (n_coord_flags > 0) {
		    if (x_only) delta[X] = to_vec[X];
		    if (y_only) delta[Y] = to_vec[Y];
		    if (z_only) delta[Z] = to_vec[Z];
		} else {
		    VMOVE(delta, to_vec);
		}
		VADD2(target, s->e_keypoint, delta);
	    }

	    VMOVE(s->e_para, target);
	    s->e_inpara = 3;
	    rt_edit_set_edflag(s, RT_PARAMS_EDIT_TRANS);
	    rt_edit_process(s);
	    return BRLCAD_OK;
	});

    bu_vls_free(&k_vls);
    return ret;
}


/* ------------------------------------------------------------------ *
 * tra (alias: translate -r)
 * ------------------------------------------------------------------ */
class cmd_tra : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] tra X Y Z"); }
	std::string purpose() { return std::string("relative translate (alias for translate -r X Y Z)"); }
	int exec(struct ged *, void *, int, const char **);
};
static cmd_tra edit_tra_cmd;

int
cmd_tra::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    struct ged_edit_ctx *ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    argc--; argv++;   /* skip "tra" */

    vect_t delta = VINIT_ZERO;
    int vret = bu_opt_vect_t(NULL, argc, argv, &delta);
    if (vret < 0) {
	bu_vls_printf(gedp->ged_result_str, "%s\n", usage().c_str());
	return BRLCAD_ERROR;
    }

    return _edit_xform_apply(gedp, ctx->dp, ctx->flag_i,
	[&](struct rt_edit *s) -> int
	{
	    vect_t target;
	    VADD2(target, s->e_keypoint, delta);
	    VMOVE(s->e_para, target);
	    s->e_inpara = 3;
	    rt_edit_set_edflag(s, RT_PARAMS_EDIT_TRANS);
	    rt_edit_process(s);
	    return BRLCAD_OK;
	});
}


/* ------------------------------------------------------------------ *
 * rotate
 * ------------------------------------------------------------------ */
class cmd_rotate : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] rotate [-R] [-x|-y|-z] X [Y [Z]]"); }
	std::string purpose() { return std::string("rotate primitive or comb instance (Euler angles in degrees, or radians with -R)"); }
	int exec(struct ged *, void *, int, const char **);
};
static cmd_rotate edit_rotate_cmd;

int
cmd_rotate::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    struct ged_edit_ctx *ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    argc--; argv++;   /* skip "rotate" */

    /* ---- Option parsing ------------------------------------------ */
    int rad_flag = 0;
    int x_only = 0, y_only = 0, z_only = 0;

    struct bu_opt_desc d[6];
    BU_OPT(d[0], "R", "",  "",  NULL, &rad_flag, "Interpret angles as radians");
    BU_OPT(d[1], "x", "",  "",  NULL, &x_only,   "Rotate about X axis only");
    BU_OPT(d[2], "y", "",  "",  NULL, &y_only,   "Rotate about Y axis only");
    BU_OPT(d[3], "z", "",  "",  NULL, &z_only,   "Rotate about Z axis only");
    BU_OPT_NULL(d[4]);

    struct bu_vls opterrs = BU_VLS_INIT_ZERO;
    int nrem = bu_opt_parse(&opterrs, argc, argv, d);
    bu_vls_free(&opterrs);

    if (nrem < 0) {
	bu_vls_printf(gedp->ged_result_str, "%s\n", usage().c_str());
	return BRLCAD_ERROR;
    }

    int n_coord_flags = x_only + y_only + z_only;

    /* ---- Parse angle values --------------------------------------- */
    vect_t angles = VINIT_ZERO;

    if (n_coord_flags == 0) {
	if (nrem < 1) {
	    bu_vls_printf(gedp->ged_result_str,
		"rotate: missing angle(s)\n");
	    return BRLCAD_ERROR;
	}
	if (nrem == 1) {
	    /* Single unconstrained angle — 180° is ambiguous */
	    fastf_t a = 0.0;
	    if (bu_opt_fastf_t(NULL, 1, &argv[0], &a) < 0) {
		bu_vls_printf(gedp->ged_result_str,
		    "rotate: bad angle value '%s'\n", argv[0]);
		return BRLCAD_ERROR;
	    }
	    fastf_t adeg = rad_flag ? (a * RAD2DEG) : a;
	    if (NEAR_EQUAL(fabs(adeg), 180.0, 1e-10)) {
		bu_vls_printf(gedp->ged_result_str,
		    "rotate: single-angle 180° is ambiguous — "
		    "specify axis with -x/-y/-z or use three angles\n");
		return BRLCAD_ERROR;
	    }
	    /* Default single-angle axis: Z */
	    angles[Z] = a;
	} else if (nrem == 2) {
	    /* Two angles → X, Y (Z = 0) */
	    if (bu_opt_fastf_t(NULL, 1, &argv[0], &angles[X]) < 0 ||
		bu_opt_fastf_t(NULL, 1, &argv[1], &angles[Y]) < 0)
	    {
		bu_vls_printf(gedp->ged_result_str,
		    "rotate: bad angle values\n");
		return BRLCAD_ERROR;
	    }
	} else {
	    /* Three angles: X, Y, Z via bu_opt_vect_t */
	    if (bu_opt_vect_t(NULL, nrem, argv, &angles) < 0) {
		bu_vls_printf(gedp->ged_result_str,
		    "rotate: bad angle values\n");
		return BRLCAD_ERROR;
	    }
	}
    } else if (n_coord_flags == 1) {
	/* Single axis specified: read one angle */
	if (nrem < 1) {
	    bu_vls_printf(gedp->ged_result_str,
		"rotate: missing angle\n");
	    return BRLCAD_ERROR;
	}
	fastf_t a = 0.0;
	if (bu_opt_fastf_t(NULL, 1, &argv[0], &a) < 0) {
	    bu_vls_printf(gedp->ged_result_str,
		"rotate: bad angle value '%s'\n", argv[0]);
	    return BRLCAD_ERROR;
	}
	if (x_only) angles[X] = a;
	else if (y_only) angles[Y] = a;
	else angles[Z] = a;
    } else {
	/* Multiple axis flags: read one angle per flag in X/Y/Z order */
	int ci = 0;
	if (x_only && ci < nrem)
	    bu_opt_fastf_t(NULL, 1, &argv[ci++], &angles[X]);
	if (y_only && ci < nrem)
	    bu_opt_fastf_t(NULL, 1, &argv[ci++], &angles[Y]);
	if (z_only && ci < nrem)
	    bu_opt_fastf_t(NULL, 1, &argv[ci++], &angles[Z]);
    }

    /* Convert radians → degrees if -R was given */
    if (rad_flag) {
	angles[X] *= RAD2DEG;
	angles[Y] *= RAD2DEG;
	angles[Z] *= RAD2DEG;
    }

    return _edit_xform_apply(gedp, ctx->dp, ctx->flag_i,
	[&](struct rt_edit *s) -> int
	{
	    /* Reset accumulated rotation so the supplied angles are applied
	     * relative to the current geometry state, not accumulated with
	     * any previous interactive rotation */
	    MAT_IDN(s->acc_rot_sol);
	    s->e_para[0] = angles[X];
	    s->e_para[1] = angles[Y];
	    s->e_para[2] = angles[Z];
	    s->e_inpara  = 3;
	    rt_edit_set_edflag(s, RT_PARAMS_EDIT_ROT);
	    rt_edit_process(s);
	    return BRLCAD_OK;
	});
}


/* ------------------------------------------------------------------ *
 * scale
 * ------------------------------------------------------------------ */
class cmd_scale : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] scale [-k FROM] [-a|-r TO] FACTOR"); }
	std::string purpose() { return std::string("uniformly scale primitive or comb instance (factor must be > 0)"); }
	int exec(struct ged *, void *, int, const char **);
};
static cmd_scale edit_scale_cmd;

int
cmd_scale::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    struct ged_edit_ctx *ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    argc--; argv++;   /* skip "scale" */

    /* Manual scan: -k/-a/-r each take 1-3 numbers; use bu_opt_* to
     * parse those numbers.  Boolean flags are handled inline.           */
    vect_t k_vec = VINIT_ZERO;
    vect_t a_vec = VINIT_ZERO;
    vect_t r_vec = VINIT_ZERO;
    bool have_k = false, have_a = false, have_r = false;
    vect_t pos_vals = VINIT_ZERO;
    int n_pos = 0;

    int i = 0;
    while (i < argc) {
	if (BU_STR_EQUAL(argv[i], "-k") && i + 1 < argc) {
	    i++;
	    int vret = bu_opt_vect_t(NULL, argc - i, argv + i, &k_vec);
	    if (vret < 0) {
		bu_vls_printf(gedp->ged_result_str,
		    "scale: bad -k value '%s'\n", argv[i]);
		return BRLCAD_ERROR;
	    }
	    i += vret;
	    have_k = true;
	} else if (BU_STR_EQUAL(argv[i], "-a") && i + 1 < argc) {
	    i++;
	    int vret = bu_opt_vect_t(NULL, argc - i, argv + i, &a_vec);
	    if (vret < 0) {
		bu_vls_printf(gedp->ged_result_str,
		    "scale: bad -a value '%s'\n", argv[i]);
		return BRLCAD_ERROR;
	    }
	    i += vret;
	    have_a = true;
	} else if (BU_STR_EQUAL(argv[i], "-r") && i + 1 < argc) {
	    i++;
	    int vret = bu_opt_vect_t(NULL, argc - i, argv + i, &r_vec);
	    if (vret > 0) {
		i += vret;
	    } else {
		fastf_t f = 0.0;
		if (bu_opt_fastf_t(NULL, 1, &argv[i], &f) < 0) {
		    bu_vls_printf(gedp->ged_result_str,
			"scale: bad -r value '%s'\n", argv[i]);
		    return BRLCAD_ERROR;
		}
		r_vec[X] = r_vec[Y] = r_vec[Z] = f;
		i++;
	    }
	    have_r = true;
	} else if (BU_STR_EQUAL(argv[i], "-c") && i + 1 < argc) {
	    /* Center argument — consumed but not yet acted upon */
	    i += 2;
	} else if (BU_STR_EQUAL(argv[i], "-n")) {
	    i++;
	} else if (argv[i][0] != '-') {
	    /* Positional: the scale factor (1 or 3 numbers) */
	    int vret = bu_opt_vect_t(NULL, argc - i, argv + i, &pos_vals);
	    if (vret > 0) {
		n_pos = vret;
		i += vret;
	    } else {
		fastf_t f = 0.0;
		if (bu_opt_fastf_t(NULL, 1, &argv[i], &f) < 0) {
		    bu_vls_printf(gedp->ged_result_str,
			"scale: bad factor value '%s'\n", argv[i]);
		    return BRLCAD_ERROR;
		}
		pos_vals[X] = pos_vals[Y] = pos_vals[Z] = f;
		n_pos = 1;
		i++;
	    }
	} else {
	    i++;   /* unknown flag — skip */
	}
    }

    /* ---- Compute a uniform scale factor from whatever was provided - */
    /* Helper: uniform factor from a vector (all-equal → first component,
     * otherwise RMS of the three components).                           */
    auto vec_to_factor = [](const vect_t v) -> fastf_t {
	if (NEAR_EQUAL(v[X], v[Y], SMALL_FASTF) &&
	    NEAR_EQUAL(v[Y], v[Z], SMALL_FASTF))
	    return v[X];
	return sqrt(VDOT(v, v) / 3.0);
    };

    fastf_t factor = 1.0;
    if (have_r) {
	factor = vec_to_factor(r_vec);
    } else if (have_k && have_a) {
	/* Factor from the FROM→TO reference vector */
	vect_t diff;
	VSUB2(diff, a_vec, k_vec);
	factor = vec_to_factor(diff);
    } else if (n_pos > 0) {
	factor = vec_to_factor(pos_vals);
    } else {
	bu_vls_printf(gedp->ged_result_str, "%s\n", usage().c_str());
	return BRLCAD_ERROR;
    }

    if (factor <= 0.0) {
	bu_vls_printf(gedp->ged_result_str,
	    "scale: factor must be > 0 (got %g)\n", factor);
	return BRLCAD_ERROR;
    }

    return _edit_xform_apply(gedp, ctx->dp, ctx->flag_i,
	[&](struct rt_edit *s) -> int
	{
	    /* Set es_scale directly; bypass the e_para/acc_sc_sol accumulator
	     * so that each CLI scale call is relative to current geometry.  */
	    s->es_scale = factor;
	    s->e_inpara  = 0;
	    rt_edit_set_edflag(s, RT_PARAMS_EDIT_SCALE);
	    rt_edit_process(s);
	    return BRLCAD_OK;
	});
}


/* ------------------------------------------------------------------ *
 * checkpoint
 * ------------------------------------------------------------------ */
class cmd_checkpoint : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] checkpoint"); }
	std::string purpose() { return std::string("save a restore-point for the current edit session"); }
	int exec(struct ged *, void *, int, const char **);
};
static cmd_checkpoint edit_checkpoint_cmd;

int
cmd_checkpoint::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    struct ged_edit_ctx *ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    struct db_full_path dfp;
    db_full_path_init(&dfp);
    db_add_node_to_full_path(&dfp, ctx->dp);

    /* Use existing buffer entry or create a new one */
    struct rt_edit *s = ged_edit_buf_get(gedp, &dfp);
    bool is_new = (s == NULL);
    if (is_new) {
	struct bn_tol tol = BN_TOL_INIT_TOL;
	s = rt_edit_create(&dfp, gedp->dbip, &tol, NULL);
	if (!s) {
	    db_free_full_path(&dfp);
	    return BRLCAD_ERROR;
	}
	ged_edit_buf_set(gedp, &dfp, s);
    }

    int ret = rt_edit_checkpoint(s);
    db_free_full_path(&dfp);
    return ret;
}


/* ------------------------------------------------------------------ *
 * revert
 * ------------------------------------------------------------------ */
class cmd_revert : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] revert"); }
	std::string purpose() { return std::string("restore the edit session to the last checkpoint"); }
	int exec(struct ged *, void *, int, const char **);
};
static cmd_revert edit_revert_cmd;

int
cmd_revert::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    struct ged_edit_ctx *ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    struct db_full_path dfp;
    db_full_path_init(&dfp);
    db_add_node_to_full_path(&dfp, ctx->dp);

    struct rt_edit *s = ged_edit_buf_get(gedp, &dfp);
    if (!s) {
	bu_vls_printf(gedp->ged_result_str,
	    "revert: no active edit session for '%s'\n", ctx->dp->d_namep);
	db_free_full_path(&dfp);
	return BRLCAD_ERROR;
    }

    int ret = rt_edit_revert(s);
    if (ret == BRLCAD_OK && !ctx->flag_i)
	ret = ged_edit_buf_promote(gedp, &dfp);

    db_free_full_path(&dfp);
    return ret;
}


/* ------------------------------------------------------------------ *
 * reset
 * ------------------------------------------------------------------ */
class cmd_reset : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] reset"); }
	std::string purpose() { return std::string("abandon in-buffer edit state and revert to on-disk geometry"); }
	int exec(struct ged *, void *, int, const char **);
};
static cmd_reset edit_reset_cmd;

int
cmd_reset::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    struct ged_edit_ctx *ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    struct db_full_path dfp;
    db_full_path_init(&dfp);
    db_add_node_to_full_path(&dfp, ctx->dp);

    ged_edit_buf_abandon(gedp, &dfp);
    db_free_full_path(&dfp);
    return BRLCAD_OK;
}


/* ------------------------------------------------------------------ *
 * mat  — apply a raw 4×4 matrix to the primitive
 * ------------------------------------------------------------------ */
class cmd_mat : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] mat M00 M01 ... M33"); }
	std::string purpose() { return std::string("apply a 4x4 matrix (row-major, 16 values) to the primitive"); }
	int exec(struct ged *, void *, int, const char **);
};
static cmd_mat edit_mat_cmd;

int
cmd_mat::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    struct ged_edit_ctx *ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    argc--; argv++;   /* skip "mat" */

    if (argc < 16) {
	bu_vls_printf(gedp->ged_result_str,
	    "mat: need 16 values (4x4 matrix, row-major order)\n");
	return BRLCAD_ERROR;
    }

    mat_t mat;
    for (int mi = 0; mi < 16; mi++) {
	if (bu_opt_fastf_t(NULL, 1, &argv[mi], &mat[mi]) < 0) {
	    bu_vls_printf(gedp->ged_result_str,
		"mat: bad matrix element [%d]: '%s'\n", mi, argv[mi]);
	    return BRLCAD_ERROR;
	}
    }

    return _edit_xform_apply(gedp, ctx->dp, ctx->flag_i,
	[&](struct rt_edit *s) -> int
	{
	    struct rt_db_internal *ip = &s->es_int;
	    if (!ip->idb_meth || !ip->idb_meth->ft_mat) {
		bu_vls_printf(gedp->ged_result_str,
		    "mat: primitive type does not support matrix application\n");
		return BRLCAD_ERROR;
	    }
	    (*ip->idb_meth->ft_mat)(ip, mat, ip);
	    return BRLCAD_OK;
	});
}


// Perturb command
class cmd_perturb : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] perturb factor"); }
	std::string purpose() { return std::string("perturb primitive or primitives below comb by the specified factor (must be greater than 0)"); }
	int exec(struct ged *, void *, int, const char **);
	struct ged_edit_ctx *ctx;
    private:
	int dp_perturb(struct directory *dp);
	fastf_t factor = 0;
};
static cmd_perturb edit_perturb_cmd;

int
cmd_perturb::dp_perturb(struct directory *dp)
{
    fastf_t lfactor = factor + factor*0.1*bn_rand_half(ctx->prand);
    bu_log("%s: %g\n", dp->d_namep, lfactor);
    struct rt_db_internal intern;
    RT_DB_INTERNAL_INIT(&intern);
    struct db_i *dbip = ctx->gedp->dbip;
    if (rt_db_get_internal(&intern, dp, dbip, NULL, &rt_uniresource) < 0) {
	bu_log("rt_db_get_internal failed for %s\n", dp->d_namep);
	return BRLCAD_ERROR;
    }

    if (!intern.idb_meth || !intern.idb_meth->ft_perturb) {
	return BRLCAD_ERROR;
    }

    struct rt_db_internal *pintern;
    if (intern.idb_meth->ft_perturb(&pintern, &intern, 1, lfactor) != BRLCAD_OK) {
	bu_log("librt perturbation failed for %s\n", dp->d_namep);
	return BRLCAD_ERROR;
    }
    if (!pintern) {
	bu_log("librt perturbation failed for %s\n", dp->d_namep);
	return BRLCAD_ERROR;
    }

    std::string oname(dp->d_namep);
    db_delete(dbip, dp);
    db_dirdelete(dbip, dp);
    struct directory *ndp = db_diradd(dbip, oname.c_str(), RT_DIR_PHONY_ADDR, 0, RT_DIR_SOLID, (void *)&pintern->idb_type);
    if (ndp == RT_DIR_NULL) {
	bu_log("Cannot add %s to directory\n", oname.c_str());
	rt_db_free_internal(pintern);
	return BRLCAD_ERROR;
    }

    if (rt_db_put_internal(ndp, dbip, pintern, &rt_uniresource) < 0) {
	bu_log("Failed to write %s to database\n", oname.c_str());
	rt_db_free_internal(pintern);
	return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}

int
cmd_perturb::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    argc--; argv++;

    if (argc < 1 || !argv) {
	bu_vls_printf(gedp->ged_result_str, "%s\n", usage().c_str());
	return BRLCAD_ERROR;
    }

    if (bu_opt_fastf_t(NULL, 1, argv, (void *)&factor) != 1) {
	bu_vls_printf(gedp->ged_result_str, "%s\n", usage().c_str());
	return BRLCAD_ERROR;
    }
    if (NEAR_ZERO(factor, SMALL_FASTF))
	return BRLCAD_OK;

    struct bu_ptbl objs = BU_PTBL_INIT_ZERO;
    if (db_search(&objs, DB_SEARCH_RETURN_UNIQ_DP, "-type shape", 1, &ctx->dp, ctx->gedp->dbip, NULL, NULL, NULL) < 0) {
	bu_vls_printf(gedp->ged_result_str, "search error\n");
	return BRLCAD_ERROR;
    }
    if (!BU_PTBL_LEN(&objs)) {
	bu_vls_printf(gedp->ged_result_str, "no solids\n");
	return BRLCAD_OK;
    }

    int ret = BRLCAD_OK;
    for (size_t i = 0; i < BU_PTBL_LEN(&objs); i++) {
	struct directory *odp = (struct directory *)BU_PTBL_GET(&objs, i);
	int oret = dp_perturb(odp);
	if (oret != BRLCAD_OK)
	    ret = BRLCAD_ERROR;
    }
    bu_ptbl_free(&objs);

    DbiState *dbis = (DbiState *)ctx->gedp->dbi_state;
    dbis->update();

    return ret;
}


/* ================================================================== *
 * Main entry point — three-pass parser
 * ================================================================== */

extern "C" int
ged_edit2_core(struct ged *gedp, int argc, const char *argv[])
{
    int help = 0;

    /* ---- Initialise context ---------------------------------------- */
    struct ged_edit_ctx ctx;
    ctx.gedp         = gedp;
    ctx.verbosity    = 0;
    ctx.prand        = NULL;
    ctx.flag_S       = 0;
    ctx.flag_f       = 0;
    ctx.flag_F       = 0;
    ctx.flag_i       = 0;
    ctx.from_selection = false;
    ctx.has_conflict = false;
    ctx.dp           = RT_DIR_NULL;
    bn_rand_init(ctx.prand, 0);

    bu_vls_trunc(gedp->ged_result_str, 0);

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);

    /* Skip past the "edit" command name */
    argc--; argv++;

    /* ---- Build subcommand map --------------------------------------- */
    std::map<std::string, ged_subcmd *> edit_cmds;
    edit_cmds["rot"]        = &edit_rotate_cmd;
    edit_cmds["rotate"]     = &edit_rotate_cmd;
    edit_cmds["tra"]        = &edit_tra_cmd;
    edit_cmds["translate"]  = &edit_translate_cmd;
    edit_cmds["sca"]        = &edit_scale_cmd;
    edit_cmds["scale"]      = &edit_scale_cmd;
    edit_cmds["perturb"]    = &edit_perturb_cmd;
    edit_cmds["checkpoint"] = &edit_checkpoint_cmd;
    edit_cmds["revert"]     = &edit_revert_cmd;
    edit_cmds["reset"]      = &edit_reset_cmd;
    edit_cmds["mat"]        = &edit_mat_cmd;

    /* ---- Global option descriptors --------------------------------- */
    struct bu_opt_desc d[8];
    BU_OPT(d[0], "h", "help",         "",  NULL, &help,        "Print help");
    BU_OPT(d[1], "v", "verbose",      "",  NULL, &ctx.verbosity,"Verbose output");
    BU_OPT(d[2], "S", "selection",    "",  NULL, &ctx.flag_S,  "Operate on selection (ignore cmd-line specifier)");
    BU_OPT(d[3], "f", "force",        "",  NULL, &ctx.flag_f,  "Force: apply op, write to disk, clear conflict");
    BU_OPT(d[4], "F", "abandon",      "",  NULL, &ctx.flag_F,  "Abandon: discard intermediate state, use on-disk");
    BU_OPT(d[5], "i", "intermediate", "",  NULL, &ctx.flag_i,  "Intermediate: apply to temp buffer only (no disk write)");
    BU_OPT_NULL(d[6]);

    const char *bargs_help = "[options] <geometry_specifier> subcommand [args]";

    if (!argc) {
	_ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds, "edit", bargs_help, 0, NULL);
	return BRLCAD_OK;
    }

    /* Note whether the first token looks like an option.  If so we
     * can't reliably distinguish option errors from bad geometry specs. */
    bool maybe_opts = (argv[0][0] == '-');

    /* ---- Pass 1: find positions of first geometry spec and subcommand
     *              so we know how many leading tokens to feed to
     *              bu_opt_parse as global options.                      */
    DbiState *dbis = (DbiState *)gedp->dbi_state;

    int geom_pos = INT_MAX;
    int cmd_pos  = INT_MAX;
    std::vector<unsigned long long> gs;

    for (int i = 0; i < argc; i++) {
	/* Check if this token matches a known subcommand name */
	if (edit_cmds.find(std::string(argv[i])) != edit_cmds.end()) {
	    if (cmd_pos == INT_MAX)
		cmd_pos = i;
	    break;
	}

	/* Try to resolve as a geometry specifier */
	ged_edit_geom_spec spec;
	if (_resolve_geom_spec(spec, argv[i], dbis)) {
	    if (geom_pos == INT_MAX) {
		geom_pos = i;
		gs = spec.hashes;
	    }
	    break;
	}
    }

    /* With no geometry or command found yet — all remaining tokens are
     * candidates for options.  Parse them all: if -h is among them, print
     * help and return OK; otherwise report the first token as invalid. */
    if (geom_pos == INT_MAX && cmd_pos == INT_MAX) {
	if (maybe_opts) {
	    struct bu_vls opterrs = BU_VLS_INIT_ZERO;
	    bu_opt_parse(&opterrs, argc, argv, d);
	    bu_vls_free(&opterrs);
	    if (help) {
		_ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
		    "edit", bargs_help, 0, NULL);
		return BRLCAD_OK;
	    }
	    _ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
		"edit", bargs_help, 0, NULL);
	} else {
	    bu_vls_printf(gedp->ged_result_str,
		"Invalid geometry specifier: %s\n", argv[0]);
	}
	return BRLCAD_ERROR;
    }

    /* The option prefix is everything before the first geom or cmd token */
    int opt_prefix_len = (geom_pos < cmd_pos) ? geom_pos : cmd_pos;

    /* Parse global options from the prefix */
    if (opt_prefix_len > 0) {
	struct bu_vls opterrs = BU_VLS_INIT_ZERO;
	int opt_ret = bu_opt_parse(&opterrs, opt_prefix_len, argv, d);
	if (opt_ret < 0) {
	    bu_vls_printf(gedp->ged_result_str, "%s", bu_vls_cstr(&opterrs));
	    _ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
		"edit", bargs_help, 0, NULL);
	    bu_vls_free(&opterrs);
	    return BRLCAD_ERROR;
	}
	bu_vls_free(&opterrs);

	/* Shift remaining tokens to front */
	int remaining = argc - opt_prefix_len;
	for (int i = 0; i < remaining; i++)
	    argv[i] = argv[opt_prefix_len + i];
	argc -= opt_prefix_len;

	/* Adjust positions after shift */
	if (geom_pos != INT_MAX) geom_pos -= opt_prefix_len;
	if (cmd_pos  != INT_MAX) cmd_pos  -= opt_prefix_len;
    }

    /* Handle -h after option processing */
    if (help) {
	const char *cmd_name_for_help = (cmd_pos != INT_MAX) ? argv[cmd_pos] : "edit";
	_ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
	    cmd_name_for_help, bargs_help, 0, NULL);
	return BRLCAD_OK;
    }

    /* Sanity: geometry must come before command if both present */
    if (geom_pos != INT_MAX && cmd_pos != INT_MAX &&
	    (geom_pos > cmd_pos || cmd_pos != geom_pos + 1)) {
	_ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
	    "edit", bargs_help, 0, NULL);
	return BRLCAD_ERROR;
    }

    /* ---- Pass 2: collect geometry specifiers ----------------------- */

    /* Re-resolve the geometry specifier(s) into ged_edit_geom_spec entries.
     * The current implementation collects a single specifier (the one at
     * geom_pos).  Multi-object editing (dot batch, multiple paths) is
     * supported structurally but dispatched one-at-a-time. */
    const char *geom_str = NULL;
    if (geom_pos != INT_MAX) {
	geom_str = argv[geom_pos];
	ged_edit_geom_spec spec;
	if (_resolve_geom_spec(spec, geom_str, dbis)) {
	    ctx.geom_specs.push_back(spec);
	}
    }

    /* Selection fallback: if no explicit specifier was given, use the
     * active "default" selection state. */
    if (ctx.geom_specs.empty() && !ctx.flag_F) {
	std::vector<BSelectState *> ss = dbis->get_selected_states("default");
	if (!ss.empty()) {
	    std::vector<std::string> sel_paths = ss[0]->list_selected_paths();
	    for (const std::string &spath : sel_paths) {
		ged_edit_geom_spec spec;
		if (_resolve_geom_spec(spec, spath.c_str(), dbis))
		    ctx.geom_specs.push_back(spec);
	    }
	    if (!ctx.geom_specs.empty())
		ctx.from_selection = true;
	}
    }

    /* Selection conflict arbiter: explicit specifier present AND the same
     * object is also in the active selection → require an arbiter flag. */
    if (!ctx.geom_specs.empty() && !ctx.from_selection && !ctx.flag_S &&
	    !ctx.flag_f && !ctx.flag_F && !ctx.flag_i) {
	std::vector<BSelectState *> ss = dbis->get_selected_states("default");
	if (!ss.empty()) {
	    std::vector<std::string> sel_paths = ss[0]->list_selected_paths();
	    for (const auto &gspec : ctx.geom_specs) {
		for (const std::string &spath : sel_paths) {
		    if (gspec.path == spath) {
			ctx.has_conflict = true;
			bu_vls_printf(gedp->ged_result_str,
			    "Conflict: \"%s\" has both an explicit command-line "
			    "specifier and an active selection.\n"
			    "Use -S to operate on the selection, -f to force "
			    "a disk write, -F to abandon the intermediate "
			    "state, or -i to edit the temp buffer.\n",
			    gspec.path.c_str());
			return BRLCAD_ERROR;
		    }
		}
	    }
	}
    }

    /* If -S flag is set, discard explicit specifiers and use selection */
    if (ctx.flag_S) {
	ctx.geom_specs.clear();
	std::vector<BSelectState *> ss = dbis->get_selected_states("default");
	if (!ss.empty()) {
	    std::vector<std::string> sel_paths = ss[0]->list_selected_paths();
	    for (const std::string &spath : sel_paths) {
		ged_edit_geom_spec spec;
		if (_resolve_geom_spec(spec, spath.c_str(), dbis))
		    ctx.geom_specs.push_back(spec);
	    }
	    ctx.from_selection = true;
	}
    }

    /* Set convenience dp from first resolved specifier */
    if (!ctx.geom_specs.empty()) {
	ctx.dp = ctx.geom_specs[0].dp;
    }

    /* Advance past the geometry token */
    if (geom_str) {
	argc--; argv++;
    }

    /* ---- Pass 3: subcommand dispatch ------------------------------- */

    if (!argc || !argv[0]) {
	if (!ctx.geom_specs.empty() || ctx.from_selection) {
	    /* Object specified but no subcommand */
	    bu_vls_printf(gedp->ged_result_str,
		"No subcommand specified for \"%s\"\n",
		geom_str ? geom_str : "(selection)");
	} else {
	    _ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
		"edit", bargs_help, 0, NULL);
	}
	return BRLCAD_ERROR;
    }

    std::string cmd_str(argv[0]);
    auto e_it = edit_cmds.find(cmd_str);
    if (e_it == edit_cmds.end()) {
	bu_vls_printf(gedp->ged_result_str,
	    "Unknown subcommand: %s\n", argv[0]);
	_ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
	    "edit", bargs_help, 0, NULL);
	return BRLCAD_ERROR;
    }

    /* Must have a geometry specifier before dispatching */
    if (ctx.geom_specs.empty()) {
	bu_vls_printf(gedp->ged_result_str,
	    "No valid geometry specifier found; nothing to edit.\n");
	_ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
	    "edit", bargs_help, 0, NULL);
	return BRLCAD_ERROR;
    }

    return e_it->second->exec(gedp, &ctx, argc, argv);
}



// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8


