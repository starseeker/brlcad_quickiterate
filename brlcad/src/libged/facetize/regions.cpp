/*                     R E G I O N S . C P P
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
/** @file libged/facetize.cpp
 *
 * Logic implementing the per-region facetize mode.
 *
 * Note:  we deliberately manage this somewhat differently from the "convert
 * everything to one BoT" case to minimize the number of subprocesses we have
 * to launch.  For very large models, if we just treat each region like its a
 * complete conversion, we may end up launching too many subprocesses and run
 * into operating system limitations.
 */

#include "common.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

#include <string.h>

#include "bu/app.h"
#include "bu/path.h"
#include "bu/env.h"
#include "bu/time.h"
#include "bg/trimesh.h"
#include "rt/db_io.h"
#include "rt/search.h"
#include "raytrace.h"
#include "wdb.h"
#include "../ged_private.h"
#include "./ged_facetize.h"

static const double FACETIZE_RT_EMPTY_TOL = 1.0e-9;
static const double FACETIZE_USEC_TO_SEC_DIVISOR = 1.0e6;

/* Minimum Crofton crossing count for a statistically meaningful SA
 * comparison.  Below this threshold (~1/sqrt(N) noise > 14 %) the
 * estimate is too noisy to distinguish a real mismatch from sampling
 * variance; such results are accepted with a note rather than triggering
 * a perturb retry that would face the same sampling limitation.         */
static const long CROFTON_FEW_HIT_THRESHOLD = 50;

static void
_collect_tree_leaves(union tree *tp, std::set<std::string> &leaves)
{
    if (!tp) return;
    switch (tp->tr_op) {
	case OP_UNION:
	case OP_INTERSECT:
	case OP_SUBTRACT:
	case OP_XOR:
	    _collect_tree_leaves(tp->tr_b.tb_right, leaves);
	    /* fall through */
	case OP_NOT:
	case OP_GUARD:
	case OP_XNOP:
	    _collect_tree_leaves(tp->tr_b.tb_left, leaves);
	    break;
	case OP_DB_LEAF:
	    leaves.insert(std::string(tp->tr_l.tl_name));
	    break;
	default:
	    break;
    }
}

static bool
_has_perturbable_leaf(struct db_i *dbip, struct directory *dp, std::set<std::string> &visited)
{
    if (!dbip || !dp) return false;
    if (visited.find(dp->d_namep) != visited.end()) return false;
    visited.insert(std::string(dp->d_namep));

    if (!(dp->d_flags & RT_DIR_COMB))
	return (OBJ[dp->d_minor_type].ft_perturb != NULL);

    struct rt_db_internal in;
    RT_DB_INTERNAL_INIT(&in);
    if (rt_db_get_internal(&in, dp, dbip, NULL) < 0)
	return false;

    struct rt_comb_internal *comb = (struct rt_comb_internal *)in.idb_ptr;
    std::set<std::string> leaves;
    if (comb->tree)
	_collect_tree_leaves(comb->tree, leaves);
    rt_db_free_internal(&in);

    for (const auto &lname : leaves) {
	struct directory *ldp = db_lookup(dbip, lname.c_str(), LOOKUP_QUIET);
	if (_has_perturbable_leaf(dbip, ldp, visited))
	    return true;
    }

    return false;
}

static long
_crofton_on_obj(struct db_i *dbip, const char *obj_name, double &out_sa, double &out_vol)
{
    out_sa = out_vol = -1.0;

    struct rt_i *rtip = rt_new_rti(dbip);
    if (!rtip) return -1L;

    if (rt_gettree(rtip, obj_name) != 0) {
	rt_free_rti(rtip);
	return -1L;
    }
    rt_prep_parallel(rtip, 1);

    struct rt_crofton_params crp;
    crp.n_rays = 0;
    crp.stability_mm = 0.05;
    crp.time_ms = 2000.0;

    int cr = rt_crofton_shoot(rtip, &crp, &out_sa, &out_vol);
    rt_free_rti(rtip);
    return (cr >= 0) ? (long)cr : -1L;
}

static int
_bot_metrics(struct db_i *dbip, const char *bot_name, double &out_sa, double &out_vol)
{
    out_sa = out_vol = -1.0;
    struct directory *dp = db_lookup(dbip, bot_name, LOOKUP_QUIET);
    if (!dp || (dp->d_flags & RT_DIR_COMB))
	return -1;

    struct rt_db_internal in;
    RT_DB_INTERNAL_INIT(&in);
    if (rt_db_get_internal(&in, dp, dbip, NULL) < 0)
	return -1;

    int ret = -1;
    if (in.idb_minor_type == DB5_MINORTYPE_BRLCAD_BOT) {
	struct rt_bot_internal *bot = (struct rt_bot_internal *)in.idb_ptr;
	if (bot->mode == RT_BOT_SOLID) {
	    if (bot->num_faces > 0 && bot->num_vertices > 0) {
		out_sa = bg_trimesh_area(bot->faces, bot->num_faces,
			(const point_t *)bot->vertices, bot->num_vertices);
		out_vol = bg_trimesh_volume(bot->faces, bot->num_faces,
			(const point_t *)bot->vertices, bot->num_vertices);
	    } else {
		out_sa = 0.0;
		out_vol = 0.0;
	    }
	    ret = 0;
	}
    }
    rt_db_free_internal(&in);
    return ret;
}

/**
 * Delete any existing object named @p bot_name from @p dbip and write a
 * new zero-face BoT in its place.  Used when Crofton detects that the
 * Boolean evaluation of a region is almost certainly empty (zero ray
 * intersections) so the facetize result should be empty too.
 */
static void
_write_empty_bot(struct db_i *dbip, const char *bot_name, int verbosity)
{
    struct directory *od = db_lookup(dbip, bot_name, LOOKUP_QUIET);
    if (od != RT_DIR_NULL) {
	db_delete(dbip, od);
	db_dirdelete(dbip, od);
    }
    struct rt_bot_internal *ebot;
    BU_GET(ebot, struct rt_bot_internal);
    ebot->magic        = RT_BOT_INTERNAL_MAGIC;
    ebot->mode         = RT_BOT_SOLID;
    ebot->orientation  = RT_BOT_CCW;
    ebot->thickness    = NULL;
    ebot->face_mode    = (struct bu_bitv *)NULL;
    ebot->bot_flags    = 0;
    ebot->num_vertices = 0;
    ebot->num_faces    = 0;
    ebot->vertices     = NULL;
    ebot->faces        = NULL;
    (void)_ged_facetize_write_bot(dbip, ebot, bot_name, verbosity);
}

static void
_clear_variant_plan(struct _ged_facetize_state *s)
{
    delete (FacetizeVariantPlan *)s->variant_plan;
    s->variant_plan = NULL;
}

/* returns 1 on pass, 0 on mismatch, -1 on unavailable/skip */
static int
_validate_csg_vs_bot(struct db_i *csg_dbip, const char *obj_name, struct db_i *bot_dbip, const char *bot_name, double sa_tol_pct, double vol_tol_pct, double *sa_err_pct, double *vol_err_pct);

/* -----------------------------------------------------------------------
 * Perturbed-CSG in-memory db helpers for Pass 2 Crofton validation.
 *
 * Build a fresh in-memory database containing:
 *   - every CSG solid leaf from the original s->dbip region, with variant
 *     leaves replaced by ft_perturb-generated parametric copies (reusing
 *     the exact same perturbation factor recorded in vplan->variant_recs),
 *   - a region comb whose tree mirrors the original but with variant leaf
 *     names substituted.
 *
 * Crofton then raytraces against true parametric CSG geometry for the Pass 2
 * reference, rather than against BoTs or the unperturbed original.
 * ----------------------------------------------------------------------- */

struct PerturCsgCtx {
    struct db_i               *src_dbip;     /* original .g — read-only source */
    struct rt_wdb             *inmem_wdbp;   /* destination in-memory db */
    const FacetizeVariantPlan *vplan;
    std::vector<std::string>   path_stack;
    std::set<std::string>      written;      /* names already written to inmem */
};

/* Forward declarations (mutually recursive). */
static union tree  *_pcsg_copy_tree(PerturCsgCtx &ctx, union tree *tp, bool in_sub);
static bool         _pcsg_make_comb(PerturCsgCtx &ctx, const char *comb_name, bool in_sub);

static union tree *
_pcsg_copy_tree(PerturCsgCtx &ctx, union tree *tp, bool in_sub)
{
    if (!tp) return NULL;

    union tree *nt;
    BU_ALLOC(nt, union tree);
    RT_TREE_INIT(nt);
    nt->tr_op = tp->tr_op;

    switch (tp->tr_op) {
	case OP_DB_LEAF: {
			     const char *leaf = tp->tr_l.tl_name;
			     struct directory *ldp = db_lookup(ctx.src_dbip, leaf, LOOKUP_QUIET);
			     std::string use_name = leaf;

			     if (ldp && (ldp->d_flags & RT_DIR_COMB)) {
				 /* Intermediate comb: recurse, writing it into the inmem db. */
				 _pcsg_make_comb(ctx, leaf, in_sub);
				 /* Use the same name in the inmem db. */
			     } else {
				 /* Solid leaf: check for a variant. */
				 std::string path_key;
				 for (const auto &seg : ctx.path_stack)
				     path_key += "/" + seg;
				 path_key += "/" + std::string(leaf);
				 std::string role_key = path_key + (in_sub ? "#sub" : "#base");
				 auto it = ctx.vplan->inst_to_variant.find(role_key);

				 if (it != ctx.vplan->inst_to_variant.end()) {
				     /* Variant exists: recreate the perturbed CSG from src_dbip. */
				     const std::string &vname = it->second;
				     use_name = vname;
				     if (ctx.written.find(vname) == ctx.written.end()) {
					 auto rec_it = ctx.vplan->variant_recs.find(vname);
					 if (rec_it != ctx.vplan->variant_recs.end() && ldp) {
					     struct rt_db_internal src_intern;
					     RT_DB_INTERNAL_INIT(&src_intern);
					     if (rt_db_get_internal(&src_intern, ldp, ctx.src_dbip,
							 NULL) >= 0) {
						 int ptype = src_intern.idb_type;
						 struct rt_db_internal *var_intern = NULL;
						 bool ok = false;
						 if (OBJ[ptype].ft_perturb &&
							 OBJ[ptype].ft_perturb(&var_intern, &src_intern, 0,
							     rec_it->second.factor) == BRLCAD_OK &&
							 var_intern) {
						     if (wdb_put_internal(ctx.inmem_wdbp, vname.c_str(),
								 var_intern, 1.0) >= 0)
							 ok = true;
						     /* wdb_put_internal frees var_intern's idb_ptr;
						      * we still need to free the struct itself. */
						     BU_PUT(var_intern, struct rt_db_internal);
						 }
						 if (!ok) {
						     /* Fallback: write original CSG under variant name. */
						     if (wdb_put_internal(ctx.inmem_wdbp, vname.c_str(),
								 &src_intern, 1.0) >= 0)
							 ok = true;
						     /* src_intern freed by wdb_put_internal */
						 } else {
						     rt_db_free_internal(&src_intern);
						 }
						 if (ok) ctx.written.insert(vname);
					     }
					 } else {
					     /* No variant record — fall back to original name. */
					     use_name = leaf;
					 }
				     }
				 }

				 /* Ensure the (possibly original) leaf exists in the inmem db. */
				 if (ctx.written.find(use_name) == ctx.written.end()) {
				     struct directory *udp =
					 db_lookup(ctx.src_dbip, use_name.c_str(), LOOKUP_QUIET);
				     if (udp) {
					 struct rt_db_internal leaf_intern;
					 RT_DB_INTERNAL_INIT(&leaf_intern);
					 if (rt_db_get_internal(&leaf_intern, udp, ctx.src_dbip,
						     NULL) >= 0) {
					     if (wdb_put_internal(ctx.inmem_wdbp, use_name.c_str(),
							 &leaf_intern, 1.0) >= 0)
						 ctx.written.insert(use_name);
					     /* leaf_intern freed by wdb_put_internal */
					 }
				     }
				 }
			     }

			     nt->tr_l.tl_name = bu_strdup(use_name.c_str());
			     if (tp->tr_l.tl_mat) {
				 nt->tr_l.tl_mat = (matp_t)bu_malloc(sizeof(mat_t), "tl_mat cp");
				 MAT_COPY(nt->tr_l.tl_mat, tp->tr_l.tl_mat);
			     }
			     break;
			 }
	case OP_UNION:
	case OP_INTERSECT:
			 nt->tr_b.tb_left  = _pcsg_copy_tree(ctx, tp->tr_b.tb_left,  in_sub);
			 nt->tr_b.tb_right = _pcsg_copy_tree(ctx, tp->tr_b.tb_right, in_sub);
			 break;
	case OP_SUBTRACT:
			 /* Right child of subtraction is always subtractive, regardless of
			  * the outer context, so pass in_sub=true for the right branch. */
			 nt->tr_b.tb_left  = _pcsg_copy_tree(ctx, tp->tr_b.tb_left,  in_sub);
			 nt->tr_b.tb_right = _pcsg_copy_tree(ctx, tp->tr_b.tb_right, true);
			 break;
	case OP_NOT:
	case OP_GUARD:
	case OP_XNOP:
			 nt->tr_b.tb_left = _pcsg_copy_tree(ctx, tp->tr_b.tb_left, in_sub);
			 break;
	default:
			 break;
    }
    return nt;
}

static bool
_pcsg_make_comb(PerturCsgCtx &ctx, const char *comb_name, bool in_sub)
{
    if (ctx.written.find(std::string(comb_name)) != ctx.written.end())
	return true;  /* already written */

    struct directory *dp = db_lookup(ctx.src_dbip, comb_name, LOOKUP_QUIET);
    if (!dp) return false;

    struct rt_db_internal intern;
    RT_DB_INTERNAL_INIT(&intern);
    if (rt_db_get_internal(&intern, dp, ctx.src_dbip, NULL) < 0)
	return false;

    struct rt_comb_internal *orig = (struct rt_comb_internal *)intern.idb_ptr;

    ctx.path_stack.push_back(std::string(comb_name));
    union tree *new_tree = _pcsg_copy_tree(ctx, orig->tree, in_sub);
    ctx.path_stack.pop_back();

    rt_db_free_internal(&intern);

    struct rt_comb_internal *new_comb;
    BU_ALLOC(new_comb, struct rt_comb_internal);
    RT_COMB_INTERNAL_INIT(new_comb);
    new_comb->tree = new_tree;

    struct rt_db_internal new_intern;
    RT_DB_INTERNAL_INIT(&new_intern);
    new_intern.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    new_intern.idb_type       = ID_COMBINATION;
    new_intern.idb_ptr        = (void *)new_comb;
    new_intern.idb_meth       = &OBJ[ID_COMBINATION];

    bool ok = (wdb_put_internal(ctx.inmem_wdbp, comb_name,
		&new_intern, 1.0) >= 0);
    /* wdb_put_internal frees new_intern (including new_comb + new_tree). */
    if (ok) ctx.written.insert(std::string(comb_name));
    return ok;
}

/**
 * Build a fresh in-memory database containing a perturbed-CSG copy of
 * @a region_name from @a src_dbip.  Solid leaves that have a variant record
 * in @a vplan are replaced by ft_perturb-generated parametric copies using
 * the exact factor stored in vplan->variant_recs; all other leaves are
 * copied verbatim from @a src_dbip.
 *
 * The region comb is written under its original name, so callers pass that
 * name directly to _validate_csg_vs_bot().
 *
 * Returns an allocated struct db_i * on success (caller must db_close() it),
 * or NULL on failure.
 */
static struct db_i *
_create_perturbed_csg_db(struct db_i *src_dbip, const char *region_name,
	const FacetizeVariantPlan *vplan)
{
    if (!src_dbip || !region_name || !vplan) return NULL;

    struct db_i *inmem_dbip = db_create_inmem();
    if (!inmem_dbip) return NULL;

    struct rt_wdb *inmem_wdbp = wdb_dbopen(inmem_dbip, RT_WDB_TYPE_DB_INMEM);
    if (!inmem_wdbp) { db_close(inmem_dbip); return NULL; }

    PerturCsgCtx ctx;
    ctx.src_dbip    = src_dbip;
    ctx.inmem_wdbp  = inmem_wdbp;
    ctx.vplan       = vplan;

    if (!_pcsg_make_comb(ctx, region_name, false)) {
	db_close(inmem_dbip);
	return NULL;
    }
    return inmem_dbip;
}

static int
_validate_csg_vs_bot(struct db_i *csg_dbip, const char *obj_name, struct db_i *bot_dbip, const char *bot_name, double sa_tol_pct, double vol_tol_pct, double *sa_err_pct, double *vol_err_pct)
{
    /* Return codes:
     *   1  PASS:      SA and volume within tolerance.
     *   0  MISMATCH:  outside tolerance — trigger perturb retry.
     *  -1  ERROR:     Crofton prep or BoT metric failure.
     *   2  FEW_HIT:   sampler found 1..CROFTON_FEW_HIT_THRESHOLD-1 crossings —
     *                 too few for a meaningful comparison; accept with note.
     *   3  ZERO_HIT:  sampler found zero crossings for a non-empty BoT —
     *                 the BoT output is suspect; warn user to inspect.       */
    double csa = -1.0, cvol = -1.0, bsa = -1.0, bvol = -1.0;
    long csg_crossings = _crofton_on_obj(csg_dbip, obj_name, csa, cvol);
    if (csg_crossings < 0)
	return -1;
    if (_bot_metrics(bot_dbip, bot_name, bsa, bvol) != 0)
	return -1;

    /* --- Empty-BoT case: pass iff the CSG also read as empty ---------- */
    if (std::fabs(bsa) <= FACETIZE_RT_EMPTY_TOL && std::fabs(bvol) <= FACETIZE_RT_EMPTY_TOL) {
	bool csg_empty = (csg_crossings == 0);
	*sa_err_pct  = csg_empty ? 0.0 : 100.0;
	*vol_err_pct = csg_empty ? 0.0 : 100.0;
	return csg_empty ? 1 : 0;
    }

    /* --- Non-empty BoT: examine Crofton hit count --------------------- */

    /* Zero crossings: the sampler fired rays but hit nothing.  The BoT is
     * non-empty, which is suspicious — either the CSG has genuinely been
     * reduced to nothing (e.g. a subtractor fully engulfs the base) and
     * the BoT contains leftover noise triangles, or the geometry is so
     * extreme that not a single stochastic chord hit it.  Either outcome
     * warrants user inspection rather than silent acceptance.            */
    if (csg_crossings == 0) {
	*sa_err_pct  = 100.0;
	*vol_err_pct = 100.0;
	return 3;
    }

    /* Few crossings: the sampler did find geometry but the count is below
     * the threshold at which the SA estimate is statistically reliable
     * (~1/sqrt(N) noise > 14 %).  A perturb retry would face the same
     * sampling limitation, so we accept the BoT with a note instead.    */
    if (csg_crossings < CROFTON_FEW_HIT_THRESHOLD) {
	*sa_err_pct  = 100.0;
	*vol_err_pct = 100.0;
	return 2;
    }

    /* Normal case: enough crossings for a reliable SA/volume estimate.   */
    double sa_err  = (bsa > 0.0) ? std::fabs(csa - bsa) / bsa : 1.0;
    double vol_err = (bvol > 0.0) ? std::fabs(cvol - bvol) / bvol : 1.0;
    *sa_err_pct  = sa_err  * 100.0;
    *vol_err_pct = vol_err * 100.0;
    return (sa_err > sa_tol_pct || vol_err > vol_tol_pct) ? 0 : 1;
}


struct ged_region_ctx {
    struct _ged_facetize_state *s;
    struct bu_list *vlfree;
    int64_t region_start;
    double perturb_sa_frac;
    double perturb_vol_frac;
    int vcnt_skip;
    int vcnt_total;
    int vcnt_naturally_empty;
    int vcnt_p1_pass;
    int vcnt_few_hit;
    int vcnt_zero_hit;
    int vcnt_p1_trigger;
    int vcnt_p2_pass;
    int vcnt_p2_topoflip;
    int vcnt_p2_warn;
    int vcnt_unavail;
    int vcnt_adjusted_instances;
    int vcnt_sub_variants;
    int vcnt_perturb_fallbacks;
    int vcnt_tess_failures;
    std::set<std::string> inspect_regions;
};

static int
_ged_regions_validate_args_cb(void *ctx, int UNUSED(argc), const char **UNUSED(argv), int newobjcnt)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    struct _ged_facetize_state *s = rctx ? rctx->s : NULL;
    if (!s)
	return BRLCAD_ERROR;
    if (newobjcnt != 1) {
	if (!newobjcnt)
	    bu_vls_printf(s->gedp->ged_result_str, "Need non-existent output comb name.");
	if (newobjcnt)
	    bu_vls_printf(s->gedp->ged_result_str, "More than one non-existent object specified in region processing mode, aborting.");
	return BRLCAD_ERROR;
    }
    return BRLCAD_OK;
}

static int
_ged_regions_object_fallback_cb(void *ctx, int argc, const char **argv)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    return _ged_facetize_objs(rctx->s, argc, argv);
}

static int
_ged_regions_set_working_file_cb(void *ctx, const char *working_file)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    if (!rctx || !rctx->s || !working_file)
	return BRLCAD_ERROR;
    bu_vls_sprintf(rctx->s->wfile, "%s", working_file);
    return BRLCAD_OK;
}

static int
_ged_regions_working_file_setup_cb(void *ctx, struct bu_ptbl *leaf_dps)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    return _ged_facetize_working_file_setup(rctx->s, leaf_dps);
}

static int
_ged_regions_primitive_tessellate_cb(void *ctx, struct db_i *dbip, struct bu_ptbl *leaf_dps)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    return _ged_facetize_leaves_tri(rctx->s, dbip, leaf_dps);
}

static void
_ged_regions_state_for_working_db(struct _ged_facetize_state *dst, struct _ged_facetize_state *src, struct db_i *wdbip)
{
    *dst = *src;
    dst->dbip = wdbip;
}

static int
_ged_regions_nmg_eval_cb(void *ctx, struct db_i *working_db, const char *root_name, const char *result_name)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    struct _ged_facetize_state nmg_wstate;
    _ged_regions_state_for_working_db(&nmg_wstate, rctx->s, working_db);
    const char *obj_name = root_name;
    return _ged_facetize_nmgeval(&nmg_wstate, 1, &obj_name, result_name);
}

static int
_ged_regions_manifold_eval_cb(void *ctx, struct db_i *working_db, struct rt_wdb *wdbp, const char *root_name, const char *result_name, size_t current, size_t total)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    char *obj_name = bu_strdup(root_name);
    int ret = _ged_facetize_booleval_tri(rctx->s, working_db, wdbp, 1, (const char **)&obj_name, result_name, rctx->vlfree, 1, (int)current, (int)total);
    bu_free(obj_name, "obj_name");
    return ret;
}

static int
_ged_regions_validate_region_cb(void *ctx, const char *root_name, const char *result_name, struct db_i **working_db, struct rt_wdb **wdbp, size_t current, size_t UNUSED(total), int *eval_status)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    struct _ged_facetize_state *s = rctx->s;
    struct db_i *wdbip = *working_db;
    int bret = *eval_status;

    if (bret == BRLCAD_OK) {
	struct directory *ebot_dp = db_lookup(wdbip, result_name, LOOKUP_QUIET);
	if (ebot_dp != RT_DIR_NULL) {
	    struct rt_db_internal einternal;
	    RT_DB_INTERNAL_INIT(&einternal);
	    if (rt_db_get_internal(&einternal, ebot_dp, wdbip, NULL) >= 0) {
		if (einternal.idb_minor_type == DB5_MINORTYPE_BRLCAD_BOT) {
		    struct rt_bot_internal *ebot = (struct rt_bot_internal *)einternal.idb_ptr;
		    if (ebot->num_faces == 0)
			rctx->vcnt_naturally_empty++;
		}
		rt_db_free_internal(&einternal);
	    }
	}
    }

    bool can_validate = false;
    if (!s->no_perturb) {
	std::set<std::string> visited;
	struct directory *rdp = db_lookup(s->dbip, root_name, LOOKUP_QUIET);
	can_validate = _has_perturbable_leaf(s->dbip, rdp, visited);
    }
    if (!s->no_perturb && !can_validate) {
	rctx->vcnt_skip++;
	if (s->verbosity > 0)
	    bu_log("FACETIZE: %s has no ft_perturb-capable leaves; skipping raytrace validation\n", root_name);
    }

    if (bret == BRLCAD_OK && !s->no_perturb && can_validate) {
	rctx->vcnt_total++;
	double sa_err_pct = -1.0, vol_err_pct = -1.0;
	int vret = _validate_csg_vs_bot(s->dbip, root_name, wdbip, result_name,
		rctx->perturb_sa_frac, rctx->perturb_vol_frac,
		&sa_err_pct, &vol_err_pct);
	if (vret == 1) {
	    rctx->vcnt_p1_pass++;
	    facetize_log(s, 1, "FACETIZE: %s CSG vs BoT MATCH (SA_err=%.2f%% VOL_err=%.2f%%) - skipping perturb\n",
		    root_name, sa_err_pct, vol_err_pct);
	}
	if (vret == 2) {
	    rctx->vcnt_few_hit++;
	    rctx->inspect_regions.insert(std::string(root_name) + " (few ray hits)");
	    facetize_log(s, 1, "FACETIZE NOTE: %s Crofton found very few ray intersections with CSG geometry (sub-mm or near-degenerate); BoT accepted - verify modeling intent\n",
		    root_name);
	}
	if (vret == 3) {
	    rctx->vcnt_zero_hit++;
	    rctx->inspect_regions.insert(std::string(root_name) + " (zero CSG ray hits)");
	    facetize_log(s, 1, "FACETIZE: %s Crofton found zero ray intersections with CSG geometry; Boolean eval likely empty - replacing BoT with empty\n",
		    root_name);
	    if (!s->no_empty)
		_write_empty_bot(wdbip, result_name, s->verbosity);
	}
	if (vret == 0) {
	    rctx->vcnt_p1_trigger++;
	    facetize_log(s, 1, "FACETIZE: %s CSG vs BoT MISMATCH (SA_err=%.2f%% VOL_err=%.2f%%) - triggering perturb\n",
		    root_name, sa_err_pct, vol_err_pct);
	    bool reopened_wdb = false;
	    _clear_variant_plan(s);
	    struct directory *rdp = db_lookup(s->dbip, root_name, LOOKUP_QUIET);
	    struct directory *dpw[2] = {rdp, NULL};
	    FacetizeVariantPlan *region_vplan = _ged_facetize_build_variant_plan(s, 1, dpw);
	    if (region_vplan) {
		s->variant_plan = (void *)region_vplan;
		rctx->vcnt_adjusted_instances += region_vplan->n_adjusted_instances;
		rctx->vcnt_sub_variants += region_vplan->n_sub_variants;
		rctx->vcnt_perturb_fallbacks += region_vplan->n_perturb_fallbacks;
		if (!region_vplan->variant_names.empty())
		    _ged_facetize_tessellate_variant_names(s, region_vplan);
		rctx->vcnt_tess_failures += region_vplan->n_variant_tess_failures;
		reopened_wdb = true;
	    }
	    if (region_vplan) {
		if (reopened_wdb) {
		    db_close(wdbip);
		    wdbip = db_open(bu_vls_cstr(s->wfile), DB_OPEN_READWRITE);
		    if (!wdbip) {
			*eval_status = BRLCAD_ERROR;
			return BRLCAD_ERROR;
		    }
		    db_dirbuild(wdbip);
		    db_update_nref(wdbip);
		    *wdbp = wdb_dbopen(wdbip, RT_WDB_TYPE_DB_DEFAULT);
		    *working_db = wdbip;
		}

		s->use_variant_plan = 1;
		struct directory *od = db_lookup(wdbip, result_name, LOOKUP_QUIET);
		if (od != RT_DIR_NULL) {
		    db_delete(wdbip, od);
		    db_dirdelete(wdbip, od);
		}
		char *obj_name_retry = bu_strdup(root_name);
		bret = _ged_facetize_booleval_tri(s, wdbip, *wdbp, 1, (const char **)&obj_name_retry, result_name, rctx->vlfree, 1, (int)current, -1);
		bu_free(obj_name_retry, "obj_name_retry");
		s->use_variant_plan = 0;

		if (bret == BRLCAD_OK) {
		    double sa_err2 = -1.0, vol_err2 = -1.0;
		    struct db_i *perturb_dbip = _create_perturbed_csg_db(s->dbip, root_name, region_vplan);
		    struct db_i *csg_ref_dbip = perturb_dbip ? perturb_dbip : s->dbip;
		    int vret2 = _validate_csg_vs_bot(csg_ref_dbip, root_name, wdbip, result_name,
			    rctx->perturb_sa_frac, rctx->perturb_vol_frac,
			    &sa_err2, &vol_err2);
		    if (perturb_dbip) db_close(perturb_dbip);
		    if (vret2 == 1) {
			rctx->vcnt_p2_pass++;
			facetize_log(s, 1, "FACETIZE: %s perturbed CSG vs BoT MATCH (SA_err=%.2f%% VOL_err=%.2f%%) - perturb successful\n",
				root_name, sa_err2, vol_err2);
		    }
		    if (vret2 == 2) {
			rctx->vcnt_p2_topoflip++;
			rctx->inspect_regions.insert(std::string(root_name) + " (few perturbed CSG crossings)");
			facetize_log(s, 1, "FACETIZE NOTE: %s Crofton found very few crossings for perturbed CSG; perturb may have shifted geometry - check output\n",
				root_name);
		    }
		    if (vret2 == 3) {
			rctx->vcnt_zero_hit++;
			rctx->inspect_regions.insert(std::string(root_name) + " (zero perturbed CSG crossings)");
			facetize_log(s, 1, "FACETIZE: %s Crofton found zero crossings for perturbed CSG; Boolean eval likely empty after perturb - replacing BoT with empty\n",
				root_name);
			if (!s->no_empty)
			    _write_empty_bot(wdbip, result_name, s->verbosity);
		    }
		    if (vret2 == 0) {
			rctx->vcnt_p2_warn++;
			rctx->inspect_regions.insert(std::string(root_name) + " (persistent mismatch after perturb)");
			facetize_log(s, 1, "FACETIZE WARNING: %s persistent validation mismatch after perturb retry (SA_err=%.2f%% VOL_err=%.2f%%) - check output geometry with 'lint'\n",
				root_name, sa_err2, vol_err2);
		    }
		    if (vret2 < 0) {
			rctx->vcnt_unavail++;
			if (s->verbosity > 0)
			    bu_log("FACETIZE: validation unavailable after perturb retry for %s\n", root_name);
		    }
		}
	    } else {
		rctx->vcnt_unavail++;
		rctx->inspect_regions.insert(std::string(root_name) + " (perturb plan unavailable)");
		if (s->verbosity > 0)
		    bu_log("FACETIZE: perturb plan unavailable for %s\n", root_name);
	    }
	    _clear_variant_plan(s);
	}
	if (vret < 0) {
	    rctx->vcnt_unavail++;
	    rctx->inspect_regions.insert(std::string(root_name) + " (validation unavailable)");
	    if (s->verbosity > 0)
		bu_log("FACETIZE: validation unavailable for %s (crofton/metric prep failure)\n", root_name);
	}
    }

    *eval_status = bret;
    return BRLCAD_OK;
}

static void
_ged_regions_use_variant_plan_cb(void *ctx, int enabled)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    rctx->s->use_variant_plan = enabled;
}

static void
_ged_regions_primitive_summary_cb(void *ctx)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    facetize_primitives_summary(rctx->s);
}

static void
_ged_regions_region_summary_cb(void *ctx, size_t eval_total)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    struct _ged_facetize_state *s = rctx->s;
    if ((rctx->vcnt_total > 0 || rctx->vcnt_skip > 0) && !s->make_nmg && !s->nmg_booleval) {
	double elapsed_s = (bu_gettime() - rctx->region_start) / FACETIZE_USEC_TO_SEC_DIVISOR;
	facetize_log(s, 0, "\nFACETIZE summary:\n");
	facetize_log(s, 0, "  %-45s %8zu\n", "Total roots evaluated", eval_total);
	facetize_log(s, 0, "  %-45s %8.2f\n", "Runtime (sec)", elapsed_s);
	facetize_log(s, 0, "  %-45s %8d\n", "Validation skipped (no perturbable leaves)", rctx->vcnt_skip);
	facetize_log(s, 0, "  %-45s %8d\n", "Validation pass (P1)", rctx->vcnt_p1_pass);
	facetize_log(s, 0, "  %-45s %8d\n", "Naturally empty BoTs (Boolean eval)", rctx->vcnt_naturally_empty);
	facetize_log(s, 0, "  %-45s %8d\n", "Perturb retries triggered", rctx->vcnt_p1_trigger);
	facetize_log(s, 0, "  %-45s %8d\n", "Perturb retries passed (P2)", rctx->vcnt_p2_pass);
	facetize_log(s, 0, "  %-45s %8d\n", "Few-hit notes (pre-perturb)", rctx->vcnt_few_hit);
	facetize_log(s, 0, "  %-45s %8d\n", "Few-hit notes (post-perturb)", rctx->vcnt_p2_topoflip);
	facetize_log(s, 0, "  %-45s %8d\n", "No-ray-hit BoTs replaced with empty BoTs", rctx->vcnt_zero_hit);
	facetize_log(s, 0, "  %-45s %8d\n", "Persistent mismatches", rctx->vcnt_p2_warn);
	facetize_log(s, 0, "  %-45s %8d\n", "Validation unavailable", rctx->vcnt_unavail);
	if (!rctx->inspect_regions.empty()) {
	    facetize_log(s, 0, "\n  Regions to inspect manually:\n");
	    for (const auto &iname : rctx->inspect_regions)
		facetize_log(s, 0, "    %s\n", iname.c_str());
	}
    }
}

static void
_ged_regions_variant_summary_cb(void *ctx)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    if (rctx->vcnt_adjusted_instances > 0) {
	facetize_log(rctx->s, 0, "FACETIZE: variant summary: %d adjusted instance(s) "
		"(%d subtractive), %d fallback(s), %d tess failure(s)\n",
		rctx->vcnt_adjusted_instances,
		rctx->vcnt_sub_variants,
		rctx->vcnt_perturb_fallbacks,
		rctx->vcnt_tess_failures);
    }
}

static void
_ged_regions_cleanup_cb(void *ctx)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    if (rctx && rctx->s && rctx->s->wdir)
	bu_dirclear(rctx->s->wdir);
}

void *
_ged_facetize_region_context_create(struct _ged_facetize_state *s)
{
    if (!s)
	return NULL;

    struct rt_wdb *wdbp = wdb_dbopen(s->dbip, RT_WDB_TYPE_DB_DEFAULT);
    s->tol = &(wdbp->wdb_ttol);

    struct ged_region_ctx *rctx = new ged_region_ctx();
    rctx->s = s;
    rctx->vlfree = &rt_vlfree;
    rctx->region_start = bu_gettime();
    rctx->perturb_sa_frac = s->perturb_sa_tol / 100.0;
    rctx->perturb_vol_frac = s->perturb_vol_tol / 100.0;
    return (void *)rctx;
}

void
_ged_facetize_region_context_destroy(void *ctx)
{
    struct ged_region_ctx *rctx = (struct ged_region_ctx *)ctx;
    delete rctx;
}

void
_ged_facetize_region_callbacks_init(struct gcv_facetize_region_callbacks *callbacks)
{
    if (!callbacks)
	return;

    *callbacks = {};
    callbacks->validate_args = _ged_regions_validate_args_cb;
    callbacks->object_fallback = _ged_regions_object_fallback_cb;
    callbacks->set_working_file = _ged_regions_set_working_file_cb;
    callbacks->working_file_setup = _ged_regions_working_file_setup_cb;
    callbacks->primitive_tessellate = _ged_regions_primitive_tessellate_cb;
    callbacks->nmg_eval = _ged_regions_nmg_eval_cb;
    callbacks->manifold_eval = _ged_regions_manifold_eval_cb;
    callbacks->validate_region = _ged_regions_validate_region_cb;
    callbacks->use_variant_plan = _ged_regions_use_variant_plan_cb;
    callbacks->primitive_summary = _ged_regions_primitive_summary_cb;
    callbacks->region_summary = _ged_regions_region_summary_cb;
    callbacks->variant_summary = _ged_regions_variant_summary_cb;
    callbacks->cleanup = _ged_regions_cleanup_cb;
}

int
_ged_facetize_regions(struct _ged_facetize_state *s, int argc, const char **argv)
{
    void *rctx = _ged_facetize_region_context_create(s);
    if (!rctx)
	return BRLCAD_ERROR;

    struct gcv_facetize_region_callbacks callbacks;
    _ged_facetize_region_callbacks_init(&callbacks);
    int ret = gcv_facetize_regions_to_db(s->dbip,
	    argc,
	    argv,
	    s->wdir,
	    bu_vls_cstr(s->bname),
	    bu_vls_cstr(s->prefix),
	    bu_vls_cstr(s->suffix),
	    s->in_place,
	    s->make_nmg,
	    s->nmg_booleval,
	    s->no_perturb,
	    s->verbosity,
	    &callbacks,
	    rctx);
    _ged_facetize_region_context_destroy(rctx);
    return ret;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
