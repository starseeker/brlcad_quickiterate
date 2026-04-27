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

#include <chrono>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iostream>
#include <fstream>
#include <queue>
#include <thread>

#include <string.h>

#include "manifold/manifold.h"

#include "bg/trimesh.h"
#include "bu/app.h"
#include "bu/path.h"
#include "bu/snooze.h"
#include "bu/time.h"
#include "../ged_private.h"
#include "./ged_facetize.h"
#include "./tess_opts.h"
#include "./subprocess.h"

static const size_t FACETIZE_EMPTY_CHECK_CROFTON_RAYS = 800u;
static const double FACETIZE_EMPTY_CHECK_REL_VOL_TOL = 1.0e-9;
static const double FACETIZE_EMPTY_CHECK_ABS_VOL_TOL = 1.0e-12;
static const size_t FACETIZE_PROGRESS_INTERVAL = 25;

static int
bot_to_manifold(void **out, struct db_tree_state *tsp, struct rt_db_internal *ip, int flip)
{
    if (!out || !tsp || !ip)
	return BRLCAD_ERROR;

    // By this point all leaves should be bots
    if (ip->idb_minor_type != ID_BOT)
	return BRLCAD_ERROR;

    struct rt_bot_internal *nbot = (struct rt_bot_internal *)ip->idb_ptr;

    if (!nbot->num_vertices) {
	// Trivial case
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

    // NOTE -  if long-thin-dense triangle fans end up causing super-long
    // evaluation times here the same way we did in plate mode extrusion, we
    // could try the preliminary decimation criteria we use there on volumetric
    // inputs as well.  Waiting on that until we see a real-world need to
    // justify it, since we would have to support the parameters bot extrude
    // needs here as well.
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
    if (bot_manifold.Status() != manifold::Manifold::Error::NoError) {
	// Urk - we got a mesh, but it's no good for a Manifold(??)
	return BRLCAD_ERROR;
    }

    // Passed - return the manifold
    (*out) = new manifold::Manifold(bot_manifold);
    return 0;
}

// We need to see if the matrix is turning the BoT inside out.  Make a
// test face, with a setup that will report non-flipping with an IDN
// matrix, and see what the currently active matrix does to it.
static int bot_flipped(mat_t *m)
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

    fastf_t vedot = VDOT(ecross, raydir);
    if (vedot > 0)
	return 1;

    return 0;
}

static double
bot_bbox_volume(const struct rt_bot_internal *bot)
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
csg_crofton_volume(struct db_i *dbip, const char *obj_name, double *out_vol)
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
    struct rt_crofton_params crp = {FACETIZE_EMPTY_CHECK_CROFTON_RAYS, 0.0, 0.0};
    int rc = rt_crofton_shoot(rtip, &crp, &sa, &vol);
    rt_free_rti(rtip);
    if (rc < 0)
	return BRLCAD_ERROR;
    *out_vol = vol;
    return BRLCAD_OK;
}

// Customized version of rt_booltree_leaf_tess for Manifold processing
static union tree *
_booltree_leaf_tess(struct db_tree_state *tsp, const struct db_full_path *pathp, struct rt_db_internal *ip, void *data)
{
    int ts_status = 0;
    union tree *curtree;
    struct directory *dp;

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

    // Infinite half spaces get special handling in the boolean evaluation
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

    // Anything else that's not a BoT is a no-op for booleans
    if (ip->idb_minor_type != ID_BOT)
	return curtree;

    // Observed in Goliath example model with SKTRACKdrivewheel2.c comb - due
    // to the values in ts_mat, the BoT ends up inside-out when read in.
    int flip = bot_flipped(&tsp->ts_mat);

    // Phase C: variant BoT override.
    // If a perturbed variant was pre-tessellated for this leaf instance, use
    // it instead of the original BoT to avoid coplanar face issues.
    struct rt_db_internal var_intern;
    RT_DB_INTERNAL_INIT(&var_intern);
    bool var_loaded = false;
    struct rt_db_internal *effective_ip = ip;
    struct _ged_facetize_state *s = (struct _ged_facetize_state *)data;
    if (s && s->use_variant_plan && s->variant_plan) {
	FacetizeVariantPlan *vplan = (FacetizeVariantPlan *)s->variant_plan;
	char *path_str = db_path_to_string(pathp);
	/* Reconstruct the same role-keyed key used in plan.cpp Phase C:
	 * TS_SOFAR_MINUS is set when the leaf is on the subtractive side of
	 * any boolean node encountered above it in the current walk. */
	bool is_sub_ctx = (tsp->ts_sofar & TS_SOFAR_MINUS) != 0;
	std::string role_key = std::string(path_str) +
	    (is_sub_ctx ? "#sub" : "#base");
	bu_free(path_str, "path_str");
	auto it = vplan->inst_to_variant.find(role_key);
	if (it != vplan->inst_to_variant.end()) {
	    struct directory *vdp =
		db_lookup(tsp->ts_dbip, it->second.c_str(), LOOKUP_QUIET);
	    if (vdp && vdp->d_minor_type == ID_BOT) {
		if (rt_db_get_internal(&var_intern, vdp, tsp->ts_dbip,
				       NULL, tsp->ts_resp) >= 0) {
		    effective_ip = &var_intern;
		    var_loaded = true;
		}
	    }
	    /* If variant lookup failed (no BoT yet), fall through to original */
	}
    }

    void *odata = NULL;
    ts_status = bot_to_manifold(&odata, tsp, effective_ip, flip);

    if (var_loaded)
	rt_db_free_internal(&var_intern);
    if (ts_status < 0) {
	// If we failed, return TREE_NULL
	return TREE_NULL;
    }

    /* Diagnostic: log leaf name, role, and mesh SA */
    {
	bool is_sub_ctx = (tsp->ts_sofar & TS_SOFAR_MINUS) != 0;
	double leaf_sa = 0.0;
	if (odata) {
	    manifold::Manifold *lm = (manifold::Manifold *)odata;
	    leaf_sa = lm->SurfaceArea();
	}
	if (s && s->verbosity > 1) {
	    bu_log("[LEAF_TESS] name=%-30s  role=%s  mesh_SA=%.6f mm^2\n",
		   dp->d_namep,
		   is_sub_ctx ? "SUB " : "BASE",
		   leaf_sa);
	}
    }

    BU_GET(curtree, union tree);
    RT_TREE_INIT(curtree);
    curtree->tr_op = OP_TESS;
    curtree->tr_d.td_name = bu_strdup(dp->d_namep);
    curtree->tr_d.td_r = NULL;
    curtree->tr_d.td_d = odata;
    curtree->tr_d.td_i = NULL;

    bool should_log_treewalk = (s && s->verbosity > 1 && (RT_G_DEBUG & RT_DEBUG_TREEWALK));
    if (should_log_treewalk)
	bu_log("_booltree_leaf_tess(%s) OK\n", dp->d_namep);

    return curtree;
}


static union tree *
facetize_region_end(struct db_tree_state *tsp,
	const struct db_full_path *pathp,
	union tree *curtree,
	void *client_data)
{
    union tree **facetize_tree;

    if (tsp) RT_CK_DBTS(tsp);
    if (pathp) RT_CK_FULL_PATH(pathp);

    struct _ged_facetize_state *s = (struct _ged_facetize_state *)client_data;
    facetize_tree = &s->facetize_tree;

    if (curtree->tr_op == OP_NOP) return curtree;

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

    /* Tree has been saved, and will be freed later */
    return TREE_NULL;
}

static int
manifold_do_bool(
	union tree *tp, union tree *tl, union tree *tr,
	int op, struct bu_list *UNUSED(vlfree), const struct bn_tol *UNUSED(tol), void *data)
{
    struct _ged_facetize_state *s = (struct _ged_facetize_state *)data;
    if (!s)
	return -1;

    // Translate op for MANIFOLD
    manifold::OpType manifold_op = manifold::OpType::Add;
    switch (op) {
	case OP_UNION:
	    manifold_op = manifold::OpType::Add;
	    break;
	case OP_INTERSECT:
	    manifold_op = manifold::OpType::Intersect;
	    break;
	case OP_SUBTRACT:
	    manifold_op = manifold::OpType::Subtract;
	    break;
	default:
	    manifold_op = manifold::OpType::Add;
    };

    // If we have a left half space, bail - that's not well defined for producing
    // a Manifold closed volume
    if (tl->tr_d.td_i) {
	bu_log("Error - internal pointer on left boolean input\n");
	return -1;
    }

    // By this point we should have prepared our Manifold inputs - now
    // it's a question of doing the evaluation.
    manifold::Manifold *lm = (manifold::Manifold *)tl->tr_d.td_d;
    manifold::Manifold *rm = (manifold::Manifold *)tr->tr_d.td_d;
    manifold::Manifold *result = NULL;
    int failed = 0;
    bool delete_left = false;
    // On the right we can either have a Manifold, or a half space.  If it's
    // the latter, we need special handling.
    bool delete_right = false;
    if (tr->tr_d.td_i) {
	if (tr->tr_d.td_i->idb_minor_type != ID_HALF) {
	    return -1;
	}
	if (!lm) {
	    lm = new manifold::Manifold();
	    delete_left = true;
	}
	struct rt_half_internal *hf_ip= (struct rt_half_internal *)tr->tr_d.td_i->idb_ptr;
	if (manifold_op != manifold::OpType::Add) {

	    // Intersections and Subtractions with half spaces are handled
	    // by Manifold routines
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

    // Anything not already set up or handled is a no-op
    if (!lm) {
	lm = new manifold::Manifold();
	delete_left = true;
    }
    if (!rm) {
	rm = new manifold::Manifold();
	delete_right = true;
    }

    if (!result) {

	// We should have valid inputs - proceed
	facetize_log(s, 1, "Trying boolean op:  %s, %s\n", tl->tr_d.td_name, tr->tr_d.td_name);

	static const char *op_names[] = {"ADD","INTERSECT","SUBTRACT","ADD"};
	int opidx = (op == OP_INTERSECT) ? 1 : (op == OP_SUBTRACT) ? 2 : 0;
	if (s->verbosity > 1) {
	    bu_log("[BOOL_OP] %-8s L=%-30s SA=%.4f  R=%-30s SA=%.4f\n",
		   op_names[opidx],
		   tl->tr_d.td_name, lm->SurfaceArea(),
		   tr->tr_d.td_name, rm->SurfaceArea());
	}

	manifold::Manifold bool_out;
	try {
	    bool_out = lm->Boolean(*rm, manifold_op);
	} catch (...) {
	    facetize_log(s, 0, "Manifold boolean library threw failure\n");
	    // write out the failing inputs to files to aid in debugging
	    const char *evar = getenv("GED_MANIFOLD_DEBUG");
	    if (evar && strlen(evar)) {
		std::cerr << "Manifold op: " << (int)manifold_op << "\n";
		std::ofstream lofile, rofile;
		lofile.open(std::string(tl->tr_d.td_name)+std::string(".obj"));
		rofile.open(std::string(tr->tr_d.td_name)+std::string(".obj"));
		lm->WriteOBJ(lofile); rm->WriteOBJ(rofile);
		lofile.close(); rofile.close();
		bu_exit(EXIT_FAILURE, "Exiting to avoid overwriting debug outputs from Manifold boolean failure.");
	    }
	    failed = 1;
	}

	if (!failed) {
	    if (s->verbosity > 1) {
		bu_log("[BOOL_OP] %-8s L=%-30s  R=%-30s  result_SA=%.4f\n",
		       op_names[opidx],
		       tl->tr_d.td_name, tr->tr_d.td_name,
		       bool_out.SurfaceArea());
	    }
	    result = new manifold::Manifold(bool_out);
	}

	// If we're debugging and need to capture OBJ meshes for "successful" cases can use GED_MANIFOLD_DEBUG env var.
	const char *evar = getenv("GED_MANIFOLD_DEBUG");
	if (evar && strlen(evar)) {
	    std::ofstream lofile, rofile, oofile;
	    lofile.open(std::string(tl->tr_d.td_name)+std::string(".obj"));
	    rofile.open(std::string(tr->tr_d.td_name)+std::string(".obj"));
	    oofile.open(std::string("out-") + std::string(tl->tr_d.td_name)+std::to_string(op)+std::string(tr->tr_d.td_name)+std::string(".obj"));
	    lm->WriteOBJ(lofile); rm->WriteOBJ(rofile); bool_out.WriteOBJ(oofile);
	    lofile.close(); rofile.close(); oofile.close();
	}
    }

    // Memory cleanup
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

std::vector<std::string>
tess_avail_methods()
{

    // Build up the path to the ged_exec executable
    char tess_exec[MAXPATHLEN];
    bu_dir(tess_exec, MAXPATHLEN, BU_DIR_BIN, "ged_exec", BU_DIR_EXT, NULL);

    const char *tess_cmd[MAXPATHLEN] = {NULL};
    tess_cmd[ 0] = tess_exec;
    tess_cmd[ 1] = "facetize_process";
    tess_cmd[ 2] = "--list-methods";
    tess_cmd[ 3] = NULL;

    struct bu_process* p;
    bu_process_create(&p, tess_cmd, BU_PROCESS_HIDE_WINDOW);

    char mraw[MAXPATHLEN] = {'\0'};
    int read_res = bu_process_read_n(p, BU_PROCESS_STDOUT, MAXPATHLEN, mraw);

    if (bu_process_wait_n(&p, 0) || (read_res <= 0)) {
	// wait error or read error
	bu_log("%s %s - wait or read error\n", tess_cmd[0], tess_cmd[1]);
	std::vector<std::string> empty;
	return empty;
    }

    std::string mstr = std::string((const char *)mraw);
    std::stringstream mstream(mstr);
    std::string m;
    std::vector<std::string> methods;
    while (std::getline(mstream, m, ' ')) {
	methods.push_back(m);
    }

    return methods;
}

int
tess_run(struct _ged_facetize_state *s, const char **tess_cmd, int tess_cmd_cnt, fastf_t max_time, int ocnt)
{
    if (!s || !tess_cmd || !tess_cmd[3])
	return BRLCAD_ERROR;

    std::string wfile(tess_cmd[3]);
    std::string wfilebak = wfile + std::string(".bak");
    {
	// Before the run, prepare a backup file
	std::ifstream workfile(wfile, std::ios::binary);
	std::ofstream bakfile(wfilebak, std::ios::binary);
	if (!workfile.is_open() || !bakfile.is_open()) {
	    bu_log("Unable to create backup file %s\n", wfilebak.c_str());
	    return BRLCAD_ERROR;
	}
	bakfile << workfile.rdbuf();
	workfile.close();
	bakfile.close();
    }

    // Record the actual command being use to trigger the subprocess
    struct bu_vls cmd = BU_VLS_INIT_ZERO;
    for (int i = 0; i < tess_cmd_cnt ; i++)
	bu_vls_printf(&cmd, "%s ", tess_cmd[i]);
    facetize_log(s, 2, "%s\n", bu_vls_cstr(&cmd));
    bu_vls_free(&cmd);

    // Verbose progress line showing how many objects we're working on
    if (ocnt == 1)
	facetize_log(s, 1, "Attempting to triangulate %s...", tess_cmd[tess_cmd_cnt-ocnt]);
    if (ocnt > 1)
	facetize_log(s, 1, "Attempting to triangulate %d solids...", ocnt);

    int64_t start = bu_gettime();
    int64_t elapsed = 0;
    fastf_t seconds = 0.0;
    tess_cmd[tess_cmd_cnt] = NULL; // Make sure we're NULL terminated
    struct subprocess_s p;
    if (subprocess_create(tess_cmd, subprocess_option_no_window|subprocess_option_enable_async|subprocess_option_inherit_environment, &p)) {
	// Unable to create subprocess??
	facetize_log(s, 0, " FAILED.\n");
	facetize_log(s, 0, "Unable to create subprocess\n");

	return BRLCAD_ERROR;
    }
    while (subprocess_alive(&p)) {
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	elapsed = bu_gettime() - start;
	seconds = elapsed / 1000000.0;

	// Check for and pass along intermediate output
	char curr_out[MAXPATHLEN*10] = {'\0'};
	subprocess_read_stdout(&p, curr_out, MAXPATHLEN*10);
	if (strlen(curr_out))
	    facetize_log(s, 1, "%s", curr_out);
	char curr_err[MAXPATHLEN*10] = {'\0'};
	subprocess_read_stderr(&p, curr_err, MAXPATHLEN*10);
	if (strlen(curr_err))
	    facetize_log(s, 1, "%s", curr_err);

	if (seconds > max_time) {
	    // if we timeout, cleanup and return error
	    subprocess_terminate(&p);

	    facetize_log(s, 0, " FAILED.\n");

	    facetize_log(s, 0, "tess_run subprocess killed %g %g\n", seconds, max_time);
	    if (s->verbosity >= 0) {
		char mraw[MAXPATHLEN*10] = {'\0'};
		subprocess_read_stdout(&p, mraw, MAXPATHLEN*10);
		if (strlen(mraw))
		    facetize_log(s, 0, "%s\n", mraw);
		char mraw2[MAXPATHLEN*10] = {'\0'};
		subprocess_read_stderr(&p, mraw2, MAXPATHLEN*10);
		if (strlen(mraw2))
		    facetize_log(s, 0, "%s\n", mraw2);
	    }
	    subprocess_destroy(&p);

	    // Because we had to kill the process, there's no way of knowing
	    // whether we interrupted I/O in a state that could result in a
	    // corrupted .g file.  Restore the pre-run state of the .g file -
	    // we may have to redo some work, but this at least ensures we
	    // won't have strange garbage corrupting subsequent processing.
	    std::ifstream bakfile(wfilebak, std::ios::binary);
	    std::ofstream workfile(wfile, std::ios::binary);
	    if (!workfile.is_open() || !bakfile.is_open())
		return BRLCAD_ERROR;
	    workfile << bakfile.rdbuf();
	    workfile.close();
	    bakfile.close();


	    return BRLCAD_ERROR;
	}
    }
    int w_rc;
    if (subprocess_join(&p, &w_rc)) {
	// Unable to join??
	facetize_log(s, 0, " FAILED.\n");
	facetize_log(s, 0, "tess_run subprocess unable to join\n");
	if (s->verbosity >= 0) {
	    char mraw[MAXPATHLEN*10] = {'\0'};
	    subprocess_read_stdout(&p, mraw, MAXPATHLEN*10);
	    if (strlen(mraw))
		facetize_log(s, 0, "%s\n", mraw);
	    char mraw2[MAXPATHLEN*10] = {'\0'};
	    subprocess_read_stderr(&p, mraw2, MAXPATHLEN*10);
	    if (strlen(mraw2))
		facetize_log(s, 0, "%s\n", mraw2);
	}
	return BRLCAD_ERROR;
    }

    bu_file_delete(wfilebak.c_str());

    if (s->verbosity >= 0) {
	char mraw[MAXPATHLEN*10] = {'\0'};
	subprocess_read_stdout(&p, mraw, MAXPATHLEN*10);
	if (strlen(mraw))
	    facetize_log(s, 0, "%s\n", mraw);
	char mraw2[MAXPATHLEN*10] = {'\0'};
	subprocess_read_stderr(&p, mraw2, MAXPATHLEN*10);
	if (strlen(mraw2))
	    facetize_log(s, 0, "%s\n", mraw2);
    }

    // Needed to clean up file handles
    subprocess_destroy(&p);

    if (w_rc == BRLCAD_OK) {
	facetize_log(s, 1, " Success.\n");
    } else {
	facetize_log(s, 0, " FAILED.\n");
    }

    return (w_rc ? BRLCAD_ERROR : BRLCAD_OK);
}

/* -----------------------------------------------------------------------
 * TessSession — long-lived tessellator subprocess.
 *
 * Implements the class declared in ged_facetize.h.  The subprocess is
 * started in `--server` mode; the parent sends one-line commands on stdin
 * and reads one-line responses from stdout.
 * ----------------------------------------------------------------------- */

TessSession::TessSession()
    : proc_(nullptr), alive_(false), started_(false)
{
    memset(tess_exec_, 0, sizeof(tess_exec_));
    bu_dir(tess_exec_, MAXPATHLEN, BU_DIR_BIN, "ged_exec", BU_DIR_EXT, NULL);
}

TessSession::~TessSession()
{
    stop();
    delete proc_;
    proc_ = nullptr;
}

bool
TessSession::start(const char *wfile,
		   const std::string &methods,
		   const std::string &method_opts,
		   const char *cache_dir)
{
    wfile_       = wfile ? std::string(wfile) : std::string();
    methods_     = methods;
    method_opts_ = method_opts;
    cache_dir_   = cache_dir ? std::string(cache_dir) : std::string();
    started_     = true;
    return restart();
}

bool
TessSession::restart()
{
    if (!started_)
	return false;

    /* Clean up any previous process handle */
    if (proc_) {
	if (alive_)
	    subprocess_terminate(proc_);
	subprocess_destroy(proc_);
	delete proc_;
	proc_ = nullptr;
    }
    alive_ = false;
    stdout_buf_.clear();

    /* Build the argv array for the server subprocess */
    std::vector<const char *> args;
    args.push_back(tess_exec_);
    args.push_back("facetize_process");
    args.push_back("--server");
    args.push_back("-O");
    args.push_back(wfile_.c_str());
    std::string methods_arg;
    std::string mopts_arg;
    if (!methods_.empty()) {
	args.push_back("--methods");
	methods_arg = methods_;
	args.push_back(methods_arg.c_str());
    }
    if (!method_opts_.empty()) {
	args.push_back("--method-opts");
	mopts_arg = method_opts_;
	args.push_back(mopts_arg.c_str());
    }
    std::string cdir_arg;
    if (!cache_dir_.empty()) {
	args.push_back("--cache-dir");
	cdir_arg = cache_dir_;
	args.push_back(cdir_arg.c_str());
    }
    args.push_back(NULL);

    proc_ = new struct subprocess_s;
    memset(proc_, 0, sizeof(*proc_));
    int flags = subprocess_option_no_window
	      | subprocess_option_enable_async
	      | subprocess_option_inherit_environment;
    if (subprocess_create(args.data(), flags, proc_)) {
	delete proc_;
	proc_ = nullptr;
	return false;
    }

    /* Wait for "READY\n" from the server */
    std::string ready = read_line(10000 /* 10 s */);
    if (ready.find("READY") == std::string::npos) {
	subprocess_terminate(proc_);
	subprocess_destroy(proc_);
	delete proc_;
	proc_ = nullptr;
	return false;
    }

    alive_ = true;
    return true;
}

void
TessSession::stop()
{
    if (!proc_ || !alive_)
	return;
    FILE *fin = subprocess_stdin(proc_);
    if (fin) {
	fprintf(fin, "quit\n");
	fflush(fin);
    }
    int rc = 0;
    subprocess_join(proc_, &rc);
    subprocess_destroy(proc_);
    delete proc_;
    proc_ = nullptr;
    alive_ = false;
}

std::string
TessSession::read_line(int timeout_ms)
{
    if (!proc_)
	return std::string();

    int64_t deadline = bu_gettime() + (int64_t)timeout_ms * 1000LL;
    char buf[4096];

    while (true) {
	/* Check if we already have a complete line buffered */
	size_t nl = stdout_buf_.find('\n');
	if (nl != std::string::npos) {
	    std::string line = stdout_buf_.substr(0, nl);
	    stdout_buf_.erase(0, nl + 1);
	    /* Strip trailing \r */
	    if (!line.empty() && line.back() == '\r')
		line.pop_back();
	    return line;
	}

	/* Check timeout */
	if (bu_gettime() > deadline)
	    return std::string();

	/* Check if subprocess is still alive */
	if (!subprocess_alive(proc_)) {
	    alive_ = false;
	    return std::string();
	}

	/* Try to read more data */
	memset(buf, 0, sizeof(buf));
	int nr = subprocess_read_stdout(proc_, buf, sizeof(buf) - 1);
	if (nr > 0) {
	    stdout_buf_.append(buf, nr);
	} else {
	    /* Nothing available yet — sleep briefly */
	    std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
    }
}

TessSession::Result
TessSession::run_leaf(const char *leafname,
		      const char *method,
		      fastf_t max_time_s,
		      long long *elapsed_ms)
{
    if (!proc_ || !alive_)
	return SESS_CRASH;

    if (elapsed_ms)
	*elapsed_ms = -1;

    /* Send the tess command */
    FILE *fin = subprocess_stdin(proc_);
    if (!fin)
	return SESS_CRASH;
    fprintf(fin, "tess %s %s\n", method, leafname);
    fflush(fin);

    /* Wait for the response, honoring max_time_s */
    int timeout_ms = (int)(max_time_s * 1000.0) + 5000; /* +5 s grace */
    int64_t t0 = bu_gettime();
    std::string resp = read_line(timeout_ms);
    int64_t elapsed = bu_gettime() - t0;

    if (elapsed_ms)
	*elapsed_ms = (long long)(elapsed / 1000LL);

    if (resp.empty()) {
	/* No response within allotted time — treat as timeout */
	alive_ = false;
	if (subprocess_alive(proc_))
	    subprocess_terminate(proc_);
	return SESS_TIMEOUT;
    }

    if (!subprocess_alive(proc_))
	alive_ = false;

    /* Parse response: "OK <name> <ms>" or "FAIL <name> <reason>" */
    if (resp.rfind("OK ", 0) == 0) {
	/* Parse elapsed from the subprocess if provided */
	std::istringstream ss(resp.substr(3));
	std::string rname, ms_str;
	ss >> rname >> ms_str;
	if (!ms_str.empty() && elapsed_ms) {
	    try { *elapsed_ms = std::stoll(ms_str); }
	    catch (...) { /* ignore parse failures */ }
	}
	return SESS_OK;
    }

    if (resp.rfind("FAIL ", 0) == 0)
	return SESS_FAIL;

    /* Unexpected response */
    return SESS_FAIL;
}


    if (!s || !plan || plan->variant_names.empty())
	return BRLCAD_OK;

    char tess_exec[MAXPATHLEN];
    bu_dir(tess_exec, MAXPATHLEN, BU_DIR_BIN, "ged_exec", BU_DIR_EXT, NULL);

    char lcache[MAXPATHLEN] = {0};
    bu_dir(lcache, MAXPATHLEN, BU_DIR_CACHE, NULL);

    method_options_t *mo = (method_options_t *)s->method_opts;
    std::string mstrpp("NMG");
    std::string nmg_opts;
    fastf_t l_max_time = 30;
    if (mo) {
	nmg_opts = mo->method_optstr(mstrpp, s->dbip);
	l_max_time = (fastf_t)mo->max_time[mstrpp];
    }

    const char *tess_cmd[MAXPATHLEN] = {NULL};
    tess_cmd[0] = tess_exec;
    tess_cmd[1] = "facetize_process";
    tess_cmd[2] = "-O";
    tess_cmd[3] = bu_vls_cstr(s->wfile);
    tess_cmd[4] = "--methods";
    tess_cmd[5] = "NMG";
    tess_cmd[6] = "--method-opts";

    struct bu_vls mopts_vls = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&mopts_vls, "%s", nmg_opts.c_str());
    tess_cmd[7] = bu_vls_cstr(&mopts_vls);
    tess_cmd[8] = "--cache-dir";
    tess_cmd[9] = lcache;
    int cmd_fixed_cnt = 10;

    /* Process variants in 8000-char-bounded batches */
    int fail_cnt = 0;
    size_t vi = 0;
    while (vi < plan->variant_names.size()) {
	std::vector<const char *> batch_names;
	struct bu_vls cmd_check = BU_VLS_INIT_ZERO;
	for (int i = 0; i < cmd_fixed_cnt; i++)
	    bu_vls_printf(&cmd_check, "%s ", tess_cmd[i]);

	while (vi < plan->variant_names.size() &&
	       cmd_fixed_cnt + (int)batch_names.size() < MAXPATHLEN) {
	    const char *nm = plan->variant_names[vi].c_str();
	    if ((bu_vls_strlen(&cmd_check) + strlen(nm)) > 8000)
		break;
	    bu_vls_printf(&cmd_check, "%s ", nm);
	    batch_names.push_back(nm);
	    vi++;
	}
	bu_vls_free(&cmd_check);

	if (batch_names.empty())
	    break;

	for (size_t i = 0; i < batch_names.size(); i++)
	    tess_cmd[cmd_fixed_cnt + i] = batch_names[i];
	int total_cnt = cmd_fixed_cnt + (int)batch_names.size();

	int ret = tess_run(s, tess_cmd, total_cnt,
			   l_max_time * (fastf_t)batch_names.size(),
			   (int)batch_names.size());
	if (ret != BRLCAD_OK) {
	    facetize_log(s, 0,
			"FACETIZE: variant tessellation failed for %d object(s)\n",
			(int)batch_names.size());
	    fail_cnt += (int)batch_names.size();
	}

	/* Clear per-batch name slots */
	for (size_t i = 0; i < batch_names.size(); i++)
	    tess_cmd[cmd_fixed_cnt + i] = NULL;
    }

    bu_vls_free(&mopts_vls);
    plan->n_variant_tess_failures = fail_cnt;
    return (fail_cnt == 0) ? BRLCAD_OK : BRLCAD_ERROR;
}

int
bisect_run(struct _ged_facetize_state *s, std::vector<struct directory *> &bad_dps, std::vector<struct directory *> &inputs, const char **orig_cmd, int cmd_cnt, fastf_t max_time, int ocnt);

int
bisect_failing_inputs(struct _ged_facetize_state *s, std::vector<struct directory *> &bad_dps, std::vector<struct directory *> &inputs, const char **orig_cmd, int cmd_cnt, fastf_t max_time)
{
    std::vector<struct directory *> left_inputs;
    std::vector<struct directory *> right_inputs;
    for (size_t i = 0; i < inputs.size()/2; i++)
	left_inputs.push_back(inputs[i]);
    for (size_t i =  inputs.size()/2; i < inputs.size(); i++)
	right_inputs.push_back(inputs[i]);

    int lret = bisect_run(s, bad_dps, left_inputs, orig_cmd, cmd_cnt, max_time, left_inputs.size());
    int rret = bisect_run(s, bad_dps, right_inputs, orig_cmd, cmd_cnt, max_time, right_inputs.size());
    return lret + rret;
}

int
bisect_run(struct _ged_facetize_state *s, std::vector<struct directory *> &bad_dps, std::vector<struct directory *> &inputs, const char **orig_cmd, int cmd_cnt, fastf_t max_time, int ocnt)
{
    const char *tess_cmd[MAXPATHLEN] = {NULL};
    // The initial part of the re-run is the same.
    for (int i = 0; i < cmd_cnt; i++) {
	tess_cmd[i] = orig_cmd[i];
    }
    for (size_t i = 0; i < inputs.size(); i++) {
	tess_cmd[cmd_cnt+i] = inputs[i]->d_namep;
    }

    int ret = tess_run(s, tess_cmd, cmd_cnt+inputs.size(), max_time, ocnt);
    if (ret) {
	if (inputs.size() > 1) {
	    return bisect_failing_inputs(s, bad_dps, inputs, tess_cmd, cmd_cnt, max_time);
	}
	bad_dps.push_back(inputs[0]);
	return 1;
    }
    return 0;
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

#define CMD_LEN_MAX 8000

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

    // Set up a priority order of methods to try when processing primitives.
    std::vector<std::string> avail_methods = tess_avail_methods();
    if (avail_methods.size() == 0) {
	bu_log("No methods for tessellation found.\n");
	bu_dirclear(s->wdir);
	return BRLCAD_ERROR;
    }

    method_options_t *mo = (method_options_t*)s->method_opts;
    std::vector<std::string> method_list;
    for (size_t i = 0; i < mo->methods.size(); i++) {
	std::string cmethod = mo->methods[i];
	if (std::find(avail_methods.begin(), avail_methods.end(), cmethod) != avail_methods.end()) {
	    method_list.push_back(cmethod);
	} else {
	    bu_log("Warning: user requested %s tessellation method not found.\n", cmethod.c_str());
	}
    }

    if (mo->methods.size() && !method_list.size()) {
	bu_log("Error: all user requested tessellation methods unsupported.\n");
	bu_dirclear(s->wdir);
	return BRLCAD_ERROR;
    }

    if (!method_list.size() && avail_methods.size()) {
	method_list = avail_methods;
    }

    // We want the subprocess to be using the same cache directory as the parent
    char lcache[MAXPATHLEN] = {0};
    bu_dir(lcache, MAXPATHLEN, BU_DIR_CACHE, NULL);

    /* -----------------------------------------------------------------------
     * Phase 0: seed / rewind attribute state on the working .g copy.
     * On --resume, any leaf still tagged "working::<method>::..." is rewound
     * to "nottried" so it will be retried with the new parameters.
     * On a fresh run, all candidate leaves get "nottried".
     * ----------------------------------------------------------------------- */
    {
	struct db_i *wdbip = db_open(bu_vls_cstr(s->wfile), DB_OPEN_READWRITE);
	if (wdbip) {
	    db_dirbuild(wdbip);
	    db_update_nref(wdbip);
	    if (s->resume) {
		facetize_resume_rewind_working(wdbip, leaf_dps);
	    } else {
		facetize_seed_leaves_nottried(wdbip, leaf_dps);
	    }
	    db_close(wdbip);
	}
    }

    /* -----------------------------------------------------------------------
     * Phase 1 main loop: iterate over each method in priority order.
     *
     * Per-leaf state transitions:
     *   nottried → working::<method>::... (before sending to subprocess)
     *   working::<method>::... → <method>::... (on OK)
     *   working::<method>::... stays (on FAIL/TIMEOUT/CRASH → next method)
     * ----------------------------------------------------------------------- */
    std::vector<std::string> failed_dps;

    /* Build combined method options string for the TessSession startup */
    auto method_opts_for = [&](const std::string &m) -> std::string {
	return mo->method_optstr(m, dbip);
    };

    /* Process the standard queue using the priority-ordered method list */
    {
	/* Build flat list of candidates (from pq in priority order) */
	std::vector<struct directory *> std_leaves;
	std::priority_queue<struct directory *, std::vector<struct directory *>, DpCompare> pq_copy = pq;
	while (!pq_copy.empty()) {
	    std_leaves.push_back(pq_copy.top());
	    pq_copy.pop();
	}

	for (size_t mi = 0; mi < method_list.size() && !std_leaves.empty(); mi++) {
	    const std::string &method = method_list[mi];
	    fastf_t l_max_time = (fastf_t)mo->max_time[method];
	    std::string mopts = method_opts_for(method);

	    /* Build comma-separated list of all methods for subprocess startup */
	    std::string methods_csv;
	    for (size_t j = 0; j < method_list.size(); j++) {
		if (j) methods_csv += ",";
		methods_csv += method_list[j];
	    }

	    /* Start (or restart) the TessSession for this method sweep */
	    TessSession sess;
	    struct bu_vls mopts_arg = BU_VLS_INIT_ZERO;
	    bu_vls_sprintf(&mopts_arg, "NMG %s", mopts.c_str());
	    if (!sess.start(bu_vls_cstr(s->wfile), method, bu_vls_cstr(&mopts_arg), lcache)) {
		bu_vls_free(&mopts_arg);
		bu_log("FACETIZE: failed to start tessellation subprocess for method %s\n", method.c_str());
		/* Mark all remaining leaves as failed */
		struct db_i *wdbip = db_open(bu_vls_cstr(s->wfile), DB_OPEN_READWRITE);
		if (wdbip) {
		    db_dirbuild(wdbip);
		    for (auto *ldp : std_leaves)
			facetize_status_set(wdbip, ldp->d_namep, "skipped");
		    db_close(wdbip);
		}
		break;
	    }
	    bu_vls_free(&mopts_arg);

	    size_t total_this_method = std_leaves.size();
	    std::vector<struct directory *> still_failing;

	    for (size_t li = 0; li < std_leaves.size(); li++) {
		struct directory *ldp = std_leaves[li];

		/* Progress reporting */
		if (s->verbosity == 0 && ((li + 1) % FACETIZE_PROGRESS_INTERVAL == 0 || li + 1 == total_this_method)) {
		    facetize_log(s, 0, "  [%s] %zu/%zu solids processed\n", method.c_str(), li + 1, total_this_method);
		} else if (s->verbosity >= 2) {
		    facetize_log(s, 2, "  [%s] tessellating %s\n", method.c_str(), ldp->d_namep);
		}

		/* Mark leaf as in-flight in the working .g */
		{
		    struct db_i *wdbip = db_open(bu_vls_cstr(s->wfile), DB_OPEN_READWRITE);
		    if (wdbip) {
			db_dirbuild(wdbip);
			struct bu_vls working_status = BU_VLS_INIT_ZERO;
			bu_vls_sprintf(&working_status, "working::%s::%s",
				       method.c_str(), mopts.c_str());
			facetize_status_set(wdbip, ldp->d_namep, bu_vls_cstr(&working_status));
			bu_vls_free(&working_status);
			db_close(wdbip);
		    }
		}

		/* If the session crashed, restart it */
		if (!sess.alive()) {
		    if (!sess.restart()) {
			/* Can't restart — leave this and remaining leaves as working:: */
			still_failing.push_back(ldp);
			for (size_t k = li + 1; k < std_leaves.size(); k++)
			    still_failing.push_back(std_leaves[k]);
			break;
		    }
		}

		long long elapsed_ms = -1;
		TessSession::Result res = sess.run_leaf(ldp->d_namep, method.c_str(),
							l_max_time, &elapsed_ms);

		struct db_i *wdbip = db_open(bu_vls_cstr(s->wfile), DB_OPEN_READWRITE);
		if (!wdbip) {
		    still_failing.push_back(ldp);
		    continue;
		}
		db_dirbuild(wdbip);
		db_update_nref(wdbip);

		if (res == TessSession::SESS_OK) {
		    /* Strip the "working::" prefix → status = <method>::<opts> */
		    struct bu_vls done_status = BU_VLS_INIT_ZERO;
		    bu_vls_sprintf(&done_status, "%s::%s", method.c_str(), mopts.c_str());
		    facetize_status_set(wdbip, ldp->d_namep, bu_vls_cstr(&done_status));
		    /* Also set legacy method attr for backward compat */
		    struct bu_attribute_value_set avs = BU_AVS_INIT_ZERO;
		    struct directory *wdp = db_lookup(wdbip, ldp->d_namep, LOOKUP_QUIET);
		    if (wdp) {
			db5_get_attributes(wdbip, &avs, wdp);
			bu_avs_add(&avs, FACETIZE_METHOD_ATTR, method.c_str());
			db5_update_attributes(wdp, &avs, wdbip);
		    }
		    bu_avs_free(&avs);
		    bu_vls_free(&done_status);
		    if (s->verbosity >= 2)
			facetize_log(s, 2, "    OK (%lld ms)\n", elapsed_ms);
		} else {
		    /* FAIL / TIMEOUT / CRASH — leave working:: prefix for next method */
		    still_failing.push_back(ldp);
		    if (res == TessSession::SESS_TIMEOUT || res == TessSession::SESS_CRASH) {
			/* Restart will happen at top of next leaf iteration */
			if (res == TessSession::SESS_TIMEOUT)
			    facetize_log(s, 1, "  [%s] %s timed out (%.0f s limit)\n",
					 method.c_str(), ldp->d_namep, (double)l_max_time);
		    }
		}
		db_close(wdbip);
	    }

	    sess.stop();
	    std_leaves = still_failing;
	}

	/* After all methods exhausted, std_leaves holds unconvertible primitives */
	for (auto *ldp : std_leaves)
	    failed_dps.push_back(std::string(ldp->d_namep));
    }

    /* Process DSP displacement map leaves — always CM */
    {
	std::string cm_method = "CM";
	fastf_t cm_max_time = (fastf_t)mo->max_time[cm_method];
	std::string cm_opts = method_opts_for(cm_method);

	TessSession cm_sess;
	struct bu_vls mopts_arg = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&mopts_arg, "CM %s", cm_opts.c_str());
	bool cm_started = cm_sess.start(bu_vls_cstr(s->wfile), cm_method, bu_vls_cstr(&mopts_arg), lcache);
	bu_vls_free(&mopts_arg);

	while (!q_dsp.empty()) {
	    struct directory *ldp = q_dsp.front();
	    q_dsp.pop();

	    if (!cm_started) {
		failed_dps.push_back(std::string(ldp->d_namep));
		continue;
	    }

	    {
		struct db_i *wdbip = db_open(bu_vls_cstr(s->wfile), DB_OPEN_READWRITE);
		if (wdbip) {
		    db_dirbuild(wdbip);
		    facetize_status_set(wdbip, ldp->d_namep, "working::CM");
		    db_close(wdbip);
		}
	    }

	    if (!cm_sess.alive() && !cm_sess.restart()) {
		failed_dps.push_back(std::string(ldp->d_namep));
		continue;
	    }

	    long long elapsed_ms = -1;
	    TessSession::Result res = cm_sess.run_leaf(ldp->d_namep, "CM", cm_max_time, &elapsed_ms);

	    struct db_i *wdbip = db_open(bu_vls_cstr(s->wfile), DB_OPEN_READWRITE);
	    if (wdbip) {
		db_dirbuild(wdbip);
		db_update_nref(wdbip);
		if (res == TessSession::SESS_OK) {
		    struct bu_vls done_status = BU_VLS_INIT_ZERO;
		    bu_vls_sprintf(&done_status, "CM::%s", cm_opts.c_str());
		    facetize_status_set(wdbip, ldp->d_namep, bu_vls_cstr(&done_status));
		    struct bu_attribute_value_set avs = BU_AVS_INIT_ZERO;
		    struct directory *wdp = db_lookup(wdbip, ldp->d_namep, LOOKUP_QUIET);
		    if (wdp) {
			db5_get_attributes(wdbip, &avs, wdp);
			bu_avs_add(&avs, FACETIZE_METHOD_ATTR, "CM");
			db5_update_attributes(wdp, &avs, wdbip);
		    }
		    bu_avs_free(&avs);
		    bu_vls_free(&done_status);
		} else {
		    failed_dps.push_back(std::string(ldp->d_namep));
		}
		db_close(wdbip);
	    } else {
		failed_dps.push_back(std::string(ldp->d_namep));
	    }
	}
	cm_sess.stop();
    }

    /* Process plate-mode BoTs — NMG with the plate max_time */
    {
	std::string nmg_method = "NMG";
	fastf_t plate_max_time = (fastf_t)mo->plate_max_time;
	std::string nmg_opts = method_opts_for(nmg_method);

	TessSession pbot_sess;
	struct bu_vls mopts_arg = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&mopts_arg, "NMG %s", nmg_opts.c_str());
	bool pbot_started = pbot_sess.start(bu_vls_cstr(s->wfile), nmg_method, bu_vls_cstr(&mopts_arg), lcache);
	bu_vls_free(&mopts_arg);
	bool pbot_any_fail = false;

	while (!q_pbot.empty()) {
	    struct directory *ldp = q_pbot.top();
	    q_pbot.pop();

	    if (!pbot_started) {
		pbot_any_fail = true;
		continue;
	    }

	    {
		struct db_i *wdbip = db_open(bu_vls_cstr(s->wfile), DB_OPEN_READWRITE);
		if (wdbip) {
		    db_dirbuild(wdbip);
		    facetize_status_set(wdbip, ldp->d_namep, "working::NMG_PLATE");
		    db_close(wdbip);
		}
	    }

	    if (!pbot_sess.alive() && !pbot_sess.restart()) {
		pbot_any_fail = true;
		continue;
	    }

	    long long elapsed_ms = -1;
	    TessSession::Result res = pbot_sess.run_leaf(ldp->d_namep, "NMG", plate_max_time, &elapsed_ms);

	    struct db_i *wdbip = db_open(bu_vls_cstr(s->wfile), DB_OPEN_READWRITE);
	    if (wdbip) {
		db_dirbuild(wdbip);
		db_update_nref(wdbip);
		if (res == TessSession::SESS_OK) {
		    struct bu_vls done_status = BU_VLS_INIT_ZERO;
		    bu_vls_sprintf(&done_status, "NMG_PLATE::%s", nmg_opts.c_str());
		    facetize_status_set(wdbip, ldp->d_namep, bu_vls_cstr(&done_status));
		    struct bu_attribute_value_set avs = BU_AVS_INIT_ZERO;
		    struct directory *wdp = db_lookup(wdbip, ldp->d_namep, LOOKUP_QUIET);
		    if (wdp) {
			db5_get_attributes(wdbip, &avs, wdp);
			bu_avs_add(&avs, FACETIZE_METHOD_ATTR, "PLATE");
			db5_update_attributes(wdp, &avs, wdbip);
		    }
		    bu_avs_free(&avs);
		    bu_vls_free(&done_status);
		} else {
		    pbot_any_fail = true;
		}
		db_close(wdbip);
	    } else {
		pbot_any_fail = true;
	    }
	}
	pbot_sess.stop();

	if (pbot_any_fail) {
	    facetize_log(s, 0, "Plate mode conversion wasn't able to complete\n");
	    return BRLCAD_ERROR;
	}
    }

    if (failed_dps.size()) {
	// All methods exhausted — decide what to do with the remaining failures.
	struct db_i *cdbip = db_open(bu_vls_cstr(s->wfile), DB_OPEN_READWRITE);
	if (cdbip) {
	    db_dirbuild(cdbip);
	    db_update_nref(cdbip);
	    for (size_t i = 0; i < failed_dps.size(); i++) {
		struct directory *dp = db_lookup(cdbip, failed_dps[i].c_str(), LOOKUP_QUIET);
		if (!dp)
		    continue;
		if (s->partial) {
		    /* --partial: mark as skipped so Phase 2 can substitute empty BoTs */
		    facetize_status_set(cdbip, failed_dps[i].c_str(), "skipped");
		} else {
		    /* Strict mode: mark as FAIL for summary; keep working:: attrs
		     * intact so the user can --resume with different options. */
		    struct bu_attribute_value_set avs = BU_AVS_INIT_ZERO;
		    db5_get_attributes(cdbip, dp, &avs);
		    (void)bu_avs_add(&avs, FACETIZE_METHOD_ATTR, "FAIL");
		    (void)db5_update_attributes(dp, &avs, cdbip);
		    bu_avs_free(&avs);
		}
	    }
	    db_close(cdbip);
	}

	if (!s->partial) {
	    /* Return error — caller must NOT delete the working dir so the user
	     * can --resume with adjusted options/methods. */
	    return BRLCAD_ERROR;
	}
    }

    return BRLCAD_OK;
}

int
_ged_facetize_booleval_tri(struct _ged_facetize_state *s, struct db_i *dbip, struct rt_wdb *wdbp, int argc, const char **argv, const char *oname, struct bu_list *vlfree, bool output_to_working)
{
    union tree *ftree;
    if (!dbip || !wdbp || !argv || !oname)
	return BRLCAD_ERROR;

    /* Per-object booleval status is shown only in verbose mode. */
    if (s->verbosity >= 1) {
	if (argc == 1) {
	    bu_log("%s: evaluating booleans...\n", argv[0]);
	} else {
	    bu_log("Evaluating booleans for the trees of %d input objects...\n", argc);
	}
    }

    // Unlike the -r flag processing regions, where each individual region
    // processed is semantically a single solid , there is no guarantee in
    // general that the output is representing a single, well behaved solid.
    // Consequently, thin volumes and close faces may be expected features and
    // it's more problematic to do the fixup check.  However, if we were given
    // a single primitive or region, those outputs should satisfy the fixup
    // criteria.
    bool do_fixup = false;
    if (argc == 1 && !s->no_fixup) {
	struct directory *dp = db_lookup(dbip, argv[0], LOOKUP_QUIET);
	if ((dp->d_flags & RT_DIR_REGION) || (dp->d_flags & RT_DIR_SOLID))
	    do_fixup = true;
    }

    // If we don't have inputs that can be fed to db_walk_tree it will produce
    // an error, which we don't want.  What we do want in such a case - where
    // there are NO valid walking candidates - is to indicate that there wasn't
    // a logic failure.  That means we need an empty bot to be generated - i.e.
    // we don't want to trigger the db_walk_tree error path.
    int ac = 0;
    const char **av = (const char **)bu_calloc(argc, sizeof(const char *), "av");
    for (int i = 0; i < argc; i++) {
	struct directory *dp = db_lookup(dbip, argv[i], LOOKUP_QUIET);
	if (dp->d_flags & RT_DIR_COMB || dp->d_flags & RT_DIR_SOLID) {
	    av[ac] = argv[i];
	    ac++;
	}
    }

    if (ac) {
	s->error_flag = 0;
	struct db_tree_state init_state;
	db_init_db_tree_state(&init_state, dbip);
	/* Establish tolerances */
	init_state.ts_ttol = &wdbp->wdb_ttol;
	init_state.ts_tol = &wdbp->wdb_tol;
	init_state.ts_m = NULL;
	s->facetize_tree = (union tree *)0;
	int i = 0;
	if (!BU_SETJUMP) {
	    /* try */
	    i = db_walk_tree(dbip, argc, argv,
		    1,
		    &init_state,
		    0,			/* take all regions */
		    facetize_region_end,
		    _booltree_leaf_tess,
		    (void *)s
		    );
	} else {
	    /* catch */
	    BU_UNSETJUMP;
	    i = -1;
	} BU_UNSETJUMP;

	// Something went wrong - not just empty geometry, but an actual error.
	// Do not generate a BoT, empty or otherwise.
	if (i < 0 || s->error_flag) {
	    bu_free(av, "av");
	    facetize_log(s, 0, "FAILED.\n");
	    return BRLCAD_ERROR;
	}
    }
    bu_free(av, "av");

    struct db_i *odbip = (output_to_working) ? dbip : s->dbip;

    // We don't have a tree - unless we've been told not to, prepare an empty BoT
    if (!s->facetize_tree && !s->no_empty) {
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
	if (_ged_facetize_write_bot(odbip, bot, oname, s->verbosity) != BRLCAD_OK) {
	    facetize_log(s, 0, "FAILED.\n");
	    return BRLCAD_ERROR;
	}
	facetize_log(s, 0, " Success.\n");
	return BRLCAD_OK;
    }

    // Third stage is to execute the boolean operations
    ftree = rt_booltree_eval(s->facetize_tree, vlfree, &wdbp->wdb_tol, &manifold_do_bool, 0, (void *)s);
    if (!ftree) {
	return BRLCAD_ERROR;
    }

    if (ftree->tr_d.td_d) {
	manifold::Manifold *om = (manifold::Manifold *)ftree->tr_d.td_d;
	if (om->Status() != manifold::Manifold::Error::NoError) {
	    // Urk - boolean failure of some sort!
	    facetize_log(s, 0, "Boolean algorithm FAILED.\n");
	    return BRLCAD_ERROR;
	}

	if (s->verbosity > 1) {
	    bu_log("[FINAL_BOOL] obj=%s  final_mesh_SA=%.6f mm^2  num_verts=%zu  num_faces=%zu\n",
		   (argc > 0 && argv && argv[0]) ? argv[0] : "?",
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

	/* Guard against near-zero perturb slivers: if the booleval mesh is tiny,
	 * quickly Crofton-check the original CSG.  If CSG is effectively empty,
	 * emit an empty BoT to match raytrace behavior. */
	double bot_vol = 0.0;
	if (bot->num_faces > 0 && bot->num_vertices > 0) {
	    bot_vol = std::fabs(bg_trimesh_volume(bot->faces, bot->num_faces,
						  (const point_t *)bot->vertices,
						  bot->num_vertices));
	}
	double bbox_vol = bot_bbox_volume(bot);
	bool tiny_bot = (bbox_vol > 0.0) ?
	    (bot_vol <= bbox_vol * FACETIZE_EMPTY_CHECK_REL_VOL_TOL) :
	    (bot_vol <= FACETIZE_EMPTY_CHECK_ABS_VOL_TOL);
	bool is_single_input = (argc == 1 && argv && argv[0]);
	bool has_csg_context = (s && s->dbip);
	if (tiny_bot && is_single_input && has_csg_context) {
	    double csg_vol = -1.0;
	    if (csg_crofton_volume(s->dbip, argv[0], &csg_vol) == BRLCAD_OK) {
		double csg_abs = std::fabs(csg_vol);
		double csg_vtol = (bbox_vol > 0.0) ?
		    (bbox_vol * FACETIZE_EMPTY_CHECK_REL_VOL_TOL) :
		    FACETIZE_EMPTY_CHECK_ABS_VOL_TOL;
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

	// If we have a manifold_mesh, write it out as a bot
	if (_ged_facetize_write_bot(odbip, bot, oname, s->verbosity) != BRLCAD_OK) {
	    facetize_log(s, 0, "FAILED.\n");
	    return BRLCAD_ERROR;
	}
    } else {
	// Evaluation didn't produce a tree - unless we've been told not to,
	// prepare an empty BoT
	if (!s->no_empty) {
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
	    if (_ged_facetize_write_bot(odbip, bot, oname, s->verbosity) != BRLCAD_OK) {
		facetize_log(s, 0, "FAILED.\n");
		return BRLCAD_ERROR;
	    }
	    facetize_log(s, 0, "Success.\n");
	    return BRLCAD_OK;
	}
    }

    // If we meet the conditions, apply the fixup logic
    if (do_fixup) {
	struct directory *dp = db_lookup(dbip, argv[0], LOOKUP_QUIET);
	if ((dp->d_flags & RT_DIR_REGION) || (!(dp->d_flags & RT_DIR_COMB))) {
	    struct directory *bot_dp = db_lookup(odbip, oname, LOOKUP_QUIET);
	    struct rt_bot_internal *nbot = bot_fixup(s, odbip, bot_dp, oname);
	    if (nbot) {
		// Write out new version of BoT
		db_delete(odbip, bot_dp);
		db_dirdelete(odbip, bot_dp);
		if (_ged_facetize_write_bot(odbip, nbot, oname, s->verbosity) != BRLCAD_OK) {
		    facetize_log(s, 0, "FAILED.\n");
		}
	    }
	}
    }

    facetize_log(s, 0, "Success.\n");
    return BRLCAD_OK;
}

int
_ged_facetize_booleval(struct _ged_facetize_state *s, int argc, struct directory **dpa, const char *oname, bool output_to_working, bool cleanup)
{
    int ret = BRLCAD_OK;
    struct bu_list *vlfree = &rt_vlfree;

    if (!s)
	return BRLCAD_ERROR;

    if (!argc || !dpa)
	return BRLCAD_ERROR;

    struct db_i *dbip = s->dbip;
    struct rt_wdb *wwdbp;

    /* First stage is to process the primitive instances.  We include points in
     * this even though they do not define a volume in order to allow for the
     * possibility of applying the alternative pnt based reconstruction methods
     * to their data. */
    const char *sfilter = "-type shape -or -type pnts";
    struct bu_ptbl leaf_dps = BU_PTBL_INIT_ZERO;
    if (db_search(&leaf_dps, DB_SEARCH_RETURN_UNIQ_DP, sfilter, argc, dpa, dbip, NULL, NULL, NULL) < 0) {
	// Empty input - nothing to facetize.
	return BRLCAD_OK;
    }

    /* OK, we have work to do. Set up a working copy of the .g file. */
    if (_ged_facetize_working_file_setup(s, &leaf_dps) != BRLCAD_OK)
	return BRLCAD_ERROR;

    /* Direct Manifold booleval keeps the eager perturb path: when enabled,
     * build and tessellate coplanarity-avoidance variants up front.
     * Region mode overrides this by validating first and only retrying with
     * variants on demand. */
    if (s->variant_plan) {
	delete (FacetizeVariantPlan *)s->variant_plan;
	s->variant_plan = NULL;
    }
    if (!s->make_nmg && !s->nmg_booleval && !s->no_perturb) {
	FacetizeVariantPlan *vplan = _ged_facetize_build_variant_plan(s, argc, dpa);
	s->variant_plan = (void *)vplan;
    }

    if (_ged_facetize_leaves_tri(s, dbip, &leaf_dps))
	return BRLCAD_ERROR;

    if (s->variant_plan) {
	FacetizeVariantPlan *vplan = (FacetizeVariantPlan *)s->variant_plan;
	if (!vplan->variant_names.empty())
	    _ged_facetize_tessellate_variant_names(s, vplan);
    }

    // Re-open working .g copy after BoTs have replaced CSG solids and perform
    // the tree walk to set up Manifold data.
    struct db_i *wdbip = db_open(bu_vls_cstr(s->wfile), (output_to_working) ? DB_OPEN_READWRITE :  DB_OPEN_READONLY);
    if (!wdbip) {
	bu_dirclear(s->wdir);
	return BRLCAD_ERROR;
    }
    if (db_dirbuild(wdbip) < 0)
	return BRLCAD_ERROR;

    db_update_nref(wdbip);

    // Need wdbp in the next two stages for tolerances
    wwdbp = wdb_dbopen(wdbip, RT_WDB_TYPE_DB_DEFAULT);

    /* Second stage is to prepare Manifold versions of the instances of the BoT
     * obj conversions generated by stage 1.  This is where matrix placement
     * is handled. */
    // Prepare argc/argv array for db_walk_tree
    const char **av = (const char **)bu_calloc(argc+1, sizeof(char *), "av");
    for (int i = 0; i < argc; i++) {
	av[i] = dpa[i]->d_namep;
    }

    if (_ged_facetize_booleval_tri(s, wdbip, wwdbp, argc, av, oname, vlfree, output_to_working) != BRLCAD_OK) {
	if (s->verbosity >= 0) {
	    bu_log("FACETIZE: failed to generate %s\n", oname);
	}
    }

    bu_free(av, "av");
    db_close(wdbip);

    if (cleanup)
	bu_dirclear(s->wdir);

    bu_ptbl_free(&leaf_dps);

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
