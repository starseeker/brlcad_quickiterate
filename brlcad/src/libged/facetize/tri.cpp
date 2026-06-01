/*                         T R I . C P P
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
/** @file libged/facetize/tri_booleval.cpp
 *
 * Triangle centric boolean evaluation logic using Manifold library.
 */

#include "common.h"

#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <queue>

#include <string.h>
#include "bu/app.h"
#include "bu/path.h"
#include "bu/snooze.h"
#include "bu/time.h"
#include "gcv/facetize.h"
#include "../ged_private.h"
#include "./ged_facetize.h"

#define CMD_LEN_MAX 8000

void
_ged_facetize_process_log(void *ctx, int verbosity, const char *msg)
{
    struct _ged_facetize_state *s = (struct _ged_facetize_state *)ctx;

    if (!s || !msg)
        return;

    facetize_log(s, verbosity, "%s", msg);
}

/*
 * Tessellate variant primitives that were created by _ged_facetize_build_variant_plan().
 * Processes all names using the NMG method (same fixed command structure as
 * _ged_facetize_leaves_tri).  Tessellation failures are logged but do not
 * abort: the booleval will silently fall back to the original (non-variant)
 * mesh for any variant whose BoT is not available.
 */
int
_ged_facetize_tessellate_variant_names(struct _ged_facetize_state *s,
				       FacetizeVariantPlan *plan)
{
    if (!s || !plan || plan->variant_names.empty())
	return BRLCAD_OK;

    char lcache[MAXPATHLEN] = {0};
    bu_dir(lcache, MAXPATHLEN, BU_DIR_CACHE, NULL);

    const struct gcv_facetize_method_opts_state *mo = (const struct gcv_facetize_method_opts_state *)s->method_opts;
    std::vector<const char *> object_names;
    for (size_t i = 0; i < plan->variant_names.size(); i++)
	object_names.push_back(plan->variant_names[i].c_str());

    const char *methods[1] = {"NMG"};
    int fail_cnt = gcv_facetize_process_run_methods(bu_vls_cstr(s->wfile),
	    1,
	    methods,
	    1,
	    mo,
	    s->dbip,
	    0,
	    lcache,
	    object_names.data(),
	    object_names.size(),
	    MAXPATHLEN,
	    CMD_LEN_MAX,
	    0,
	    1,
	    NULL,
	    _ged_facetize_process_log,
	    (void *)s);
    if (fail_cnt < 0)
	fail_cnt = (int)object_names.size();
    if (fail_cnt > 0)
	facetize_log(s, 0, "FACETIZE: variant tessellation failed for %d object(s)\n", fail_cnt);

    plan->n_variant_tess_failures = fail_cnt;
    return (fail_cnt == 0) ? BRLCAD_OK : BRLCAD_ERROR;
}


class DpCompare
{
    public:
	bool operator()(struct directory *dp1, struct directory *dp2) {
	    // C++ priority queues return the largest element, but
	    // we want to start with the smaller elements - so we
	    // invert the large/small reporting
	    return (dp1->d_len > dp2->d_len);
	}
};

int
_ged_facetize_leaves_tri(struct _ged_facetize_state *s, struct db_i *dbip, struct bu_ptbl *leaf_dps)
{
    // Sort dp objects by d_len using a priority queue
    std::priority_queue<struct directory *, std::vector<struct directory *>, DpCompare> pq;
    std::queue<struct directory *> q_dsp;
    std::priority_queue<struct directory *, std::vector<struct directory *>, DpCompare> q_pbot;
    for (size_t i = 0; i < BU_PTBL_LEN(leaf_dps); i++) {
	struct directory *ldp = (struct directory *)BU_PTBL_GET(leaf_dps, i);

	// If this isn't a proper BRL-CAD object, tessellation is a no-op
	if (ldp->d_major_type != DB5_MAJORTYPE_BRLCAD)
	    continue;

	// Plate mode bots only have a realistic chance of being handled by
	// the plate to vol conversion method, but they can be quite slow
	// and will run into max-time limitations if they are large.  Separate
	// the large ones out - we will treat their handling like a fallback method and
	// be more tolerant of time
	if (ldp->d_minor_type == ID_BOT) {
	    struct rt_db_internal intern;
	    RT_DB_INTERNAL_INIT(&intern);
	    if (rt_db_get_internal(&intern, ldp, dbip, NULL) < 0) {
		pq.push(ldp);
		continue;
	    }
	    struct rt_bot_internal *bot = (struct rt_bot_internal *)(intern.idb_ptr);
	    int propVal = (int)rt_bot_propget(bot, "type");
	    // Plate mode BoTs need an explicit volume representation
	    if (propVal == RT_BOT_PLATE || propVal == RT_BOT_PLATE_NOCOS) {
		q_pbot.push(ldp);
		continue;
	    }
	}

	// Standard case
	pq.push(ldp);
    }

    if (pq.empty() && q_dsp.empty() && q_pbot.empty()) {
	bu_log("Note: no viable objects for tessellation found.\n");
	return BRLCAD_OK;
    }

    const struct gcv_facetize_method_opts_state *mo = (const struct gcv_facetize_method_opts_state *)s->method_opts;
    std::queue<std::string> method_flags;
    std::queue<std::string> method_flags_bak;
    {
	std::vector<const char *> req_methods;
	struct gcv_facetize_opts fopts;
	struct bu_ptbl resolved_names = BU_PTBL_INIT_ZERO;

	gcv_facetize_opts_default(&fopts);
	size_t method_cnt = gcv_facetize_method_opts_method_count(mo);
	for (size_t i = 0; i < method_cnt; i++) {
	    const char *method = gcv_facetize_method_opts_method_name(mo, i);
	    if (method)
		req_methods.push_back(method);
	}
	fopts.methods = req_methods.size() ? req_methods.data() : NULL;
	fopts.method_count = req_methods.size();
	if (gcv_facetize_resolved_method_names(&fopts, &resolved_names) != BRLCAD_OK) {
	    bu_avs_free(&fopts.method_options);
	    bu_log("No methods for tessellation found.\n");
	    bu_dirclear(s->wdir);
	    return BRLCAD_ERROR;
	}
	bu_avs_free(&fopts.method_options);

	for (size_t i = 0; i < BU_PTBL_LEN(&resolved_names); i++) {
	    const char *mname = (const char *)BU_PTBL_GET(&resolved_names, i);
	    if (!mname)
		continue;
	    method_flags.push(std::string(mname));
	}
	gcv_facetize_free_method_names(&resolved_names);
    }

    method_flags_bak = method_flags;

    std::vector<std::string> method_name_storage;
    {
	std::queue<std::string> mqueue = method_flags_bak;
	while (!mqueue.empty()) {
	    method_name_storage.push_back(mqueue.front());
	    mqueue.pop();
	}
    }
    std::vector<const char *> method_names;
    for (size_t i = 0; i < method_name_storage.size(); i++)
	method_names.push_back(method_name_storage[i].c_str());

    // We want the subprocess to be using the same cache directory
    // as the parent
    char lcache[MAXPATHLEN] = {0};
    bu_dir(lcache, MAXPATHLEN, BU_DIR_CACHE, NULL);

    std::vector<std::string> failed_dps;

    if (!pq.empty()) {
	std::vector<const char *> object_names;
	while (!pq.empty()) {
	    object_names.push_back(pq.top()->d_namep);
	    pq.pop();
	}

	struct bu_ptbl bad_names = BU_PTBL_INIT_ZERO;
	int err_cnt = gcv_facetize_process_run_methods(bu_vls_cstr(s->wfile),
		1,
		method_names.data(),
		method_names.size(),
		mo,
		dbip,
		0,
		lcache,
		object_names.data(),
		object_names.size(),
		MAXPATHLEN,
		CMD_LEN_MAX,
		0,
		0,
		&bad_names,
		_ged_facetize_process_log,
		(void *)s);
	if (err_cnt < 0) {
	    gcv_facetize_free_string_ptbl(&bad_names);
	    return BRLCAD_ERROR;
	}
	for (size_t i = 0; i < BU_PTBL_LEN(&bad_names); i++) {
	    const char *bad_name = (const char *)BU_PTBL_GET(&bad_names, i);
	    if (bad_name)
		failed_dps.push_back(std::string(bad_name));
	}
	gcv_facetize_free_string_ptbl(&bad_names);
    }

    if (!q_dsp.empty()) {
	std::vector<const char *> object_names;
	while (!q_dsp.empty()) {
	    object_names.push_back(q_dsp.front()->d_namep);
	    q_dsp.pop();
	}
	const char *cm_methods[1] = {"CM"};
	struct bu_ptbl bad_names = BU_PTBL_INIT_ZERO;
	int err_cnt = gcv_facetize_process_run_methods(bu_vls_cstr(s->wfile),
		1,
		cm_methods,
		1,
		mo,
		dbip,
		0,
		lcache,
		object_names.data(),
		object_names.size(),
		MAXPATHLEN,
		CMD_LEN_MAX,
		0,
		0,
		&bad_names,
		_ged_facetize_process_log,
		(void *)s);
	if (err_cnt < 0) {
	    gcv_facetize_free_string_ptbl(&bad_names);
	    return BRLCAD_ERROR;
	}
	for (size_t i = 0; i < BU_PTBL_LEN(&bad_names); i++) {
	    const char *bad_name = (const char *)BU_PTBL_GET(&bad_names, i);
	    if (bad_name)
		failed_dps.push_back(std::string(bad_name));
	}
	gcv_facetize_free_string_ptbl(&bad_names);
    }

    if (!q_pbot.empty()) {
	std::vector<const char *> object_names;
	while (!q_pbot.empty()) {
	    object_names.push_back(q_pbot.top()->d_namep);
	    q_pbot.pop();
	}
	const char *pbot_methods[1] = {"NMG"};
	int err_cnt = gcv_facetize_process_run_methods(bu_vls_cstr(s->wfile),
		1,
		pbot_methods,
		1,
		mo,
		dbip,
		0,
		lcache,
		object_names.data(),
		object_names.size(),
		MAXPATHLEN,
		CMD_LEN_MAX,
		1,
		1,
		NULL,
		_ged_facetize_process_log,
		(void *)s);
	if (err_cnt) {
	    // If we couldn't handle the plate mode conversion, we can't do the
	    // boolean evaluation
	    facetize_log(s, 0, "Plate mode conversion wasn't able to complete\n");
	    return BRLCAD_ERROR;
	}
    }

    if (failed_dps.size()) {
	// As the parent process, we can know when we've run out of options
       // to try.  If we get there, flag the solid in the working copy so
       // the summary knows to report it.
       struct db_i *cdbip = db_open(bu_vls_cstr(s->wfile), DB_OPEN_READWRITE);
       if (cdbip) {
           db_dirbuild(cdbip);
           db_update_nref(cdbip);
           for (size_t i = 0; i < failed_dps.size(); i++) {
	       struct directory *dp = db_lookup(cdbip, failed_dps[i].c_str(), LOOKUP_QUIET);
	       if (!dp)
		   continue;
               struct bu_attribute_value_set avs = BU_AVS_INIT_ZERO;
               db5_get_attributes(cdbip, &avs, dp);
               (void)bu_avs_add(&avs, FACETIZE_METHOD_ATTR, "FAIL");
               (void)db5_update_attributes(dp, &avs, cdbip);
           }
           db_close(cdbip);
       }
       return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}

static const char *
_ged_facetize_variant_name(void *ctx, const char *path, int is_subtractive)
{
    struct _ged_facetize_state *s = (struct _ged_facetize_state *)ctx;

    if (!s || !s->use_variant_plan || !s->variant_plan || !path)
        return NULL;

    FacetizeVariantPlan *vplan = (FacetizeVariantPlan *)s->variant_plan;
    std::string role_key = std::string(path) + (is_subtractive ? "#sub" : "#base");
    auto it = vplan->inst_to_variant.find(role_key);
    if (it == vplan->inst_to_variant.end())
        return NULL;

    return it->second.c_str();
}

static struct rt_bot_internal *
_ged_facetize_bot_fixup_cb(void *ctx, struct db_i *wdbip, struct directory *bot_dp, const char *bname)
{
    struct _ged_facetize_state *s = (struct _ged_facetize_state *)ctx;

    if (!s)
        return NULL;

    return bot_fixup(s, wdbip, bot_dp, bname);
}

static int
_ged_facetize_working_file_setup_cb(void *ctx, struct bu_ptbl *leaf_dps)
{
    return _ged_facetize_working_file_setup((struct _ged_facetize_state *)ctx, leaf_dps);
}

static const char *
_ged_facetize_working_file_cb(void *ctx)
{
    struct _ged_facetize_state *s = (struct _ged_facetize_state *)ctx;
    return s ? bu_vls_cstr(s->wfile) : NULL;
}

static void
_ged_facetize_variant_plan_reset_cb(void *ctx)
{
    struct _ged_facetize_state *s = (struct _ged_facetize_state *)ctx;
    if (!s || !s->variant_plan)
        return;
    delete (FacetizeVariantPlan *)s->variant_plan;
    s->variant_plan = NULL;
}

static int
_ged_facetize_variant_plan_build_cb(void *ctx, int argc, struct directory **dpa)
{
    struct _ged_facetize_state *s = (struct _ged_facetize_state *)ctx;
    if (!s)
        return BRLCAD_ERROR;

    FacetizeVariantPlan *vplan = _ged_facetize_build_variant_plan(s, argc, dpa);
    s->variant_plan = (void *)vplan;
    return BRLCAD_OK;
}

static int
_ged_facetize_primitive_tessellate_cb(void *ctx, struct db_i *dbip, struct bu_ptbl *leaf_dps)
{
    return _ged_facetize_leaves_tri((struct _ged_facetize_state *)ctx, dbip, leaf_dps);
}

static int
_ged_facetize_variant_tessellate_cb(void *ctx)
{
    struct _ged_facetize_state *s = (struct _ged_facetize_state *)ctx;
    if (!s || !s->variant_plan)
        return BRLCAD_OK;

    FacetizeVariantPlan *vplan = (FacetizeVariantPlan *)s->variant_plan;
    if (vplan->variant_names.empty())
        return BRLCAD_OK;
    return _ged_facetize_tessellate_variant_names(s, vplan);
}

int
_ged_facetize_booleval_tri(struct _ged_facetize_state *s, struct db_i *dbip, struct rt_wdb *wdbp, int argc, const char **argv, const char *oname, struct bu_list *vlfree, bool output_to_working, int curr_cnt, int total_cnt)
{
    if (!s || !dbip || !wdbp || !argv || !oname)
        return BRLCAD_ERROR;

    if (total_cnt < 0) {
        facetize_log(s, 0, "Processing %s [%d perturb]...", oname, curr_cnt);
    } else if (total_cnt == 0) {
        facetize_log(s, 0, "Processing %s...", oname);
    } else {
        facetize_log(s, 0, "Processing %s [%d of %d]...", oname, curr_cnt, total_cnt);
    }

    struct db_i *odbip = (output_to_working) ? dbip : s->dbip;
    return gcv_facetize_manifold_eval_to_db(dbip,
            s->dbip,
            odbip,
            wdbp,
            argc,
            argv,
            oname,
            vlfree,
            s->no_empty,
            s->no_fixup,
            s->verbosity,
            _ged_facetize_process_log,
            (void *)s,
            _ged_facetize_variant_name,
            (void *)s,
            _ged_facetize_bot_fixup_cb,
            (void *)s);
}

int
_ged_facetize_booleval(struct _ged_facetize_state *s, int argc, struct directory **dpa, const char *oname, bool output_to_working, bool cleanup)
{
    struct bu_list *vlfree = &rt_vlfree;

    if (!s)
        return BRLCAD_ERROR;

    struct gcv_facetize_manifold_object_callbacks callbacks = {};
    callbacks.working_file_setup = _ged_facetize_working_file_setup_cb;
    callbacks.working_file = _ged_facetize_working_file_cb;
    callbacks.variant_plan_reset = _ged_facetize_variant_plan_reset_cb;
    callbacks.variant_plan_build = _ged_facetize_variant_plan_build_cb;
    callbacks.primitive_tessellate = _ged_facetize_primitive_tessellate_cb;
    callbacks.variant_tessellate = _ged_facetize_variant_tessellate_cb;

    return gcv_facetize_manifold_objects_to_db(s->dbip,
            argc,
            dpa,
            oname,
            bu_vls_cstr(s->wfile),
            s->wdir,
            output_to_working ? 1 : 0,
            cleanup ? 1 : 0,
            s->make_nmg,
            s->nmg_booleval,
            s->no_perturb,
            s->no_empty,
            s->no_fixup,
            s->verbosity,
            vlfree,
            _ged_facetize_process_log,
            (void *)s,
            &callbacks,
            (void *)s,
            _ged_facetize_variant_name,
            (void *)s,
            _ged_facetize_bot_fixup_cb,
            (void *)s);
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
