/*                         V I E W . C
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
/** @file libged/view.c
 *
 * The view command.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "bu/cmd.h"
#include "bu/malloc.h"
#include "bu/vls.h"

#include "bsg/settings.h"
#include "bsg/node.h"
#include "bsg/selection.h"
#include "ged/bsg_ged_draw.h"
#include "../ged_private.h"
#include "./ged_view.h"

/* Phase A1 (drawing_stack_modernization): visit context + callback for the
 * "view vZ" autodetect path.  Used with both bv_view_obj_visit and
 * bv_view_objs_visit_db; their callback signature matches. */
struct _view_vZ_ctx {
    struct bview *cv;
    int calc_mode;
    double vZ;
    int have_vz;
};

static int
_view_vZ_visit_cb(struct bv_scene_obj *s, void *data)
{
    struct _view_vZ_ctx *c = (struct _view_vZ_ctx *)data;
    fastf_t calc_val = bv_vZ_calc(s, c->cv, c->calc_mode);
    if (c->calc_mode) {
	if (calc_val > c->vZ) {
	    c->vZ = calc_val;
	    c->have_vz = 1;
	}
    } else {
	if (calc_val < c->vZ) {
	    c->vZ = calc_val;
	    c->have_vz = 1;
	}
    }
    return 1;
}

int
_view_cmd_msgs(void *bs, int argc, const char **argv, const char *us, const char *ps)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    if (argc == 2 && BU_STR_EQUAL(argv[1], HELPFLAG)) {
	bu_vls_printf(gd->gedp->ged_result_str, "%s\n%s\n", us, ps);
	return 1;
    }
    if (argc == 2 && BU_STR_EQUAL(argv[1], PURPOSEFLAG)) {
	bu_vls_printf(gd->gedp->ged_result_str, "%s\n", ps);
	return 1;
    }
    return 0;
}

struct _view_independent_path {
    char *path;
    int mode;
};

typedef int (*view_core_cmd_func)(struct ged *, int, const char **);

static int
_view_call_on_gd_view(struct _ged_view_info *gd, view_core_cmd_func cmd, int argc, const char **argv)
{
    struct bview *cv = gd->gedp->ged_gvp;
    gd->gedp->ged_gvp = gd->cv;
    int ret = cmd(gd->gedp, argc, argv);
    gd->gedp->ged_gvp = cv;
    return ret;
}

static void
_view_independent_paths_free(struct _view_independent_path *paths, size_t path_cnt)
{
    if (!paths)
	return;

    for (size_t i = 0; i < path_cnt; i++) {
	if (paths[i].path)
	    bu_free(paths[i].path, "independent path");
    }

    bu_free(paths, "independent paths");
}

static int
_view_independent_paths_add(struct _view_independent_path **paths,
			    size_t *path_cnt,
			    size_t *path_cap,
			    const char *path,
			    int mode)
{
    if (!paths || !path_cnt || !path_cap || !path || !path[0])
	return BRLCAD_ERROR;

    for (size_t i = 0; i < *path_cnt; i++) {
	if ((*paths)[i].mode == mode && BU_STR_EQUAL((*paths)[i].path, path))
	    return BRLCAD_OK;
    }

    if (*path_cnt >= *path_cap) {
	size_t ncap = (*path_cap < 8) ? 8 : (*path_cap * 2);
	struct _view_independent_path *npaths = (struct _view_independent_path *)bu_realloc(
		*paths, ncap * sizeof(struct _view_independent_path), "independent paths");
	if (!npaths)
	    return BRLCAD_ERROR;
	*paths = npaths;
	*path_cap = ncap;
    }

    (*paths)[*path_cnt].path = bu_strdup(path);
    (*paths)[*path_cnt].mode = mode;
    (*path_cnt)++;

    return BRLCAD_OK;
}

static int
_view_independent_collect_paths(struct _view_independent_path **paths,
				size_t *path_cnt,
				size_t *path_cap,
				struct bv_scene_obj *node)
{
    if (!node)
	return BRLCAD_OK;

    if (node->bsg.bsg_kind & BSG_NODE_VIEW_SCOPE)
	return BRLCAD_OK;

    if (node->bsg.bsg_kind & BV_DB_OBJS) {
	void *_ud = bsg_node_ged_data_get((const bsg_node *)node);
	struct ged_bv_data *bdata = _ud ? (struct ged_bv_data *)_ud : NULL;
	if (bdata && bdata->s_fullpath.fp_len > 0) {
	    char *fpath = db_path_to_string(&bdata->s_fullpath);
	    const char *npath = (fpath && fpath[0] == '/') ? fpath + 1 : fpath;
	    if (npath && strlen(npath)) {
		/* Phase 12: read draw mode via BSG settings accessor. */
		struct bsg_settings sinfo;
		bsg_node_settings_get((const bsg_node *)node, &sinfo);
		int mode = sinfo.draw_mode;
		int ret = _view_independent_paths_add(paths, path_cnt, path_cap, npath, mode);
		if (fpath)
		    bu_free(fpath, "db path string");
		if (ret != BRLCAD_OK)
		    return ret;
	    } else if (fpath) {
		bu_free(fpath, "db path string");
	    }
	}
    }

    for (size_t i = 0; i < BU_PTBL_LEN(&node->bsg.bsg_children); i++) {
	struct bv_scene_obj *child = (struct bv_scene_obj *)BU_PTBL_GET(&node->bsg.bsg_children, i);
	int ret = _view_independent_collect_paths(paths, path_cnt, path_cap, child);
	if (ret != BRLCAD_OK)
	    return ret;
    }

    return BRLCAD_OK;
}

int
_view_cmd_aet(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    const char *usage_string = "view [options] aet [vals]";
    const char *purpose_string = "get/set azimuth/elevation/twist of view";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    return _view_call_on_gd_view(gd, ged_aet_core, argc, argv);
}

int
_view_cmd_center(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    const char *usage_string = "view [options] center [vals]";
    const char *purpose_string = "get/set view center";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    return _view_call_on_gd_view(gd, ged_center_core, argc, argv);
}

int
_view_cmd_eye(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    const char *usage_string = "view [options] eye [vals]";
    const char *purpose_string = "get/set view eye point";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    return _view_call_on_gd_view(gd, ged_eye_core, argc, argv);
}

int
_view_cmd_faceplate(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    const char *usage_string = "view [options] faceplate [vals]";
    const char *purpose_string = "manage faceplate view elements";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    return _view_call_on_gd_view(gd, ged_faceplate_core, argc, argv);
}

/* When a view is "independent", it displays only those objects when have been
 * added to its individual scene storage - the shared objects common to all
 * views will not be drawn.  When shifting a view from shared to independent
 * its local storage is populated with copies of the shared objects to prevent
 * an abrupt change of displayed contents, but once this setup is complete
 * further draw or erase operations in shared views will no longer alter the
 * scene object lists in the independent view.  To modify the independent
 * view's scene, it must be specifically set as the current view in libged.
 * Note also that when a view ceases to be independent, it's local object set
 * is compared to the shared object set and any objects in both are removed
 * from the local set.  However, any object in the independent list that are
 * not present in the shared set will remain, since there is no way for the
 * library to know if the intent is to preserve or remove such objects from the
 * scene.  Removal, as the destructive option, is the responsibility of the
 * application.
 *
 * Note that views may have localized scene objects even when not independent,
 * but they must be defined as view objects rather than database objects. */
int
_view_cmd_independent(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    const char *usage_string = "view [options] independent <view> [0|1]";
    const char *purpose_string = "make a view independent (1) or part of the default view set (0)";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    // We know we're the independent command - start processing args
    argc--; argv++;

    struct ged *gedp = gd->gedp;
    if (!argc) {
	bu_vls_printf(gedp->ged_result_str, "no view specified\n");
	return BRLCAD_ERROR;
    }

    struct bview *v = bv_set_find_view(&gedp->ged_views, argv[0]);
    if (!v) {
	bu_vls_printf(gedp->ged_result_str, "view %s not found\n", argv[0]);
	return BRLCAD_ERROR;
    }

    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "%d\n", bv_view_is_independent(v));
	return BRLCAD_OK;
    }

    if (BU_STR_EQUAL(argv[1], "1")) {
	if (bv_view_is_independent(v))
	    return BRLCAD_OK;

	struct _view_independent_path *paths = NULL;
	size_t path_cnt = 0;
	size_t path_cap = 0;
	if (gedp->i->ged_gdp->gd_draw_root) {
	    int ret = _view_independent_collect_paths(&paths, &path_cnt, &path_cap,
		    gedp->i->ged_gdp->gd_draw_root);
	    if (ret != BRLCAD_OK) {
		_view_independent_paths_free(paths, path_cnt);
		bu_vls_printf(gedp->ged_result_str, "failed to snapshot shared draw state\n");
		return BRLCAD_ERROR;
	    }
	}

	struct bview *cv = gedp->ged_gvp;
	gedp->ged_gvp = v;
	bsg_view_obj_ensure_root(gedp);
	gedp->ged_gvp = cv;

	if (!v->gv_draw_root || !bv_view_independent_scope(v, 1)) {
	    _view_independent_paths_free(paths, path_cnt);
	    bu_vls_printf(gedp->ged_result_str, "failed to create independent draw scope for %s\n",
		    argv[0]);
	    return BRLCAD_ERROR;
	}

	for (size_t i = 0; i < path_cnt; i++) {
	    struct bu_vls mode = BU_VLS_INIT_ZERO;
	    bu_vls_sprintf(&mode, "-m%d", paths[i].mode);
	    const char *av[7] = {"draw", "-R", "-V", NULL, NULL, NULL, NULL};
	    av[3] = bu_vls_cstr(&v->gv_name);
	    av[4] = bu_vls_cstr(&mode);
	    av[5] = paths[i].path;
	    ged_exec_draw(gedp, 6, av);
	    bu_vls_free(&mode);
	}
	_view_independent_paths_free(paths, path_cnt);
	return BRLCAD_OK;
    }

    if (BU_STR_EQUAL(argv[1], "0")) {
	if (!bv_view_is_independent(v))
	    return BRLCAD_OK;
	const char *z_av[4] = {"Z", "-V", NULL, "-g"};
	z_av[2] = bu_vls_cstr(&v->gv_name);
	ged_exec_Z(gedp, 4, z_av);
	bv_view_independent_scope_destroy(v);
	return BRLCAD_OK;
    }

    bu_vls_printf(gedp->ged_result_str, "invalid value supplied: %s (need 0 or 1)\n", argv[1]);
    return BRLCAD_ERROR;
}

int
_view_cmd_list(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    const char *usage_string = "view [options] ";
    const char *purpose_string = "list available views";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    struct ged *gedp = gd->gedp;
    struct bu_ptbl *views = bv_set_views(&gedp->ged_views);
    for (size_t i = 0; i < BU_PTBL_LEN(views); i++) {
	struct bview *v = (struct bview *)BU_PTBL_GET(views, i);
	if (v != gedp->ged_gvp) {
	    bu_vls_printf(gedp->ged_result_str, "  %s\n", bu_vls_cstr(&v->gv_name));
	} else {
	    bu_vls_printf(gedp->ged_result_str, "* %s\n", bu_vls_cstr(&v->gv_name));
	}
    }

    return BRLCAD_OK;
}

int
_view_cmd_quat(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    const char *usage_string = "view [options] quat [vals]";
    const char *purpose_string = "get/set quaternion of view";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    return _view_call_on_gd_view(gd, ged_quat_core, argc, argv);
}

int
_view_cmd_selections(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    const char *usage_string = "view [options] selections [options] [args]";
    const char *purpose_string = "manipulate view selections";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    argc--; argv++;

    struct bview *v = gd->cv;
    if (!v) {
	bu_vls_printf(gd->gedp->ged_result_str, "no current view selected\n");
	return BRLCAD_ERROR;
    }

    size_t sel_cnt = 0;
    if (v->bsg_root) {
	struct bsg_selection_set *ss =
	    bsg_scene_selection_get((bsg_node *)v->bsg_root, "active", 0);
	if (ss)
	    sel_cnt = bsg_selection_count(ss);
    }

    bu_vls_printf(gd->gedp->ged_result_str, "%llu",
	    (unsigned long long)sel_cnt);

    return BRLCAD_OK;
}

int
_view_cmd_size(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    const char *usage_string = "view [options] size [vals]";
    const char *purpose_string = "get/set view size";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    return _view_call_on_gd_view(gd, ged_size_core, argc, argv);
}

int
_view_cmd_snap(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    const char *usage_string = "view [options] snap [vals]";
    const char *purpose_string = "snap to view elements";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    return _view_call_on_gd_view(gd, ged_view_snap, argc, argv);
}

int
_view_cmd_ypr(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    const char *usage_string = "view [options] ypr [vals]";
    const char *purpose_string = "get/set yaw/pitch/roll of view";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    return _view_call_on_gd_view(gd, ged_ypr_core, argc, argv);
}


struct vZ_opt {
    int set;
    struct bu_vls vn;
};

static
int vZ_opt_read(struct bu_vls *msg, size_t argc, const char **argv, void *set_var)
{
    struct vZ_opt *vZ = (struct vZ_opt *)set_var;
    vZ->set = 1;
    if (bu_opt_vls(msg, argc, argv, (void *)&vZ->vn) == 1) {
	return 1;
    }
    return 0;
}

int
_view_cmd_vZ(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    struct ged *gedp = gd->gedp;
    const char *usage_string = "view [options] vZ [opts] [val|x y z]";
    const char *purpose_string = "get/set/calc view data vZ value";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string))
	return BRLCAD_OK;

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    int print_help = 0;
    struct vZ_opt calc_near = { 0, BU_VLS_INIT_ZERO };
    struct vZ_opt calc_far = { 0, BU_VLS_INIT_ZERO };
    struct bu_opt_desc d[4];
    BU_OPT(d[0], "h", "help", "",       NULL,  &print_help, "Print help");
    BU_OPT(d[1], "N", "near", "[obj]",  &vZ_opt_read,  &calc_near,  "Find vZ value of closest view obj vertex");
    BU_OPT(d[2], "F", "far",  "[obj]",  &vZ_opt_read,  &calc_far,   "Find vZ value of furthest view obj vertex");
    BU_OPT_NULL(d[3]);

    // We know we're the vZ command - start processing args
    argc--; argv++;

    int ac = bu_opt_parse(NULL, argc, argv, d);
    argc = ac;

    if (print_help || (calc_near.set && calc_far.set)) {
	bu_vls_printf(gedp->ged_result_str, "[WARNING] this command is deprecated - vZ values should be set on data objects\n\nUsage:\n%s", usage_string);
	return GED_HELP;
    }

    int calc_mode = -1;
    struct bu_vls calc_target = BU_VLS_INIT_ZERO;
    if (calc_near.set) {
	calc_mode = 0;
	bu_vls_sprintf(&calc_target, "%s", bu_vls_cstr(&calc_near.vn));
	bu_vls_free(&calc_near.vn);
    }
    if (calc_far.set) {
	calc_mode = 1;
	bu_vls_sprintf(&calc_target, "%s", bu_vls_cstr(&calc_far.vn));
	bu_vls_free(&calc_far.vn);
    }

    if (calc_mode != -1) {
	struct bview *v = gd->cv;
	if (bu_vls_strlen(&calc_target)) {
	    // User has specified a view object to use - try to find it
	    struct bv_scene_obj *wobj = bv_find_obj(v, bu_vls_cstr(&calc_target));
	    if (wobj) {
		fastf_t vZ = bv_vZ_calc(wobj, gd->cv, calc_mode);
		bu_vls_sprintf(gedp->ged_result_str, "%0.15f", vZ);
		return BRLCAD_OK;
	    } else {
		bu_vls_sprintf(gedp->ged_result_str, "View object %s not found", bu_vls_cstr(&calc_target));
		bu_vls_free(&calc_target);
		return BRLCAD_ERROR;
	    }
	} else {
	    // No specific view object to use - check all drawn
	    // view objects.
	    //
	    // Phase A1 (drawing_stack_modernization): replaced the legacy
	    // fourfold BV_VIEW_OBJS / BV_DB_OBJS ptbl scan with typed-API
	    // visits (bv_view_obj_visit + bv_view_objs_visit_db).  The
	    // BSG-aware helpers cover both shared and local scopes for
	    // view-only objects, and the DB visit walks every leaf of the
	    // BSG draw tree (preserving the previous "if no s_vlist, walk
	    // children" behaviour by virtue of the BSG traversal stopping
	    // at leaves). */
	    struct _view_vZ_ctx ctx;
	    ctx.cv = gd->cv;
	    ctx.calc_mode = calc_mode;
	    ctx.vZ = (calc_mode) ? -DBL_MAX : DBL_MAX;
	    ctx.have_vz = 0;
	    bv_view_obj_visit(v, BV_VIEW_OBJ_SCOPE_ALL, _view_vZ_visit_cb, &ctx);
	    bv_view_objs_visit_db(v, _view_vZ_visit_cb, &ctx);
	    if (ctx.have_vz) {
		bu_vls_sprintf(gedp->ged_result_str, "%0.15f", ctx.vZ);
	    }
	}
	return BRLCAD_OK;
    }

    /* Phase T-final (drawing_stack_modernization): the legacy get/set
     * scratch path that read or wrote `gv_tcl->gv_data_vZ` was removed
     * from libged.  `gv_data_vZ` is a Tcl-mode editing scratch scalar
     * that has no meaning to BSG rendering or to non-Tcl libged callers,
     * and the deprecation message above already tells users to set vZ
     * values on data objects directly.  Tcl callers that still need the
     * scratch can use the `data_vZ` command exposed by libtclcad
     * (commands.c), which keeps the gv_tcl-resident scalar consistent
     * with the rest of the Tcl editing-mode plumbing. */
    bu_vls_printf(gedp->ged_result_str, "[WARNING] this command is deprecated - vZ values should be set on data objects.\n\nUsage:\n%s", usage_string);
    return GED_HELP;
}
int
_view_cmd_width(void *ds, int argc, const char **argv)
{
    const char *usage_string = "view [options] width";
    const char *purpose_string = "report current width in pixels of view.";
    if (_view_cmd_msgs(ds, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    argc--; argv++;

    struct _ged_view_info *gd = (struct _ged_view_info *)ds;
    struct bview *v = gd->cv;
    bu_vls_printf(gd->gedp->ged_result_str, "%d\n", v->gv_width);
    return BRLCAD_OK;
}

int
_view_cmd_height(void *ds, int argc, const char **argv)
{
    const char *usage_string = "view [options] height";
    const char *purpose_string = "report current height in pixels of view.";
    if (_view_cmd_msgs(ds, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    argc--; argv++;

    struct _ged_view_info *gd = (struct _ged_view_info *)ds;
    struct bview *v = gd->cv;
    bu_vls_printf(gd->gedp->ged_result_str, "%d\n", v->gv_height);
    return BRLCAD_OK;
}

int
_view_cmd_print(struct ged *gedp, int argc, const char **argv)
{
    // ae
    ged_aet_core(gedp, argc, argv);
    char* ae = bu_vls_strdup(gedp->ged_result_str);

    // dir
    ged_viewdir_core(gedp, argc, argv);
    char* dir = bu_vls_strdup(gedp->ged_result_str);

    // center
    ged_center_core(gedp, argc, argv);
    char* center = bu_vls_strdup(gedp->ged_result_str);

    // eye
    ged_eye_core(gedp, argc, argv);
    char* eye = bu_vls_strdup(gedp->ged_result_str);

    // size
    ged_size_core(gedp, argc, argv);
    char* size = bu_vls_strdup(gedp->ged_result_str);

    // print
    bu_vls_trunc(gedp->ged_result_str, 0);
    bu_vls_printf(gedp->ged_result_str, "    ae: %s\n    dir: <%s>\n    center: (%s)\n    eye: (%s)\n    size: %s\n", ae, dir, center, eye, size);

    // cleanup
    bu_free(ae, "ae str free");
    bu_free(dir, "dir str free");
    bu_free(center, "center str free");
    bu_free(eye, "eye str free");
    bu_free(size, "size str free");

    return BRLCAD_OK;
}

int
_view_cmd_knob(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    const char *usage_string = "view [options] knob [vals]";
    const char *purpose_string = "low level view rotate/translate/scale operations";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string)) {
	return BRLCAD_OK;
    }

    return _view_call_on_gd_view(gd, ged_knob_core, argc, argv);
}

const struct bu_cmdtab _view_cmds[] = {
    { "ae",         _view_cmd_aet},
    { "aet",        _view_cmd_aet},
    { "center",     _view_cmd_center},
    { "eye",        _view_cmd_eye},
    { "faceplate",  _view_cmd_faceplate},
    { "height",     _view_cmd_height},
    { "independent",_view_cmd_independent},
    { "knob",       _view_cmd_knob},
    { "list",       _view_cmd_list},
    { "lod",        _view_cmd_lod},
    { "obj",        _view_cmd_objs},
    { "objs",       _view_cmd_objs},
    { "quat",       _view_cmd_quat},
    { "selections", _view_cmd_selections},
    { "size",       _view_cmd_size},
    { "snap",       _view_cmd_snap},
    { "vZ",         _view_cmd_vZ},
    { "width",      _view_cmd_width},
    { "ypr",        _view_cmd_ypr},
    { (char *)NULL,      NULL}
};

int
ged_view_core(struct ged *gedp, int argc, const char *argv[])
{
    int help = 0;
    struct _ged_view_info gd;
    gd.gedp = gedp;
    gd.cmds = _view_cmds;
    gd.cv = NULL;
    gd.gobj_dbpath = NULL;
    gd.verbosity = 0;

    // Sanity
    if (UNLIKELY(!gedp || !argc || !argv)) {
	return BRLCAD_ERROR;
    }

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    // We know we're the dm command - start processing args
    argc--; argv++;

    // See if we have any high level options set
    struct bu_vls vname = BU_VLS_INIT_ZERO;
    struct bu_opt_desc d[4];
    BU_OPT(d[0], "h", "help",    "",      NULL,               &help,         "Print help");
    BU_OPT(d[1], "v", "verbose", "",      &bu_opt_incr_long,  &gd.verbosity, "Verbose output");
    BU_OPT(d[2], "V", "view",    "name",  &bu_opt_vls,        &vname,        "Specified view (default is GED current)");
    BU_OPT_NULL(d[3]);

    gd.gopts = d;

    // High level options are only defined prior to the subcommand
    int cmd_pos = -1;
    for (int i = 0; i < argc; i++) {
	if (bu_cmd_valid(_view_cmds, argv[i]) == BRLCAD_OK) {
	    cmd_pos = i;
	    break;
	}
    }

    // Clear out any high level opts prior to subcommand
    int acnt = (cmd_pos >= 0) ? cmd_pos : argc;
    int ac_ret = bu_opt_parse(NULL, acnt, argv, d);
    if (ac_ret) {
	help = 1;
    } else {
	for (int i = 0; i < acnt; i++) {
	    argc--; argv++;
	}
    }

    if (help) {
	if (cmd_pos >= 0) {
	    argc = argc - cmd_pos;
	    argv = &argv[cmd_pos];
	    _ged_subcmd_help(gedp, (struct bu_opt_desc *)d, (const struct bu_cmdtab *)_view_cmds, "view", "[options] subcommand [args]", &gd, argc, argv);
	} else {
	    _ged_subcmd_help(gedp, (struct bu_opt_desc *)d, (const struct bu_cmdtab *)_view_cmds, "view", "[options] subcommand [args]", &gd, 0, NULL);
	}
	bu_vls_free(&vname);
	return BRLCAD_OK;
    }

    // Must have a subcommand
    if (cmd_pos == -1) {
	bu_vls_printf(gedp->ged_result_str, ": no valid subcommand specified\n");
	_ged_subcmd_help(gedp, (struct bu_opt_desc *)d, (const struct bu_cmdtab *)_view_cmds, "view", "[options] subcommand [args]", &gd, 0, NULL);
	bu_vls_free(&vname);
	return BRLCAD_ERROR;
    }

    // Either a view was specified, or we use the current view
    if (bu_vls_strlen(&vname)) {
	struct bu_ptbl *views = bv_set_views(&gedp->ged_views);
	for (size_t i = 0; i < BU_PTBL_LEN(views); i++) {
	    struct bview *v = (struct bview *)BU_PTBL_GET(views, i);
	    if (BU_STR_EQUAL(bu_vls_cstr(&vname), bu_vls_cstr(&v->gv_name))) {
		gd.cv = v;
		break;
	    }
	}
	if (!gd.cv) {
	    bu_vls_printf(gedp->ged_result_str, ": invalid view name: %s", bu_vls_cstr(&vname));
	    bu_vls_free(&vname);
	    return BRLCAD_ERROR;
	}
    } else {
	gd.cv = gedp->ged_gvp;
    }

    if (!gd.cv) {
	bu_vls_printf(gedp->ged_result_str, ": no view specified and no view listed as current in GED");
	bu_vls_free(&vname);
	return BRLCAD_ERROR;
    }

    int ret;
    if (bu_cmd(_view_cmds, argc, argv, 0, (void *)&gd, &ret) == BRLCAD_OK) {
	bu_vls_free(&vname);
	return ret;
    } else {
	bu_vls_printf(gedp->ged_result_str, "subcommand %s not defined", argv[0]);
    }

    bu_vls_free(&vname);
    return BRLCAD_ERROR;
}

#include "../include/plugin.h"

#define GED_VIEW_COMMANDS(X, XID) \
    X(ae, ged_aet_core, GED_CMD_DEFAULT) \
    X(aet, ged_aet_core, GED_CMD_DEFAULT) \
    X(autoview, ged_autoview_core, GED_CMD_DEFAULT) \
    X(center, ged_center_core, GED_CMD_DEFAULT) \
    X(data_lines, ged_view_data_lines, GED_CMD_DEFAULT) \
    X(eye, ged_eye_core, GED_CMD_DEFAULT) \
    X(eye_pt, ged_eye_core, GED_CMD_DEFAULT) \
    X(lookat, ged_lookat_core, GED_CMD_DEFAULT) \
    X(print, _view_cmd_print, GED_CMD_DEFAULT) \
    X(quat, ged_quat_core, GED_CMD_DEFAULT) \
    X(qvrot, ged_qvrot_core, GED_CMD_DEFAULT) \
    X(saveview, ged_saveview_core, GED_CMD_DEFAULT) \
    X(sdata_lines, ged_view_data_lines, GED_CMD_DEFAULT) \
    X(size, ged_size_core, GED_CMD_DEFAULT) \
    X(view, ged_view_core, GED_CMD_DEFAULT) \
    X(view2, ged_view_core, GED_CMD_DEFAULT) \
    X(view_func, ged_view_core, GED_CMD_DEFAULT) \
    X(viewdir, ged_viewdir_core, GED_CMD_DEFAULT) \
    X(ypr, ged_ypr_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_VIEW_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_view", 1, GED_VIEW_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
