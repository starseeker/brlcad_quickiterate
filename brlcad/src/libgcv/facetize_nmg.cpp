/*                    F A C E T I Z E _ N M G . C P P
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
/** @file libgcv/facetize_nmg.cpp
 *
 * Classic NMG boolean evaluation path for facetization.
 */

#include "common.h"

#include <string.h>
#include <stdarg.h>

#include "bio.h"

#include "bu/hook.h"
#include "bu/log.h"
#include "bu/vls.h"
#include "gcv/facetize.h"
#include "raytrace.h"
#include "rt/db_internal.h"
#include "rt/functab.h"
#include "rt/global.h"
#include "rt/nmg_conv.h"
#include "rt/wdb.h"

struct gcv_facetize_nmg_log_state {
    struct bu_hook_list *saved_bomb_hooks;
    struct bu_hook_list *saved_log_hooks;
    struct bu_vls nmg_log;
    struct bu_vls nmg_log_header;
    int nmg_log_print_header;
    int stderr_stashed;
    int serr;
    int fnull;
};

struct gcv_facetize_nmg_state {
    struct db_i *dbip;
    gcv_facetize_log_cb log_cb;
    void *log_ctx;
    int verbosity;
    struct gcv_facetize_nmg_log_state *log_s;
};

static void
_gcv_facetize_nmg_log(struct gcv_facetize_nmg_state *s, int level, const char *fmt, ...)
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

extern "C" {
static int
_gcv_facetize_bomb_hook(void *cdata, void *str)
{
    struct gcv_facetize_nmg_state *s = (struct gcv_facetize_nmg_state *)cdata;
    struct gcv_facetize_nmg_log_state *log_s = s ? s->log_s : NULL;
    if (!log_s)
        return 0;
    if (log_s->nmg_log_print_header) {
        bu_vls_printf(&log_s->nmg_log, "%s\n", bu_vls_addr(&log_s->nmg_log_header));
        log_s->nmg_log_print_header = 0;
    }
    bu_vls_printf(&log_s->nmg_log, "%s\n", (const char *)str);
    return 0;
}

static int
_gcv_facetize_nmg_logging_hook(void *data, void *str)
{
    struct gcv_facetize_nmg_state *s = (struct gcv_facetize_nmg_state *)data;
    struct gcv_facetize_nmg_log_state *log_s = s ? s->log_s : NULL;
    if (!log_s)
        return 0;
    if (log_s->nmg_log_print_header) {
        bu_vls_printf(&log_s->nmg_log, "%s\n", bu_vls_addr(&log_s->nmg_log_header));
        log_s->nmg_log_print_header = 0;
    }
    bu_vls_printf(&log_s->nmg_log, "%s\n", (const char *)str);
    return 0;
}
}

static void
_gcv_facetize_log_nmg(struct gcv_facetize_nmg_state *s)
{
    struct gcv_facetize_nmg_log_state *log_s = s ? s->log_s : NULL;

    if (!log_s || fileno(stderr) < 0)
        return;

    log_s->fnull = open("/dev/null", O_WRONLY);
    if (log_s->fnull == -1)
        log_s->fnull = open("nul", O_WRONLY);
    if (log_s->fnull != -1) {
        log_s->serr = fileno(stderr);
        log_s->stderr_stashed = dup(log_s->serr);
        dup2(log_s->fnull, log_s->serr);
        close(log_s->fnull);
    }

    bu_log_hook_delete_all();
    bu_log_add_hook(_gcv_facetize_nmg_logging_hook, (void *)s);

    bu_bomb_delete_all_hooks();
    bu_bomb_add_hook(_gcv_facetize_bomb_hook, (void *)s);
}

static void
_gcv_facetize_log_default(struct gcv_facetize_nmg_state *s)
{
    struct gcv_facetize_nmg_log_state *log_s = s ? s->log_s : NULL;

    if (!log_s || fileno(stderr) < 0)
        return;

    if (log_s->fnull != -1) {
        fflush(stderr);
        dup2(log_s->stderr_stashed, log_s->serr);
        close(log_s->stderr_stashed);
        log_s->fnull = -1;
    }

    bu_bomb_delete_all_hooks();
    bu_bomb_restore_hooks(log_s->saved_bomb_hooks);

    bu_log_hook_delete_all();
    bu_log_hook_restore_all(log_s->saved_log_hooks);
}

static union tree *
gcv_facetize_nmg_region_end(struct db_tree_state *tsp,
                            const struct db_full_path *pathp,
                            union tree *curtree,
                            void *client_data)
{
    union tree **facetize_tree;

    if (tsp) RT_CK_DBTS(tsp);
    if (pathp) RT_CK_FULL_PATH(pathp);

    facetize_tree = (union tree **)client_data;

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

static void
_gcv_facetize_nmg_log_state_free(struct gcv_facetize_nmg_state *s)
{
    struct gcv_facetize_nmg_log_state *log_s = s ? s->log_s : NULL;

    if (!log_s)
        return;

    bu_vls_free(&log_s->nmg_log_header);
    bu_vls_free(&log_s->nmg_log);
    BU_PUT(log_s, struct gcv_facetize_nmg_log_state);
    s->log_s = NULL;
}

static struct model *
gcv_facetize_try_nmg(struct gcv_facetize_nmg_state *s, struct bu_list *vlfree, int argc, const char **argv)
{
    struct db_i *dbip = s->dbip;
    int i;
    int failed = 0;
    struct db_tree_state init_state;
    union tree *facetize_tree;
    struct model *nmg_model;
    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_DEFAULT);
    struct gcv_facetize_nmg_log_state *log_s;

    BU_GET(log_s, struct gcv_facetize_nmg_log_state);
    log_s->saved_bomb_hooks = NULL;
    log_s->saved_log_hooks = NULL;
    bu_vls_init(&log_s->nmg_log);
    bu_vls_init(&log_s->nmg_log_header);
    log_s->nmg_log_print_header = 0;
    log_s->stderr_stashed = -1;
    log_s->serr = -1;
    log_s->fnull = -1;
    s->log_s = log_s;

    _gcv_facetize_log_nmg(s);

    db_init_db_tree_state(&init_state, dbip);
    init_state.ts_ttol = &wdbp->wdb_ttol;
    init_state.ts_tol = &wdbp->wdb_tol;

    facetize_tree = (union tree *)0;
    nmg_model = nmg_mm();
    init_state.ts_m = &nmg_model;

    if (!BU_SETJUMP) {
        i = db_walk_tree(dbip, argc, (const char **)argv,
                1,
                &init_state,
                0,
                gcv_facetize_nmg_region_end,
                rt_booltree_leaf_tess,
                (void *)&facetize_tree);
    } else {
        BU_UNSETJUMP;
        _gcv_facetize_log_default(s);
        _gcv_facetize_nmg_log_state_free(s);
        return NULL;
    } BU_UNSETJUMP;

    if (i < 0) {
        _gcv_facetize_log_default(s);
        _gcv_facetize_nmg_log_state_free(s);
        return NULL;
    }

    if (facetize_tree) {
        if (!BU_SETJUMP) {
            failed = nmg_boolean(facetize_tree, nmg_model, vlfree, &wdbp->wdb_tol);
        } else {
            BU_UNSETJUMP;
            _gcv_facetize_log_default(s);
            _gcv_facetize_nmg_log_state_free(s);
            return NULL;
        } BU_UNSETJUMP;
    } else {
        failed = 1;
    }

    if (!failed && facetize_tree) {
        NMG_CK_REGION(facetize_tree->tr_d.td_r);
        facetize_tree->tr_d.td_r = (struct nmgregion *)NULL;
    }

    if (facetize_tree)
        db_free_tree(facetize_tree);

    _gcv_facetize_log_default(s);
    _gcv_facetize_nmg_log_state_free(s);
    return (failed) ? NULL : nmg_model;
}

static int
gcv_facetize_write_nmg(struct gcv_facetize_nmg_state *s, struct model *nmg_model, const char *name)
{
    struct rt_db_internal intern;
    struct directory *dp;

    RT_DB_INTERNAL_INIT(&intern);
    intern.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    intern.idb_type = ID_NMG;
    intern.idb_meth = &OBJ[ID_NMG];
    intern.idb_ptr = (void *)nmg_model;

    dp = db_diradd(s->dbip, name, RT_DIR_PHONY_ADDR, 0, RT_DIR_SOLID, (void *)&intern.idb_type);
    if (dp == RT_DIR_NULL) {
        if (s->verbosity > 0)
            _gcv_facetize_nmg_log(s, 0, "Cannot add %s to directory\n", name);
        return BRLCAD_ERROR;
    }

    if (rt_db_put_internal(dp, s->dbip, &intern) < 0) {
        if (s->verbosity > 0)
            _gcv_facetize_nmg_log(s, 0, "Failed to write %s to database\n", name);
        rt_db_free_internal(&intern);
        return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}

static int
gcv_facetize_write_bot_to_db(struct gcv_facetize_nmg_state *s, struct rt_bot_internal *bot, const char *name)
{
    struct rt_db_internal intern;
    struct directory *dp;

    RT_DB_INTERNAL_INIT(&intern);
    intern.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    intern.idb_type = ID_BOT;
    intern.idb_meth = &OBJ[ID_BOT];
    intern.idb_ptr = (void *)bot;

    dp = db_diradd(s->dbip, name, RT_DIR_PHONY_ADDR, 0, RT_DIR_SOLID, (void *)&intern.idb_type);
    if (dp == RT_DIR_NULL) {
        if (s->verbosity > 0)
            _gcv_facetize_nmg_log(s, 0, "Cannot add %s to directory\n", name);
        return BRLCAD_ERROR;
    }

    if (rt_db_put_internal(dp, s->dbip, &intern) < 0) {
        if (s->verbosity > 0)
            _gcv_facetize_nmg_log(s, 0, "Failed to write %s to database\n", name);
        rt_db_free_internal(&intern);
        return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}

int
gcv_facetize_nmg_eval_to_db(struct db_i *db,
                            int argc,
                            const char **object_names,
                            const char *output_name,
                            int make_nmg,
                            int verbosity,
                            gcv_facetize_log_cb log_cb,
                            void *log_ctx)
{
    int ret = BRLCAD_OK;
    struct rt_wdb *wdbp;
    struct rt_bot_internal *bot = NULL;
    struct bu_list *vlfree = &rt_vlfree;
    struct gcv_facetize_nmg_state s;
    struct model *nmg_model = NULL;

    if (!db || argc < 1 || !object_names || !output_name)
        return BRLCAD_ERROR;

    s.dbip = db;
    s.log_cb = log_cb;
    s.log_ctx = log_ctx;
    s.verbosity = verbosity;
    s.log_s = NULL;

    nmg_model = gcv_facetize_try_nmg(&s, vlfree, argc, object_names);
    if (nmg_model == NULL) {
        if (verbosity > 1)
            _gcv_facetize_nmg_log(&s, 0, "NMG(%s):  no resulting region, aborting\n", output_name);
        ret = BRLCAD_ERROR;
        goto nmg_obj_memfree;
    }

    if (!make_nmg) {
        wdbp = wdb_dbopen(db, RT_WDB_TYPE_DB_DEFAULT);
        if (!BU_SETJUMP) {
            bot = (struct rt_bot_internal *)nmg_mdl_to_bot(nmg_model, vlfree, &wdbp->wdb_tol);
        } else {
            BU_UNSETJUMP;
            return BRLCAD_ERROR;
        } BU_UNSETJUMP;

        ret = gcv_facetize_write_bot_to_db(&s, bot, output_name);
    } else {
        ret = gcv_facetize_write_nmg(&s, nmg_model, output_name);
    }

nmg_obj_memfree:
    if (verbosity >= 0 && ret != BRLCAD_OK)
        _gcv_facetize_nmg_log(&s, 0, "NMG: failed to generate %s\n", output_name);

    return ret;
}
