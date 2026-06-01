/*              F A C E T I Z E _ M A N I F O L D . C P P
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
/** @file libgcv/facetize_manifold.cpp
 *
 * Manifold-backed triangular boolean evaluation for facetization.
 */

#include "common.h"

#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <string.h>

#include "manifold/manifold.h"

#include "bg/trimesh.h"
#include "bu/app.h"
#include "gcv/facetize.h"
#include "raytrace.h"
#include "rt/db5.h"
#include "rt/db_internal.h"
#include "rt/functab.h"
#include "rt/global.h"
#include "rt/primitives/bot.h"
#include "rt/wdb.h"
#include "wdb.h"

static const size_t GCV_FACETIZE_EMPTY_CHECK_CROFTON_RAYS = 800u;
static const double GCV_FACETIZE_EMPTY_CHECK_REL_VOL_TOL = 1.0e-9;
static const double GCV_FACETIZE_EMPTY_CHECK_ABS_VOL_TOL = 1.0e-12;

static int
gcv_facetize_sort_existing_objects(struct db_i *dbip, int argc, const char **argv, struct directory **dpa)
{
    int exist_cnt = 0;
    int nonexist_cnt = 0;
    const char **exists = (const char **)bu_calloc(argc, sizeof(const char *), "obj exists array");
    const char **nonexists = (const char **)bu_calloc(argc, sizeof(const char *), "obj nonexists array");

    if (!dbip) {
        bu_free((void *)exists, "exists array");
        bu_free((void *)nonexists, "nonexists array");
        return BRLCAD_ERROR;
    }

    for (int i = 0; i < argc; i++) {
        struct directory *dp = db_lookup(dbip, argv[i], LOOKUP_QUIET);
        if (dp == RT_DIR_NULL) {
            nonexists[nonexist_cnt] = argv[i];
            nonexist_cnt++;
        } else {
            exists[exist_cnt] = argv[i];
            if (dpa)
                dpa[exist_cnt] = dp;
            exist_cnt++;
        }
    }
    for (int i = 0; i < exist_cnt; i++)
        argv[i] = exists[i];
    for (int i = 0; i < nonexist_cnt; i++)
        argv[i + exist_cnt] = nonexists[i];

    bu_free((void *)exists, "exists array");
    bu_free((void *)nonexists, "nonexists array");
    return nonexist_cnt;
}


struct gcv_facetize_copy_data {
    struct db_i *incoming_dbip = NULL;
    struct db_i *target_dbip = NULL;
    std::string affix;
    int suffix = 0;
    int lazy_affix = 1;
    int overwrite = 0;
    long int overwritten = 0;
    std::unordered_map<std::string, std::string> name_map;
    std::unordered_set<std::string> used_names;
};

static int
gcv_facetize_cc_uniq_test(struct bu_vls *n, void *data)
{
    struct gcv_facetize_copy_data *cc_data = (struct gcv_facetize_copy_data *)data;
    if (db_lookup(cc_data->target_dbip, bu_vls_cstr(n), LOOKUP_QUIET) != RT_DIR_NULL)
        return 0;
    if (cc_data->used_names.find(std::string(bu_vls_cstr(n))) != cc_data->used_names.end())
        return 0;
    return 1;
}

static int
gcv_facetize_db_uniq_test(struct bu_vls *n, void *data)
{
    struct db_i *dbip = (struct db_i *)data;
    return (db_lookup(dbip, bu_vls_cstr(n), LOOKUP_QUIET) == RT_DIR_NULL) ? 1 : 0;
}

static int
gcv_facetize_uniq_name(const char *name, struct gcv_facetize_copy_data *cc_data)
{
    const char *orig_name = name ? name : "UNKNOWN";
    struct bu_vls iname = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&iname, "%s", orig_name);
    std::string key = name ? std::string(name) : std::string("UNKNOWN");

    if (cc_data->name_map.find(key) != cc_data->name_map.end()) {
        bu_vls_free(&iname);
        return BRLCAD_OK;
    }

    bool affix_applied = false;
    if (!cc_data->lazy_affix && cc_data->affix.length()) {
        if (cc_data->suffix)
            bu_vls_printf(&iname, "%s", cc_data->affix.c_str());
        else
            bu_vls_prepend(&iname, cc_data->affix.c_str());
        affix_applied = true;
    }

    std::string nname = std::string(bu_vls_cstr(&iname));
    struct directory *ndp = db_lookup(cc_data->target_dbip, bu_vls_cstr(&iname), LOOKUP_QUIET);
    if (!ndp) {
        cc_data->name_map[key] = nname;
        cc_data->used_names.insert(nname);
        bu_vls_free(&iname);
        return BRLCAD_OK;
    }

    bu_vls_sprintf(&iname, "%s", nname.c_str());
    if (cc_data->affix.size() && !affix_applied) {
        if (!cc_data->suffix)
            bu_vls_prepend(&iname, cc_data->affix.c_str());
        else
            bu_vls_printf(&iname, "%s", cc_data->affix.c_str());
        ndp = db_lookup(cc_data->target_dbip, bu_vls_cstr(&iname), LOOKUP_QUIET);
        if (!ndp) {
            nname = std::string(bu_vls_cstr(&iname));
            cc_data->name_map[key] = nname;
            cc_data->used_names.insert(nname);
            bu_vls_free(&iname);
            return BRLCAD_OK;
        }
    }

    const char *rx = NULL;
    const char *prx = "[!0-9]*([0-9+]).*";
    bu_vls_sprintf(&iname, "%s", orig_name);
    if (cc_data->suffix) {
        bu_vls_printf(&iname, "_0");
        if (cc_data->affix.length())
            bu_vls_printf(&iname, "%s", cc_data->affix.c_str());
    } else {
        rx = prx;
        bu_vls_prepend(&iname, "0_");
        if (cc_data->affix.length())
            bu_vls_prepend(&iname, cc_data->affix.c_str());
    }
    if (bu_vls_incr(&iname, rx, NULL, &gcv_facetize_cc_uniq_test, (void *)cc_data) < 0) {
        bu_vls_free(&iname);
        return BRLCAD_ERROR;
    }

    nname = std::string(bu_vls_cstr(&iname));
    cc_data->name_map[key] = nname;
    cc_data->used_names.insert(nname);
    bu_vls_free(&iname);
    return BRLCAD_OK;
}

static void
gcv_facetize_adjust_names(union tree *trp, struct gcv_facetize_copy_data *cc_data)
{
    if (cc_data->overwrite || !trp)
        return;

    switch (trp->tr_op) {
        case OP_DB_LEAF: {
            std::string old_name = std::string(trp->tr_l.tl_name);
            std::string new_name = cc_data->name_map[old_name];
            if (old_name != new_name && new_name.length()) {
                bu_free(trp->tr_l.tl_name, "leaf name");
                trp->tr_l.tl_name = bu_strdup(new_name.c_str());
            }
            break;
        }
        case OP_UNION:
        case OP_INTERSECT:
        case OP_SUBTRACT:
        case OP_XOR:
            gcv_facetize_adjust_names(trp->tr_b.tb_right, cc_data);
            /* fall through */
        case OP_NOT:
        case OP_GUARD:
        case OP_XNOP:
            gcv_facetize_adjust_names(trp->tr_b.tb_left, cc_data);
            break;
        default:
            break;
    }
}

static int
gcv_facetize_copy_object(struct directory *input_dp, struct gcv_facetize_copy_data *cc_data)
{
    struct rt_db_internal ip;
    RT_DB_INTERNAL_INIT(&ip);
    if (rt_db_get_internal(&ip, input_dp, cc_data->incoming_dbip, NULL) < 0)
        return BRLCAD_ERROR;

    std::string new_name;
    std::string old_name;
    if (ip.idb_major_type == DB5_MAJORTYPE_BRLCAD) {
        switch (ip.idb_minor_type) {
            case DB5_MINORTYPE_BRLCAD_COMBINATION: {
                struct rt_comb_internal *comb = (struct rt_comb_internal *)ip.idb_ptr;
                RT_CK_COMB(comb);
                gcv_facetize_adjust_names(comb->tree, cc_data);
                break;
            }
            case DB5_MINORTYPE_BRLCAD_EXTRUDE: {
                struct rt_extrude_internal *extr = (struct rt_extrude_internal *)ip.idb_ptr;
                RT_EXTRUDE_CK_MAGIC(extr);
                old_name = std::string(extr->sketch_name);
                new_name = cc_data->name_map[old_name];
                if (new_name.length() && new_name != old_name) {
                    bu_free(extr->sketch_name, "sketch name");
                    extr->sketch_name = bu_strdup(new_name.c_str());
                }
                break;
            }
            case DB5_MINORTYPE_BRLCAD_DSP: {
                struct rt_dsp_internal *dsp = (struct rt_dsp_internal *)ip.idb_ptr;
                RT_DSP_CK_MAGIC(dsp);
                if (dsp->dsp_datasrc == RT_DSP_SRC_OBJ) {
                    old_name = std::string(bu_vls_cstr(&dsp->dsp_name));
                    new_name = cc_data->name_map[old_name];
                    if (new_name.length()) {
                        bu_vls_free(&dsp->dsp_name);
                        bu_vls_strcpy(&dsp->dsp_name, new_name.c_str());
                    }
                }
                break;
            }
        }
    }

    if (cc_data->overwrite) {
        new_name = std::string(input_dp->d_namep);
    } else {
        new_name = cc_data->name_map[std::string(input_dp->d_namep)];
        if (!new_name.length()) {
            rt_db_free_internal(&ip);
            return BRLCAD_ERROR;
        }
    }

    struct directory *oride_dp = NULL;
    std::string owrite_backup;
    if (cc_data->overwrite) {
        oride_dp = db_lookup(cc_data->target_dbip, input_dp->d_namep, LOOKUP_QUIET);
        if (oride_dp) {
            struct bu_vls bname = BU_VLS_INIT_ZERO;
            bu_vls_sprintf(&bname, "%s.bak", input_dp->d_namep);
            if (bu_vls_incr(&bname, NULL, NULL, &gcv_facetize_db_uniq_test, (void *)cc_data->target_dbip) < 0)
                owrite_backup = std::string(input_dp->d_namep) + std::string(".bak");
            else
                owrite_backup = std::string(bu_vls_cstr(&bname));
            bu_vls_free(&bname);
            db_rename(cc_data->target_dbip, oride_dp, owrite_backup.c_str());
        }
    }

    struct directory *new_dp = db_diradd(cc_data->target_dbip, new_name.c_str(), RT_DIR_PHONY_ADDR, 0,
            input_dp->d_flags, (void *)&input_dp->d_minor_type);
    if (new_dp == RT_DIR_NULL) {
        rt_db_free_internal(&ip);
        return BRLCAD_ERROR;
    }
    new_dp->d_major_type = input_dp->d_major_type;

    if (rt_db_put_internal(new_dp, cc_data->target_dbip, &ip) < 0)
        return BRLCAD_ERROR;

    if (oride_dp) {
        if (db_delete(cc_data->target_dbip, oride_dp) != 0 || db_dirdelete(cc_data->target_dbip, oride_dp) != 0)
            return BRLCAD_ERROR;
        db_update_nref(cc_data->target_dbip);
        cc_data->overwritten++;
    }

    return BRLCAD_OK;
}

static void
gcv_facetize_collect_tree_names(union tree *trp, std::set<std::string> &names)
{
    if (!trp)
        return;
    switch (trp->tr_op) {
        case OP_DB_LEAF:
            names.insert(std::string(trp->tr_l.tl_name));
            break;
        case OP_UNION:
        case OP_INTERSECT:
        case OP_SUBTRACT:
        case OP_XOR:
            gcv_facetize_collect_tree_names(trp->tr_b.tb_right, names);
            /* fall through */
        case OP_NOT:
        case OP_GUARD:
        case OP_XNOP:
            gcv_facetize_collect_tree_names(trp->tr_b.tb_left, names);
            break;
        default:
            break;
    }
}

static int
gcv_facetize_collect_closure(struct db_i *dbip, const char *name, std::set<std::string> &names)
{
    if (!dbip || !name)
        return BRLCAD_ERROR;
    if (names.find(std::string(name)) != names.end())
        return BRLCAD_OK;

    struct directory *dp = db_lookup(dbip, name, LOOKUP_QUIET);
    if (!dp)
        return BRLCAD_ERROR;
    names.insert(std::string(name));

    if (!(dp->d_flags & RT_DIR_COMB))
        return BRLCAD_OK;

    struct rt_db_internal intern;
    RT_DB_INTERNAL_INIT(&intern);
    if (rt_db_get_internal(&intern, dp, dbip, NULL) < 0)
        return BRLCAD_ERROR;
    struct rt_comb_internal *comb = (struct rt_comb_internal *)intern.idb_ptr;
    RT_CK_COMB(comb);
    std::set<std::string> leaves;
    gcv_facetize_collect_tree_names(comb->tree, leaves);
    rt_db_free_internal(&intern);

    for (const auto &lname : leaves) {
        if (gcv_facetize_collect_closure(dbip, lname.c_str(), names) != BRLCAD_OK)
            return BRLCAD_ERROR;
    }
    return BRLCAD_OK;
}

struct gcv_facetize_manifold_state {
    struct db_i *source_db;
    union tree *facetize_tree;
    int error_flag;
    int verbosity;
    gcv_facetize_log_cb log_cb;
    void *log_ctx;
    gcv_facetize_variant_name_cb variant_cb;
    void *variant_ctx;
};

static void
_gcv_facetize_manifold_log(struct gcv_facetize_manifold_state *s, int level, const char *fmt, ...)
{
    struct bu_vls msg = BU_VLS_INIT_ZERO;
    va_list ap;

    if (!s || !fmt)
        return;

    va_start(ap, fmt);
    bu_vls_vprintf(&msg, fmt, ap);
    va_end(ap);

    if (s->log_cb)
        s->log_cb(s->log_ctx, level, bu_vls_cstr(&msg));
    else
        bu_log("%s", bu_vls_cstr(&msg));

    bu_vls_free(&msg);
}

static int
gcv_bot_to_manifold(void **out, struct db_tree_state *tsp, struct rt_db_internal *ip, int flip)
{
    if (!out || !tsp || !ip)
        return BRLCAD_ERROR;

    if (ip->idb_minor_type != ID_BOT)
        return BRLCAD_ERROR;

    struct rt_bot_internal *nbot = (struct rt_bot_internal *)ip->idb_ptr;

    if (!nbot->num_vertices) {
        (*out) = new manifold::Manifold();
        return 0;
    }

    if (flip) {
        switch (nbot->orientation) {
            case RT_BOT_CCW:
                nbot->orientation = RT_BOT_CW;
                break;
            default:
                nbot->orientation = RT_BOT_CCW;
        }
    }

    if (nbot->num_vertices < 3)
        return BRLCAD_ERROR;

    manifold::MeshGL64 bot_mesh;
    for (size_t j = 0; j < nbot->num_vertices*3 ; j++)
        bot_mesh.vertProperties.insert(bot_mesh.vertProperties.end(), nbot->vertices[j]);
    if (nbot->orientation == RT_BOT_CW) {
        for (size_t j = 0; j < nbot->num_faces; j++) {
            bot_mesh.triVerts.insert(bot_mesh.triVerts.end(), nbot->faces[3*j+0]);
            bot_mesh.triVerts.insert(bot_mesh.triVerts.end(), nbot->faces[3*j+2]);
            bot_mesh.triVerts.insert(bot_mesh.triVerts.end(), nbot->faces[3*j+1]);
        }
    } else {
        for (size_t j = 0; j < nbot->num_faces; j++) {
            bot_mesh.triVerts.insert(bot_mesh.triVerts.end(), nbot->faces[3*j+0]);
            bot_mesh.triVerts.insert(bot_mesh.triVerts.end(), nbot->faces[3*j+1]);
            bot_mesh.triVerts.insert(bot_mesh.triVerts.end(), nbot->faces[3*j+2]);
        }
    }

    manifold::Manifold bot_manifold = manifold::Manifold(bot_mesh);
    if (bot_manifold.Status() != manifold::Manifold::Error::NoError)
        return BRLCAD_ERROR;

    (*out) = new manifold::Manifold(bot_manifold);
    return 0;
}

static int
gcv_bot_flipped(mat_t *m)
{
    point_t oorigin = {-0.4, 0.5, 0.4};
    point_t othit = {-0.301, 0.581, 0.28};
    point_t ov[3] = {{0, 1, 1}, {-1, 1, 0}, {0, 0, 0}};
    point_t origin, thit;
    point_t v[3];

    for (int i = 0; i < 3; i++)
        MAT4X3PNT(v[i], *m, ov[i]);
    MAT4X3PNT(origin, *m, oorigin);
    MAT4X3PNT(thit, *m, othit);

    vect_t raydir;
    VSUB2(raydir, thit, origin);

    vect_t edges[2];
    VSUB2(edges[0], v[1], v[0]);
    VSUB2(edges[1], v[2], v[1]);

    vect_t ecross;
    VCROSS(ecross, edges[0], edges[1]);

    return (VDOT(ecross, raydir) > 0) ? 1 : 0;
}

static double
gcv_bot_bbox_volume(const struct rt_bot_internal *bot)
{
    if (!bot || !bot->vertices || bot->num_vertices < 1)
        return 0.0;

    point_t bmin, bmax;
    VSETALL(bmin, INFINITY);
    VSETALL(bmax, -INFINITY);
    for (size_t i = 0; i < bot->num_vertices; i++) {
        const double *v = &bot->vertices[3*i];
        if (v[0] < bmin[0]) bmin[0] = v[0];
        if (v[1] < bmin[1]) bmin[1] = v[1];
        if (v[2] < bmin[2]) bmin[2] = v[2];
        if (v[0] > bmax[0]) bmax[0] = v[0];
        if (v[1] > bmax[1]) bmax[1] = v[1];
        if (v[2] > bmax[2]) bmax[2] = v[2];
    }

    vect_t d;
    VSUB2(d, bmax, bmin);
    if (d[0] <= 0.0 || d[1] <= 0.0 || d[2] <= 0.0)
        return 0.0;
    return d[0] * d[1] * d[2];
}

static int
gcv_csg_crofton_volume(struct db_i *dbip, const char *obj_name, double *out_vol)
{
    if (!dbip || !obj_name || !out_vol)
        return BRLCAD_ERROR;

    *out_vol = -1.0;
    struct rt_i *rtip = rt_new_rti(dbip);
    if (!rtip)
        return BRLCAD_ERROR;
    if (rt_gettree(rtip, obj_name) != 0) {
        rt_free_rti(rtip);
        return BRLCAD_ERROR;
    }
    rt_prep_parallel(rtip, 1);

    double sa = 0.0, vol = 0.0;
    struct rt_crofton_params crp = {GCV_FACETIZE_EMPTY_CHECK_CROFTON_RAYS, 0.0, 0.0};
    int rc = rt_crofton_shoot(rtip, &crp, &sa, &vol);
    rt_free_rti(rtip);
    if (rc < 0)
        return BRLCAD_ERROR;
    *out_vol = vol;
    return BRLCAD_OK;
}

static union tree *
gcv_booltree_leaf_tess(struct db_tree_state *tsp, const struct db_full_path *pathp, struct rt_db_internal *ip, void *data)
{
    int ts_status = 0;
    union tree *curtree;
    struct directory *dp;
    struct gcv_facetize_manifold_state *s = (struct gcv_facetize_manifold_state *)data;

    if (!tsp || !pathp || !ip)
        return TREE_NULL;

    RT_CK_DB_INTERNAL(ip);
    RT_CK_FULL_PATH(pathp);
    dp = DB_FULL_PATH_CUR_DIR(pathp);
    RT_CK_DIR(dp);

    if (tsp->ts_m)
        NMG_CK_MODEL(*tsp->ts_m);
    BN_CK_TOL(tsp->ts_tol);
    BG_CK_TESS_TOL(tsp->ts_ttol);

    BU_GET(curtree, union tree);
    RT_TREE_INIT(curtree);
    curtree->tr_op = OP_TESS;
    curtree->tr_d.td_name = bu_strdup(dp->d_namep);
    curtree->tr_d.td_r = NULL;
    curtree->tr_d.td_d = NULL;
    curtree->tr_d.td_i = NULL;

    if (ip->idb_minor_type == ID_HALF) {
        struct rt_db_internal *hintern;
        BU_GET(hintern, struct rt_db_internal);
        RT_DB_INTERNAL_INIT(hintern);
        hintern->idb_major_type = DB5_MAJORTYPE_BRLCAD;
        hintern->idb_type = ID_HALF;
        hintern->idb_meth = &OBJ[ID_HALF];
        struct rt_half_internal *hf_cp;
        BU_GET(hf_cp, struct rt_half_internal);
        hintern->idb_ptr = (void *)hf_cp;

        struct rt_half_internal *hf_ip= (struct rt_half_internal *)ip->idb_ptr;
        hf_cp->magic = hf_ip->magic;
        HMOVE(hf_cp->eqn, hf_ip->eqn);
        curtree->tr_d.td_i = hintern;
        return curtree;
    }

    if (ip->idb_minor_type != ID_BOT)
        return curtree;

    int flip = gcv_bot_flipped(&tsp->ts_mat);

    struct rt_db_internal var_intern;
    RT_DB_INTERNAL_INIT(&var_intern);
    bool var_loaded = false;
    struct rt_db_internal *effective_ip = ip;
    if (s && s->variant_cb) {
        char *path_str = db_path_to_string(pathp);
        bool is_sub_ctx = (tsp->ts_sofar & TS_SOFAR_MINUS) != 0;
        const char *variant_name = s->variant_cb(s->variant_ctx, path_str, is_sub_ctx ? 1 : 0);
        bu_free(path_str, "path_str");
        if (variant_name) {
            struct directory *vdp = db_lookup(tsp->ts_dbip, variant_name, LOOKUP_QUIET);
            if (vdp && vdp->d_minor_type == ID_BOT) {
                if (rt_db_get_internal(&var_intern, vdp, tsp->ts_dbip, NULL) >= 0) {
                    effective_ip = &var_intern;
                    var_loaded = true;
                }
            }
        }
    }

    void *odata = NULL;
    ts_status = gcv_bot_to_manifold(&odata, tsp, effective_ip, flip);

    if (var_loaded)
        rt_db_free_internal(&var_intern);
    if (ts_status < 0)
        return TREE_NULL;

    if (s && s->verbosity > 1) {
        bool is_sub_ctx = (tsp->ts_sofar & TS_SOFAR_MINUS) != 0;
        double leaf_sa = 0.0;
        if (odata) {
            manifold::Manifold *lm = (manifold::Manifold *)odata;
            leaf_sa = lm->SurfaceArea();
        }
        bu_log("[LEAF_TESS] name=%-30s  role=%s  mesh_SA=%.6f mm^2\n",
               dp->d_namep,
               is_sub_ctx ? "SUB " : "BASE",
               leaf_sa);
    }

    BU_GET(curtree, union tree);
    RT_TREE_INIT(curtree);
    curtree->tr_op = OP_TESS;
    curtree->tr_d.td_name = bu_strdup(dp->d_namep);
    curtree->tr_d.td_r = NULL;
    curtree->tr_d.td_d = odata;
    curtree->tr_d.td_i = NULL;

    if (s && s->verbosity > 1 && (RT_G_DEBUG & RT_DEBUG_TREEWALK))
        bu_log("gcv_booltree_leaf_tess(%s) OK\n", dp->d_namep);

    return curtree;
}

static union tree *
gcv_facetize_region_end(struct db_tree_state *tsp,
                        const struct db_full_path *pathp,
                        union tree *curtree,
                        void *client_data)
{
    union tree **facetize_tree;

    if (tsp) RT_CK_DBTS(tsp);
    if (pathp) RT_CK_FULL_PATH(pathp);

    struct gcv_facetize_manifold_state *s = (struct gcv_facetize_manifold_state *)client_data;
    facetize_tree = &s->facetize_tree;

    if (curtree->tr_op == OP_NOP)
        return curtree;

    if (*facetize_tree) {
        union tree *tr;
        BU_ALLOC(tr, union tree);
        RT_TREE_INIT(tr);
        tr->tr_op = OP_UNION;
        tr->tr_b.tb_regionp = REGION_NULL;
        tr->tr_b.tb_left = *facetize_tree;
        tr->tr_b.tb_right = curtree;
        *facetize_tree = tr;
    } else {
        *facetize_tree = curtree;
    }

    return TREE_NULL;
}

static int
gcv_manifold_do_bool(union tree *tp, union tree *tl, union tree *tr, int op, struct bu_list *UNUSED(vlfree), const struct bn_tol *UNUSED(tol), void *data)
{
    struct gcv_facetize_manifold_state *s = (struct gcv_facetize_manifold_state *)data;
    if (!s)
        return -1;

    manifold::OpType manifold_op = manifold::OpType::Add;
    switch (op) {
        case OP_INTERSECT:
            manifold_op = manifold::OpType::Intersect;
            break;
        case OP_SUBTRACT:
            manifold_op = manifold::OpType::Subtract;
            break;
        case OP_UNION:
        default:
            manifold_op = manifold::OpType::Add;
            break;
    };

    if (tl->tr_d.td_i) {
        bu_log("Error - internal pointer on left boolean input\n");
        return -1;
    }

    manifold::Manifold *lm = (manifold::Manifold *)tl->tr_d.td_d;
    manifold::Manifold *rm = (manifold::Manifold *)tr->tr_d.td_d;
    manifold::Manifold *result = NULL;
    int failed = 0;
    bool delete_left = false;
    bool delete_right = false;
    if (tr->tr_d.td_i) {
        if (tr->tr_d.td_i->idb_minor_type != ID_HALF)
            return -1;
        if (!lm) {
            lm = new manifold::Manifold();
            delete_left = true;
        }
        struct rt_half_internal *hf_ip= (struct rt_half_internal *)tr->tr_d.td_i->idb_ptr;
        if (manifold_op != manifold::OpType::Add) {
            vect_t pn;
            pn[0] = hf_ip->eqn[0];
            pn[1] = hf_ip->eqn[1];
            pn[2] = hf_ip->eqn[2];
            if (op == OP_INTERSECT)
                VSCALE(pn, pn, -1);
            manifold::Manifold trimmed = lm->TrimByPlane(linalg::vec<double, 3>(pn[0], pn[1], pn[2]), hf_ip->eqn[3]);
            result = new manifold::Manifold(trimmed);
        }

        BU_PUT(hf_ip, struct rt_half_internal);
        BU_PUT(tr->tr_d.td_i, struct rt_db_internal);
        tr->tr_d.td_i = NULL;
    }

    if (!lm) {
        lm = new manifold::Manifold();
        delete_left = true;
    }
    if (!rm) {
        rm = new manifold::Manifold();
        delete_right = true;
    }

    if (!result) {
        _gcv_facetize_manifold_log(s, 1, "Trying boolean op:  %s, %s\n", tl->tr_d.td_name, tr->tr_d.td_name);
        static const char *op_names[] = {"ADD","INTERSECT","SUBTRACT","ADD"};
        int opidx = (op == OP_INTERSECT) ? 1 : (op == OP_SUBTRACT) ? 2 : 0;
        if (s->verbosity > 1) {
            bu_log("[BOOL_OP] %-8s L=%-30s SA=%.4f  R=%-30s SA=%.4f\n",
                   op_names[opidx], tl->tr_d.td_name, lm->SurfaceArea(), tr->tr_d.td_name, rm->SurfaceArea());
        }

        manifold::Manifold bool_out;
        try {
            bool_out = lm->Boolean(*rm, manifold_op);
        } catch (...) {
            _gcv_facetize_manifold_log(s, 0, "Manifold boolean library threw failure\n");
            const char *evar = getenv("GED_MANIFOLD_DEBUG");
            if (evar && strlen(evar))
                bu_exit(EXIT_FAILURE, "Exiting after Manifold boolean failure.");
            failed = 1;
        }

        if (!failed) {
            if (s->verbosity > 1) {
                bu_log("[BOOL_OP] %-8s L=%-30s  R=%-30s  result_SA=%.4f\n",
                       op_names[opidx], tl->tr_d.td_name, tr->tr_d.td_name, bool_out.SurfaceArea());
            }
            result = new manifold::Manifold(bool_out);
        }

    }

    if (delete_left)
        delete lm;
    if (delete_right)
        delete rm;

    if (tl->tr_d.td_d) {
        manifold::Manifold *m = (manifold::Manifold *)tl->tr_d.td_d;
        delete m;
        tl->tr_d.td_d = NULL;
    }
    if (tr->tr_d.td_d) {
        manifold::Manifold *m = (manifold::Manifold *)tr->tr_d.td_d;
        delete m;
        tr->tr_d.td_d = NULL;
    }

    if (failed) {
        tp->tr_d.td_d = NULL;
        return -1;
    }

    tp->tr_op = OP_TESS;
    tp->tr_d.td_d = (void *)result;
    return 0;
}

static struct rt_bot_internal *
gcv_facetize_empty_bot(void)
{
    struct rt_bot_internal *bot;
    BU_GET(bot, struct rt_bot_internal);
    bot->magic = RT_BOT_INTERNAL_MAGIC;
    bot->mode = RT_BOT_SOLID;
    bot->orientation = RT_BOT_CCW;
    bot->thickness = NULL;
    bot->face_mode = (struct bu_bitv *)NULL;
    bot->bot_flags = 0;
    bot->num_vertices = 0;
    bot->num_faces = 0;
    bot->vertices = NULL;
    bot->faces = NULL;
    return bot;
}

static int
gcv_facetize_write_bot_to_db(struct db_i *dbip, struct rt_bot_internal *bot, const char *name, int verbosity)
{
    struct rt_db_internal intern;
    struct directory *dp;

    RT_DB_INTERNAL_INIT(&intern);
    intern.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    intern.idb_type = ID_BOT;
    intern.idb_meth = &OBJ[ID_BOT];
    intern.idb_ptr = (void *)bot;

    dp = db_diradd(dbip, name, RT_DIR_PHONY_ADDR, 0, RT_DIR_SOLID, (void *)&intern.idb_type);
    if (dp == RT_DIR_NULL) {
        if (verbosity > 0)
            bu_log("Cannot add %s to directory\n", name);
        return BRLCAD_ERROR;
    }

    if (rt_db_put_internal(dp, dbip, &intern) < 0) {
        if (verbosity > 0)
            bu_log("Failed to write %s to database\n", name);
        rt_db_free_internal(&intern);
        return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}




static int
gcv_facetize_vls_db_uniq_test(struct bu_vls *n, void *data)
{
    struct db_i *dbip = (struct db_i *)data;
    return (db_lookup(dbip, bu_vls_cstr(n), LOOKUP_QUIET) == RT_DIR_NULL) ? 1 : 0;
}

int
gcv_facetize_region_result_name(struct db_i *working_db,
                                const char *root_name,
                                int make_nmg,
                                struct bu_vls *result_name)
{
    if (!working_db || !root_name || !result_name)
        return BRLCAD_ERROR;

    bu_vls_sprintf(result_name, "%s.%s", root_name, make_nmg ? "nmg" : "bot");
    struct directory *dcheck = db_lookup(working_db, bu_vls_cstr(result_name), LOOKUP_QUIET);
    if (dcheck != RT_DIR_NULL) {
        if (bu_vls_incr(result_name, NULL, NULL, &gcv_facetize_vls_db_uniq_test, (void *)working_db) < 0)
            return BRLCAD_ERROR;
    }
    return BRLCAD_OK;
}

int
gcv_facetize_region_replace_root(struct db_i *working_db,
                                 const char *root_name,
                                 const char *result_name)
{
    if (!working_db || !root_name || !result_name)
        return BRLCAD_ERROR;

    struct directory *wdp = db_lookup(working_db, root_name, LOOKUP_QUIET);
    if (!wdp)
        return BRLCAD_ERROR;

    if (wdp->d_flags & RT_DIR_COMB) {
        struct rt_db_internal intern;
        RT_DB_INTERNAL_INIT(&intern);
        if (rt_db_get_internal(&intern, wdp, working_db, NULL) < 0)
            return BRLCAD_ERROR;
        struct rt_comb_internal *comb = (struct rt_comb_internal *)intern.idb_ptr;
        RT_CK_COMB(comb);
        db_free_tree(comb->tree);
        union tree *tp;
        struct rt_tree_array *tree_list;
        BU_GET(tree_list, struct rt_tree_array);
        tree_list[0].tl_op = OP_UNION;
        BU_GET(tp, union tree);
        RT_TREE_INIT(tp);
        tree_list[0].tl_tree = tp;
        tp->tr_l.tl_op = OP_DB_LEAF;
        tp->tr_l.tl_name = bu_strdup(result_name);
        tp->tr_l.tl_mat = NULL;
        comb->tree = (union tree *)db_mkgift_tree(tree_list, 1);
        struct rt_wdb *wwdbp = wdb_dbopen(working_db, RT_WDB_TYPE_DB_DEFAULT);
        if (wdb_put_internal(wwdbp, wdp->d_namep, &intern, 1.0) < 0)
            return BRLCAD_ERROR;
    } else {
        struct directory *bot_dp = db_lookup(working_db, result_name, LOOKUP_QUIET);
        if (!bot_dp || db_delete(working_db, wdp) != 0 || db_dirdelete(working_db, wdp) != 0 ||
                db_rename(working_db, bot_dp, root_name) < 0)
            return BRLCAD_ERROR;
    }

    db_update_nref(working_db);
    return BRLCAD_OK;
}


int
gcv_facetize_to_db(struct db_i *db,
                   int argc,
                   const char **argv,
                   const struct gcv_facetize_db_opts *opts,
                   const struct gcv_facetize_db_callbacks *callbacks)
{
    if (!db || argc <= 0 || !argv || !opts || !callbacks)
        return BRLCAD_ERROR;

    if (opts->region_mode) {
        return gcv_facetize_regions_to_db(db,
                argc,
                argv,
                opts->working_dir,
                opts->base_name,
                opts->prefix,
                opts->suffix,
                opts->in_place,
                opts->make_nmg,
                opts->nmg_booleval,
                opts->no_perturb,
                opts->verbosity,
                &callbacks->regions,
                callbacks->region_data);
    }

    return gcv_facetize_objects_to_db(db,
            argc,
            argv,
            opts->in_place,
            opts->make_nmg,
            opts->nmg_booleval,
            &callbacks->objects,
            callbacks->object_data);
}


int
gcv_facetize_regions_to_db(struct db_i *target_db,
                           int argc,
                           const char **argv,
                           const char *working_dir,
                           const char *base_name,
                           const char *prefix,
                           const char *suffix,
                           int in_place,
                           int make_nmg,
                           int nmg_booleval,
                           int no_perturb,
                           int verbosity,
                           const struct gcv_facetize_region_callbacks *callbacks,
                           void *callback_data)
{
    if (!target_db || !argc || !argv || !working_dir || !base_name || !callbacks ||
            !callbacks->validate_args || !callbacks->object_fallback ||
            !callbacks->set_working_file || !callbacks->working_file_setup ||
            !callbacks->nmg_eval || !callbacks->manifold_eval)
        return BRLCAD_ERROR;

    int ret = BRLCAD_OK;
    struct directory **dpa = (struct directory **)bu_calloc(argc, sizeof(struct directory *), "dp array");
    int newobjcnt = gcv_facetize_sort_existing_objects(target_db, argc, argv, dpa);
    if (callbacks->validate_args(callback_data, argc, argv, newobjcnt) == BRLCAD_ERROR) {
        bu_free(dpa, "dp array");
        return BRLCAD_ERROR;
    }

    const char *output_name = argv[argc - 1];
    argc--;

    const char *active_regions = "( -type r ! -below -type r )";
    struct bu_ptbl *ar = NULL;
    BU_ALLOC(ar, struct bu_ptbl);
    if (db_search(ar, DB_SEARCH_RETURN_UNIQ_DP, active_regions, argc, dpa, target_db, NULL, NULL, NULL) < 0) {
        bu_ptbl_free(ar);
        bu_free(ar, "ar table");
        bu_free(dpa, "dp array");
        return BRLCAD_OK;
    }

    if (!BU_PTBL_LEN(ar)) {
        bu_ptbl_free(ar);
        bu_free(ar, "ar table");
        bu_free(dpa, "dp array");
        return callbacks->object_fallback(callback_data, argc + 1, argv);
    }

    const char *active_solids = "! -type comb";
    struct bu_ptbl *as = NULL;
    BU_ALLOC(as, struct bu_ptbl);
    if (db_search(as, DB_SEARCH_RETURN_UNIQ_DP, active_solids, argc, dpa, target_db, NULL, NULL, NULL) < 0) {
        bu_ptbl_free(as);
        bu_free(as, "as table");
        bu_ptbl_free(ar);
        bu_free(ar, "ar table");
        bu_free(dpa, "dp array");
        return BRLCAD_OK;
    }
    if (!BU_PTBL_LEN(as)) {
        bu_ptbl_free(as);
        bu_free(as, "as table");
        bu_ptbl_free(ar);
        bu_free(ar, "ar table");
        bu_free(dpa, "dp array");
        return BRLCAD_OK;
    }

    struct bu_vls wfilename = BU_VLS_INIT_ZERO;
    char tmpwfile[MAXPATHLEN] = {0};
    unsigned long long hash_num = bu_data_hash((void *)base_name, strlen(base_name));
    bu_vls_sprintf(&wfilename, "facetize_regions_%s_%llu", base_name, hash_num);
    bu_dir(tmpwfile, MAXPATHLEN, working_dir, bu_vls_cstr(&wfilename), NULL);
    bu_vls_free(&wfilename);
    if (callbacks->set_working_file(callback_data, tmpwfile) != BRLCAD_OK) {
        bu_ptbl_free(as);
        bu_free(as, "as table");
        bu_ptbl_free(ar);
        bu_free(ar, "ar table");
        bu_free(dpa, "dp array");
        return BRLCAD_ERROR;
    }

    if (callbacks->working_file_setup(callback_data, as) != BRLCAD_OK) {
        bu_ptbl_free(as);
        bu_free(as, "as table");
        bu_ptbl_free(ar);
        bu_free(ar, "ar table");
        bu_free(dpa, "dp array");
        return BRLCAD_ERROR;
    }

    if (!make_nmg && !nmg_booleval && callbacks->primitive_tessellate) {
        if (callbacks->primitive_tessellate(callback_data, target_db, as) != BRLCAD_OK) {
            bu_ptbl_free(as);
            bu_free(as, "as table");
            bu_ptbl_free(ar);
            bu_free(ar, "ar table");
            bu_free(dpa, "dp array");
            return BRLCAD_ERROR;
        }
    }
    bu_ptbl_free(as);
    bu_free(as, "as table");

    const char *implicit_regions = "( ! -below -type r ! -type comb )";
    struct bu_ptbl *ir = NULL;
    BU_ALLOC(ir, struct bu_ptbl);
    if (db_search(ir, DB_SEARCH_RETURN_UNIQ_DP, implicit_regions, argc, dpa, target_db, NULL, NULL, NULL) < 0) {
        bu_ptbl_free(ar);
        bu_free(ar, "ar table");
        bu_free(dpa, "dp array");
        return BRLCAD_OK;
    }

    if (BU_PTBL_LEN(ir) && (make_nmg || nmg_booleval)) {
        for (size_t i = 0; i < BU_PTBL_LEN(ir); i++) {
            struct directory *idp = (struct directory *)BU_PTBL_GET(ir, i);
            struct db_i *wdbip = db_open(tmpwfile, DB_OPEN_READWRITE);
            if (!wdbip) {
                bu_ptbl_free(ir);
                bu_free(ir, "ir table");
                bu_ptbl_free(ar);
                bu_free(ar, "ar table");
                bu_free(dpa, "dp array");
                return BRLCAD_ERROR;
            }
            db_dirbuild(wdbip);
            db_update_nref(wdbip);
            int nret = callbacks->nmg_eval(callback_data, wdbip, idp->d_namep, idp->d_namep);
            db_close(wdbip);
            if (nret != BRLCAD_OK) {
                bu_ptbl_free(ir);
                bu_free(ir, "ir table");
                bu_ptbl_free(ar);
                bu_free(ar, "ar table");
                bu_free(dpa, "dp array");
                return BRLCAD_ERROR;
            }
        }
    }

    struct bu_ptbl eval_roots = BU_PTBL_INIT_ZERO;
    std::set<std::string> eval_names;
    for (size_t i = 0; i < BU_PTBL_LEN(ar); i++) {
        struct directory *dp = (struct directory *)BU_PTBL_GET(ar, i);
        eval_names.insert(std::string(dp->d_namep));
        bu_ptbl_ins(&eval_roots, (long *)dp);
    }
    if (!make_nmg && !nmg_booleval) {
        for (size_t i = 0; i < BU_PTBL_LEN(ir); i++) {
            struct directory *dp = (struct directory *)BU_PTBL_GET(ir, i);
            if (eval_names.find(std::string(dp->d_namep)) == eval_names.end()) {
                eval_names.insert(std::string(dp->d_namep));
                bu_ptbl_ins(&eval_roots, (long *)dp);
            }
        }
    }
    size_t eval_total = BU_PTBL_LEN(&eval_roots);
    if (verbosity == 0)
        bu_log("Evaluating %zu roots...\n", eval_total);

    if (!make_nmg && !nmg_booleval && !no_perturb && callbacks->use_variant_plan)
        callbacks->use_variant_plan(callback_data, 0);

    struct bu_vls result_name = BU_VLS_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&eval_roots); i++) {
        struct directory *root_dp = (struct directory *)BU_PTBL_GET(&eval_roots, i);
        struct db_i *wdbip = db_open(tmpwfile, DB_OPEN_READWRITE);
        if (!wdbip) { ret = BRLCAD_ERROR; break; }
        db_dirbuild(wdbip);
        db_update_nref(wdbip);
        if (gcv_facetize_region_result_name(wdbip, root_dp->d_namep, make_nmg, &result_name) != BRLCAD_OK) {
            db_close(wdbip);
            ret = BRLCAD_ERROR;
            break;
        }

        int bret = BRLCAD_OK;
        if (make_nmg || nmg_booleval) {
            bret = callbacks->nmg_eval(callback_data, wdbip, root_dp->d_namep, bu_vls_cstr(&result_name));
            db_close(wdbip);
        } else {
            struct rt_wdb *wwdbp = wdb_dbopen(wdbip, RT_WDB_TYPE_DB_DEFAULT);
            bret = callbacks->manifold_eval(callback_data, wdbip, wwdbp, root_dp->d_namep, bu_vls_cstr(&result_name), i + 1, eval_total);
            if (callbacks->validate_region && callbacks->validate_region(callback_data, root_dp->d_namep, bu_vls_cstr(&result_name), &wdbip, &wwdbp, i + 1, eval_total, &bret) != BRLCAD_OK)
                bret = BRLCAD_ERROR;
            db_close(wdbip);
        }

        if (bret != BRLCAD_OK) {
            ret = BRLCAD_ERROR;
            break;
        }

        wdbip = db_open(tmpwfile, DB_OPEN_READWRITE);
        if (!wdbip) { ret = BRLCAD_ERROR; break; }
        db_dirbuild(wdbip);
        db_update_nref(wdbip);
        ret = gcv_facetize_region_replace_root(wdbip, root_dp->d_namep, bu_vls_cstr(&result_name));
        db_close(wdbip);
        if (ret != BRLCAD_OK)
            break;
    }
    bu_vls_free(&result_name);
    bu_ptbl_free(&eval_roots);
    bu_ptbl_free(ir);
    bu_free(ir, "ir table");

    if (callbacks->use_variant_plan)
        callbacks->use_variant_plan(callback_data, 1);

    if (ret != BRLCAD_OK) {
        bu_ptbl_free(ar);
        bu_free(ar, "ar table");
        bu_free(dpa, "dp array");
        return BRLCAD_ERROR;
    }

    if (callbacks->region_summary)
        callbacks->region_summary(callback_data, eval_total);
    if (!make_nmg && !nmg_booleval && callbacks->primitive_summary)
        callbacks->primitive_summary(callback_data);

    const char **root_names = (const char **)bu_calloc(argc, sizeof(const char *), "root names");
    for (int i = 0; i < argc; i++)
        root_names[i] = argv[i];

    struct bu_vls prefix_str = BU_VLS_INIT_ZERO;
    struct bu_vls suffix_str = BU_VLS_INIT_ZERO;
    const char *affix = NULL;
    int use_prefix = 1;
    if (prefix && strlen(prefix))
        bu_vls_sprintf(&prefix_str, "%s", prefix);
    else
        bu_vls_sprintf(&prefix_str, "facetize_");
    if (suffix && strlen(suffix)) {
        bu_vls_sprintf(&suffix_str, "%s", suffix);
        use_prefix = 0;
    }
    affix = use_prefix ? bu_vls_cstr(&prefix_str) : bu_vls_cstr(&suffix_str);

    struct gcv_facetize_import_result import_result;
    BU_PTBL_INIT(&import_result.top_names);
    ret = gcv_facetize_import_working_regions(target_db, tmpwfile, argc, root_names, in_place, use_prefix, affix, &import_result);
    bu_free(root_names, "root names");
    bu_vls_free(&prefix_str);
    bu_vls_free(&suffix_str);
    if (ret != BRLCAD_OK) {
        gcv_facetize_import_result_free(&import_result);
        bu_ptbl_free(ar);
        bu_free(ar, "ar table");
        bu_free(dpa, "dp array");
        return BRLCAD_ERROR;
    }

    struct wmember wcomb;
    BU_LIST_INIT(&wcomb.l);
    struct rt_wdb *cwdbp = wdb_dbopen(target_db, RT_WDB_TYPE_DB_DEFAULT);
    for (size_t i = 0; i < BU_PTBL_LEN(&import_result.top_names); i++) {
        const char *tname = (const char *)BU_PTBL_GET(&import_result.top_names, i);
        if (tname)
            (void)mk_addmember(tname, &(wcomb.l), NULL, DB_OP_UNION);
    }
    mk_lcomb(cwdbp, output_name, &wcomb, 0, NULL, NULL, NULL, 0);
    gcv_facetize_import_result_free(&import_result);
    db_update_nref(target_db);

    if (callbacks->variant_summary)
        callbacks->variant_summary(callback_data);
    if (callbacks->cleanup)
        callbacks->cleanup(callback_data);

    bu_ptbl_free(ar);
    bu_free(ar, "ar table");
    bu_free(dpa, "dp array");
    return BRLCAD_OK;
}

void
gcv_facetize_import_result_free(struct gcv_facetize_import_result *result)
{
    if (!result)
        return;
    gcv_facetize_free_string_ptbl(&result->top_names);
}

int
gcv_facetize_import_working_regions(struct db_i *target_db,
                                    const char *working_file,
                                    int root_count,
                                    const char **root_names,
                                    int overwrite,
                                    int use_prefix,
                                    const char *affix,
                                    struct gcv_facetize_import_result *result)
{
    if (!target_db || !working_file || root_count <= 0 || !root_names)
        return BRLCAD_ERROR;

    if (result)
        BU_PTBL_INIT(&result->top_names);

    struct db_i *incoming_dbip = db_open(working_file, DB_OPEN_READONLY);
    if (!incoming_dbip)
        return BRLCAD_ERROR;
    if (db_version(incoming_dbip) != db_version(target_db)) {
        db_close(incoming_dbip);
        return BRLCAD_ERROR;
    }
    if (db_dirbuild(incoming_dbip) < 0) {
        db_close(incoming_dbip);
        return BRLCAD_ERROR;
    }

    std::set<std::string> closure;
    for (int i = 0; i < root_count; i++) {
        if (gcv_facetize_collect_closure(incoming_dbip, root_names[i], closure) != BRLCAD_OK) {
            db_close(incoming_dbip);
            return BRLCAD_ERROR;
        }
    }

    struct gcv_facetize_copy_data cc_data;
    cc_data.incoming_dbip = incoming_dbip;
    cc_data.target_dbip = target_db;
    cc_data.overwrite = overwrite;
    cc_data.suffix = use_prefix ? 0 : 1;
    cc_data.lazy_affix = overwrite ? 0 : 1;
    if (affix && !BU_STR_EQUAL(affix, "/"))
        cc_data.affix = std::string(affix);

    for (const auto &name : closure) {
        struct directory *dp = db_lookup(incoming_dbip, name.c_str(), LOOKUP_QUIET);
        if (!dp || dp->d_major_type == DB5_MAJORTYPE_ATTRIBUTE_ONLY)
            continue;
        if (gcv_facetize_uniq_name(dp->d_namep, &cc_data) != BRLCAD_OK) {
            db_close(incoming_dbip);
            return BRLCAD_ERROR;
        }
    }

    for (const auto &name : closure) {
        struct directory *dp = db_lookup(incoming_dbip, name.c_str(), LOOKUP_QUIET);
        if (!dp || dp->d_major_type == DB5_MAJORTYPE_ATTRIBUTE_ONLY)
            continue;
        if (gcv_facetize_copy_object(dp, &cc_data) != BRLCAD_OK) {
            db_close(incoming_dbip);
            return BRLCAD_ERROR;
        }
    }

    if (result) {
        for (int i = 0; i < root_count; i++) {
            std::string mapped = overwrite ? std::string(root_names[i]) : cc_data.name_map[std::string(root_names[i])];
            if (mapped.length())
                bu_ptbl_ins(&result->top_names, (long *)bu_strdup(mapped.c_str()));
        }
    }

    db_close(incoming_dbip);
    db_sync(target_db);
    db_update_nref(target_db);
    return BRLCAD_OK;
}

int
gcv_facetize_objects_to_db(struct db_i *db,
                           int argc,
                           const char **argv,
                           int in_place,
                           int make_nmg,
                           int nmg_booleval,
                           const struct gcv_facetize_object_callbacks *callbacks,
                           void *callback_data)
{
    if (!db || argc < 0 || !argv || !callbacks || !callbacks->validate_objects ||
            !callbacks->nmg_eval || !callbacks->manifold_eval)
        return BRLCAD_ERROR;

    if (argc == 0)
        return BRLCAD_ERROR;

    int ret = BRLCAD_ERROR;
    struct directory **dpa = (struct directory **)bu_calloc(argc, sizeof(struct directory *), "dp array");
    int newobj_cnt = gcv_facetize_sort_existing_objects(db, argc, argv, dpa);
    if (callbacks->validate_objects(callback_data, argc, argv, newobj_cnt) == BRLCAD_ERROR) {
        bu_free(dpa, "dp array");
        return BRLCAD_ERROR;
    }

    const char *output_name = NULL;
    if (!in_place) {
        output_name = argv[argc - 1];
        argc--;
    }

    if (make_nmg || nmg_booleval) {
        if (!in_place) {
            ret = callbacks->nmg_eval(callback_data, argc, argv, output_name);
        } else {
            for (int i = 0; i < argc; i++) {
                const char *av[2] = {argv[i], NULL};
                ret = callbacks->nmg_eval(callback_data, 1, av, av[0]);
                if (ret == BRLCAD_ERROR)
                    break;
            }
        }
        bu_free(dpa, "dp array");
        return ret;
    }

    if (!in_place) {
        ret = callbacks->manifold_eval(callback_data, argc, dpa, output_name, 0, 0);
    } else {
        for (int i = 0; i < argc; i++) {
            struct directory *idpa[2] = {dpa[i], NULL};
            ret = callbacks->manifold_eval(callback_data, 1, idpa, argv[i], 0, 0);
            if (ret == BRLCAD_ERROR)
                break;
        }
    }

    if (ret != BRLCAD_ERROR) {
        if (callbacks->primitive_summary)
            callbacks->primitive_summary(callback_data);
        if (callbacks->cleanup)
            callbacks->cleanup(callback_data);
    }

    bu_free(dpa, "dp array");
    return ret;
}

int
gcv_facetize_manifold_eval_to_db(struct db_i *eval_db,
                                 struct db_i *source_db,
                                 struct db_i *output_db,
                                 struct rt_wdb *wdbp,
                                 int argc,
                                 const char **object_names,
                                 const char *output_name,
                                 struct bu_list *vlfree,
                                 int no_empty,
                                 int disable_fixup,
                                 int verbosity,
                                 gcv_facetize_log_cb log_cb,
                                 void *log_ctx,
                                 gcv_facetize_variant_name_cb variant_cb,
                                 void *variant_ctx,
                                 gcv_facetize_bot_fixup_cb fixup_cb,
                                 void *fixup_ctx)
{
    struct gcv_facetize_manifold_state s;
    union tree *ftree;

    if (!eval_db || !output_db || !wdbp || !object_names || !output_name || !vlfree)
        return BRLCAD_ERROR;

    s.source_db = source_db;
    s.facetize_tree = NULL;
    s.error_flag = 0;
    s.verbosity = verbosity;
    s.log_cb = log_cb;
    s.log_ctx = log_ctx;
    s.variant_cb = variant_cb;
    s.variant_ctx = variant_ctx;

    if (verbosity >= 1) {
        if (argc == 1)
            bu_log("%s: evaluating booleans...\n", object_names[0]);
        else
            bu_log("Evaluating booleans for the trees of %d input objects...\n", argc);
    }

    bool do_fixup = false;
    if (argc == 1 && !disable_fixup) {
        struct directory *dp = db_lookup(eval_db, object_names[0], LOOKUP_QUIET);
        if (dp && ((dp->d_flags & RT_DIR_REGION) || (dp->d_flags & RT_DIR_SOLID)))
            do_fixup = true;
    }

    int ac = 0;
    const char **av = (const char **)bu_calloc(argc, sizeof(const char *), "av");
    for (int i = 0; i < argc; i++) {
        struct directory *dp = db_lookup(eval_db, object_names[i], LOOKUP_QUIET);
        if (dp && (dp->d_flags & RT_DIR_COMB || dp->d_flags & RT_DIR_SOLID)) {
            av[ac] = object_names[i];
            ac++;
        }
    }

    if (ac) {
        struct db_tree_state init_state;
        db_init_db_tree_state(&init_state, eval_db);
        init_state.ts_ttol = &wdbp->wdb_ttol;
        init_state.ts_tol = &wdbp->wdb_tol;
        init_state.ts_m = NULL;
        int i = 0;
        if (!BU_SETJUMP) {
            i = db_walk_tree(eval_db, argc, object_names,
                    1,
                    &init_state,
                    0,
                    gcv_facetize_region_end,
                    gcv_booltree_leaf_tess,
                    (void *)&s);
        } else {
            BU_UNSETJUMP;
            i = -1;
        } BU_UNSETJUMP;

        if (i < 0 || s.error_flag) {
            bu_free(av, "av");
            _gcv_facetize_manifold_log(&s, 0, "FAILED.\n");
            return BRLCAD_ERROR;
        }
    }
    bu_free(av, "av");

    if (!s.facetize_tree && !no_empty) {
        struct rt_bot_internal *bot = gcv_facetize_empty_bot();
        if (gcv_facetize_write_bot_to_db(output_db, bot, output_name, verbosity) != BRLCAD_OK) {
            _gcv_facetize_manifold_log(&s, 0, "FAILED.\n");
            return BRLCAD_ERROR;
        }
        _gcv_facetize_manifold_log(&s, 0, " Success.\n");
        return BRLCAD_OK;
    }

    ftree = rt_booltree_eval(s.facetize_tree, vlfree, &wdbp->wdb_tol, &gcv_manifold_do_bool, 0, (void *)&s);
    if (!ftree)
        return BRLCAD_ERROR;

    if (ftree->tr_d.td_d) {
        manifold::Manifold *om = (manifold::Manifold *)ftree->tr_d.td_d;
        if (om->Status() != manifold::Manifold::Error::NoError) {
            _gcv_facetize_manifold_log(&s, 0, "Boolean algorithm FAILED.\n");
            return BRLCAD_ERROR;
        }

        if (verbosity > 1) {
            bu_log("[FINAL_BOOL] obj=%s  final_mesh_SA=%.6f mm^2  num_verts=%zu  num_faces=%zu\n",
                   (argc > 0 && object_names && object_names[0]) ? object_names[0] : "?",
                   om->SurfaceArea(),
                   (size_t)om->GetMeshGL64().vertProperties.size() / 3,
                   (size_t)om->GetMeshGL64().triVerts.size() / 3);
        }

        manifold::MeshGL64 rmesh = om->GetMeshGL64();
        struct rt_bot_internal *bot;
        BU_GET(bot, struct rt_bot_internal);
        bot->magic = RT_BOT_INTERNAL_MAGIC;
        bot->mode = RT_BOT_SOLID;
        bot->orientation = RT_BOT_CCW;
        bot->thickness = NULL;
        bot->face_mode = (struct bu_bitv *)NULL;
        bot->bot_flags = 0;
        bot->num_vertices = (int)rmesh.vertProperties.size()/3;
        bot->num_faces = (int)rmesh.triVerts.size()/3;
        bot->vertices = (double *)calloc(rmesh.vertProperties.size(), sizeof(double));
        bot->faces = (int *)calloc(rmesh.triVerts.size(), sizeof(int));
        for (size_t j = 0; j < rmesh.vertProperties.size(); j++)
            bot->vertices[j] = rmesh.vertProperties[j];
        for (size_t j = 0; j < rmesh.triVerts.size(); j++)
            bot->faces[j] = rmesh.triVerts[j];

        double bot_vol = 0.0;
        if (bot->num_faces > 0 && bot->num_vertices > 0) {
            bot_vol = std::fabs(bg_trimesh_volume(bot->faces, bot->num_faces,
                              (const point_t *)bot->vertices,
                              bot->num_vertices));
        }
        double bbox_vol = gcv_bot_bbox_volume(bot);
        bool tiny_bot = (bbox_vol > 0.0) ?
            (bot_vol <= bbox_vol * GCV_FACETIZE_EMPTY_CHECK_REL_VOL_TOL) :
            (bot_vol <= GCV_FACETIZE_EMPTY_CHECK_ABS_VOL_TOL);
        bool is_single_input = (argc == 1 && object_names && object_names[0]);
        if (tiny_bot && is_single_input && source_db) {
            double csg_vol = -1.0;
            if (gcv_csg_crofton_volume(source_db, object_names[0], &csg_vol) == BRLCAD_OK) {
                double csg_abs = std::fabs(csg_vol);
                double csg_vtol = (bbox_vol > 0.0) ?
                    (bbox_vol * GCV_FACETIZE_EMPTY_CHECK_REL_VOL_TOL) :
                    GCV_FACETIZE_EMPTY_CHECK_ABS_VOL_TOL;
                if (csg_abs <= csg_vtol) {
                    rt_bot_internal_free(bot);
                    bot->magic = RT_BOT_INTERNAL_MAGIC;
                    bot->mode = RT_BOT_SOLID;
                    bot->orientation = RT_BOT_CCW;
                    bot->thickness = NULL;
                    bot->face_mode = (struct bu_bitv *)NULL;
                    bot->bot_flags = 0;
                }
            }
        }
        delete om;
        ftree->tr_d.td_d = NULL;

        if (gcv_facetize_write_bot_to_db(output_db, bot, output_name, verbosity) != BRLCAD_OK) {
            _gcv_facetize_manifold_log(&s, 0, "FAILED.\n");
            return BRLCAD_ERROR;
        }
    } else if (!no_empty) {
        struct rt_bot_internal *bot = gcv_facetize_empty_bot();
        if (gcv_facetize_write_bot_to_db(output_db, bot, output_name, verbosity) != BRLCAD_OK) {
            _gcv_facetize_manifold_log(&s, 0, "FAILED.\n");
            return BRLCAD_ERROR;
        }
        _gcv_facetize_manifold_log(&s, 0, "Success.\n");
        return BRLCAD_OK;
    }

    if (do_fixup && fixup_cb) {
        struct directory *dp = db_lookup(eval_db, object_names[0], LOOKUP_QUIET);
        if (dp && ((dp->d_flags & RT_DIR_REGION) || (!(dp->d_flags & RT_DIR_COMB)))) {
            struct directory *bot_dp = db_lookup(output_db, output_name, LOOKUP_QUIET);
            struct rt_bot_internal *nbot = fixup_cb(fixup_ctx, output_db, bot_dp, output_name);
            if (nbot) {
                db_delete(output_db, bot_dp);
                db_dirdelete(output_db, bot_dp);
                if (gcv_facetize_write_bot_to_db(output_db, nbot, output_name, verbosity) != BRLCAD_OK)
                    _gcv_facetize_manifold_log(&s, 0, "FAILED.\n");
            }
        }
    }

    _gcv_facetize_manifold_log(&s, 0, " Success.\n");
    return BRLCAD_OK;
}


int
gcv_facetize_manifold_objects_to_db(struct db_i *source_db,
                                    int argc,
                                    struct directory **dpa,
                                    const char *output_name,
                                    const char *working_file,
                                    const char *working_dir,
                                    int output_to_working,
                                    int cleanup,
                                    int make_nmg,
                                    int nmg_booleval,
                                    int no_perturb,
                                    int no_empty,
                                    int disable_fixup,
                                    int verbosity,
                                    struct bu_list *vlfree,
                                    gcv_facetize_log_cb log_cb,
                                    void *log_ctx,
                                    const struct gcv_facetize_manifold_object_callbacks *callbacks,
                                    void *callback_data,
                                    gcv_facetize_variant_name_cb variant_cb,
                                    void *variant_ctx,
                                    gcv_facetize_bot_fixup_cb fixup_cb,
                                    void *fixup_ctx)
{
    if (!source_db || !argc || !dpa || !output_name || !working_dir || !vlfree || !callbacks)
        return BRLCAD_ERROR;
    if (!callbacks->working_file_setup || !callbacks->primitive_tessellate)
        return BRLCAD_ERROR;

    const char *sfilter = "-type shape -or -type pnts";
    struct bu_ptbl leaf_dps = BU_PTBL_INIT_ZERO;
    if (db_search(&leaf_dps, DB_SEARCH_RETURN_UNIQ_DP, sfilter, argc, dpa, source_db, NULL, NULL, NULL) < 0)
        return BRLCAD_OK;

    int ret = BRLCAD_OK;
    if (callbacks->working_file_setup(callback_data, &leaf_dps) != BRLCAD_OK) {
        bu_ptbl_free(&leaf_dps);
        return BRLCAD_ERROR;
    }

    if (callbacks->variant_plan_reset)
        callbacks->variant_plan_reset(callback_data);
    if (!make_nmg && !nmg_booleval && !no_perturb && callbacks->variant_plan_build) {
        if (callbacks->variant_plan_build(callback_data, argc, dpa) != BRLCAD_OK) {
            bu_ptbl_free(&leaf_dps);
            return BRLCAD_ERROR;
        }
    }

    if (callbacks->primitive_tessellate(callback_data, source_db, &leaf_dps) != BRLCAD_OK) {
        bu_ptbl_free(&leaf_dps);
        return BRLCAD_ERROR;
    }

    if (callbacks->variant_tessellate)
        (void)callbacks->variant_tessellate(callback_data);

    const char *active_working_file = working_file;
    if (callbacks->working_file)
        active_working_file = callbacks->working_file(callback_data);
    if (!active_working_file || !active_working_file[0]) {
        bu_ptbl_free(&leaf_dps);
        return BRLCAD_ERROR;
    }

    struct db_i *wdbip = db_open(active_working_file, output_to_working ? DB_OPEN_READWRITE : DB_OPEN_READONLY);
    if (!wdbip) {
        bu_ptbl_free(&leaf_dps);
        bu_dirclear(working_dir);
        return BRLCAD_ERROR;
    }
    if (db_dirbuild(wdbip) < 0) {
        db_close(wdbip);
        bu_ptbl_free(&leaf_dps);
        return BRLCAD_ERROR;
    }
    db_update_nref(wdbip);

    struct rt_wdb *wwdbp = wdb_dbopen(wdbip, RT_WDB_TYPE_DB_DEFAULT);
    const char **object_names = (const char **)bu_calloc(argc + 1, sizeof(char *), "object names");
    for (int i = 0; i < argc; i++)
        object_names[i] = dpa[i]->d_namep;

    struct db_i *output_db = output_to_working ? wdbip : source_db;
    if (gcv_facetize_manifold_eval_to_db(wdbip,
            source_db,
            output_db,
            wwdbp,
            argc,
            object_names,
            output_name,
            vlfree,
            no_empty,
            disable_fixup,
            verbosity,
            log_cb,
            log_ctx,
            variant_cb,
            variant_ctx,
            fixup_cb,
            fixup_ctx) != BRLCAD_OK) {
        if (verbosity >= 0) {
            if (log_cb) {
                struct bu_vls msg = BU_VLS_INIT_ZERO;
                bu_vls_printf(&msg, "FACETIZE: failed to generate %s\n", output_name);
                log_cb(log_ctx, 0, bu_vls_cstr(&msg));
                bu_vls_free(&msg);
            } else {
                bu_log("FACETIZE: failed to generate %s\n", output_name);
            }
        }
    }

    bu_free(object_names, "object names");
    db_close(wdbip);

    if (cleanup)
        bu_dirclear(working_dir);

    bu_ptbl_free(&leaf_dps);
    return ret;
}
