/*                        O B J S . C P P
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
/** @file libged/view/objs.c
 *
 * Commands for view objects.
 *
 */

#include "common.h"

#include <ctype.h>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <set>
#include <string>

extern "C" {
#include "bu/cmd.h"
#include "bu/color.h"
#include "bu/opt.h"
#include "bu/path.h"
#include "bu/vls.h"
#include "bsg.h"
#include "bsg/defines.h"
#include "bsg/draw_source.h"
#include "raytrace.h"
#include "ged/bsg_ged_draw.h"
}
#include "./ged_view.h"
#include "../ged_private.h"

struct view_obj_walk_state {
    struct bsg_view *v;
    int list_view;
    int list_db;
    int local_only;
    const char *glob;
    std::set<std::string> names;
    std::map<std::string, struct bsg_node *> by_name;
};

static int
_view_obj_name_match(const char *glob, const char *name)
{
    if (!glob || !strlen(glob))
	return 1;
    return (bu_path_match(glob, name, 0) == 0) ? 1 : 0;
}

static void
_view_obj_walk_bsg(struct view_obj_walk_state &w, struct bsg_node *root)
{
    if (!root || !w.v)
	return;

    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	struct bsg_node *c = (struct bsg_node *)BU_PTBL_GET(&root->children, i);
	if (!c)
	    continue;
	if ((c->s_type_flags & BSG_NODE_VIEW_SCOPE) && c->s_v && c->s_v != w.v)
	    continue;

	struct bsg_node *s = c;
	if ((c->s_type_flags & BSG_NODE_VIEW_REF) && c->s_path)
	    s = (struct bsg_node *)c->s_path;

	if (s && BU_VLS_IS_INITIALIZED(&s->s_name)) {
	    int is_view = (s->s_type_flags & BSG_OBJ_VIEW) ? 1 : 0;
	    int is_db = (s->s_type_flags & BSG_OBJ_DB) ? 1 : 0;
	    int is_local = (s->s_type_flags & BSG_OBJ_LOCAL) ? 1 : 0;
	    int type_ok = (w.list_view && is_view) || (w.list_db && is_db);
	    if (type_ok && (!w.local_only || is_local)) {
		const char *n = bu_vls_cstr(&s->s_name);
		if (n && strlen(n) && _view_obj_name_match(w.glob, n)) {
		    w.names.insert(std::string(n));
		    if (w.by_name.find(std::string(n)) == w.by_name.end())
			w.by_name[std::string(n)] = s;
		}
	    }
	}

	_view_obj_walk_bsg(w, c);
    }
}

static struct bsg_node *
_view_obj_find(struct bsg_view *v, const char *name, int list_view, int list_db, int local_only)
{
    if (!v || !name || !strlen(name))
	return NULL;

    struct view_obj_walk_state w;
    w.v = v;
    w.list_view = list_view;
    w.list_db = list_db;
    w.local_only = local_only;
    w.glob = name;
    if (v->gv_draw_root)
	_view_obj_walk_bsg(w, (struct bsg_node *)v->gv_draw_root);
    std::map<std::string, struct bsg_node *>::iterator it = w.by_name.find(std::string(name));
    if (it != w.by_name.end())
	return it->second;
    return NULL;
}

static void
_view_obj_list(struct bu_vls *out, struct bsg_view *v, int list_view, int list_db, int local_only, const char *glob)
{
    if (!out || !v || !v->gv_draw_root)
	return;
    struct view_obj_walk_state w;
    w.v = v;
    w.list_view = list_view;
    w.list_db = list_db;
    w.local_only = local_only;
    w.glob = glob;
    _view_obj_walk_bsg(w, (struct bsg_node *)v->gv_draw_root);
    for (std::set<std::string>::iterator it = w.names.begin(); it != w.names.end(); ++it)
	bu_vls_printf(out, "%s\n", it->c_str());
}

static const char *
_view_obj_type(struct bsg_node *s)
{
    if (!s)
	return "unknown";
    if (s->s_type_flags & BSG_SHAPE_AXES)
	return "axes";
    if (s->s_type_flags & BSG_SHAPE_LINES)
	return "line";
    if (s->s_type_flags & BSG_SHAPE_LABELS)
	return "label";
    if (s->s_type_flags & BSG_SHAPE_POLYGONS)
	return "polygon";
    if (s->s_type_flags & BSG_OBJ_DB)
	return "gobj";
    return "object";
}

static void
_view_obj_mode_string(struct bu_vls *out, struct bsg_node *s)
{
    if (!out || !s || !s->s_os) {
	bu_vls_printf(out, "unknown");
	return;
    }
    switch (s->s_os->s_dmode) {
	case _GED_WIREFRAME:
	    bu_vls_printf(out, "wireframe");
	    break;
	case _GED_SHADED_MODE_BOTS:
	case _GED_SHADED_MODE_ALL:
	    bu_vls_printf(out, "shaded");
	    break;
	case _GED_BOOL_EVAL:
	    bu_vls_printf(out, "evaluated");
	    break;
	case _GED_HIDDEN_LINE:
	    bu_vls_printf(out, "hidden_line");
	    break;
	case _GED_SHADED_MODE_EVAL:
	    bu_vls_printf(out, "shaded_evaluated");
	    break;
	case _GED_WIREFRAME_EVAL:
	    bu_vls_printf(out, "wireframe_evaluated");
	    break;
	default:
	    bu_vls_printf(out, "unknown");
	    break;
    }
}

int
_objs_cmd_draw(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    struct ged *gedp = gd->gedp;
    const char *usage_string = "view obj set <name> draw [0|1|UP|DOWN]";
    const char *purpose_string = "toggle view polygons";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string))
	return BRLCAD_OK;

    argc--; argv++;

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    struct bsg_node *s = gd->s;
    if (!gd->s) {
	bu_vls_printf(gedp->ged_result_str, "No view object named %s\n", gd->vobj);
	return BRLCAD_ERROR;
    }

    if (argc == 0) {
	if (s->s_flag == UP) {
	    bu_vls_printf(gedp->ged_result_str, "UP\n");
	} else {
	    bu_vls_printf(gedp->ged_result_str, "DOWN\n");
	}
	return BRLCAD_OK;
    }

    if (BU_STR_EQUAL(argv[0], "DOWN")) {
	s->s_flag = DOWN;
	return BRLCAD_OK;
    }
    if (BU_STR_EQUAL(argv[0], "UP")) {
	s->s_flag = UP;
	return BRLCAD_OK;
    }

    bu_vls_printf(gedp->ged_result_str, "Invalid argument %s\n", argv[0]);
    return BRLCAD_ERROR;
}

int
_objs_cmd_delete(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    struct ged *gedp = gd->gedp;
    const char *usage_string = "view obj remove <name>";
    const char *purpose_string = "delete view object";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string))
	return BRLCAD_OK;

    argc--; argv++;

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    struct bsg_node *s = gd->s;
    if (!s) {
	bu_vls_printf(gedp->ged_result_str, "No view object named %s\n", gd->vobj);
	return BRLCAD_ERROR;
    }
    if (!(s->s_type_flags & BSG_SHAPE_VIEWONLY)) {
	bu_vls_printf(gedp->ged_result_str, "View object %s is associated with a database object - use 'erase' cmd to clear\n", gd->vobj);
	return BRLCAD_ERROR;
    }
    bsg_obj_put(s);

    return BRLCAD_OK;
}

int
_objs_cmd_color(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    struct ged *gedp = gd->gedp;
    const char *usage_string = "view obj set <name> color [r/g/b]";
    const char *purpose_string = "show/set obj color";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string))
	return BRLCAD_OK;

    argc--; argv++;

    int recurse = 0;

    struct bu_opt_desc d[2];
    BU_OPT(d[0], "r", "recursive",       "",  NULL,  &recurse,  "Report (or set) color of all child objects");
    BU_OPT_NULL(d[1]);

    int ac = bu_opt_parse(NULL, argc, argv, d);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    struct bsg_node *s = gd->s;
    if (!gd->s) {
	bu_vls_printf(gedp->ged_result_str, "No view object named %s\n", gd->vobj);
	return BRLCAD_ERROR;
    }

    if (ac == 0) {
	bu_vls_printf(gedp->ged_result_str, "%d/%d/%d\n", s->s_color[0], s->s_color[1], s->s_color[2]);
	if (recurse) {
	    std::queue<struct bsg_node *> sobjs;
	    sobjs.push(s);
	    while (!sobjs.empty()) {
		struct bsg_node *sc = sobjs.front();
		sobjs.pop();
		bu_vls_printf(gedp->ged_result_str, "%s: %d/%d/%d\n", bu_vls_cstr(&sc->s_name), sc->s_color[0], sc->s_color[1], sc->s_color[2]);
		for (size_t i = 0; i < BU_PTBL_LEN(&sc->children); i++) {
		    struct bsg_node *scn = (struct bsg_node *)BU_PTBL_GET(&sc->children, i);
		    sobjs.push(scn);
		}
	    }
	}
	return BRLCAD_OK;
    }
    struct bu_color val;
    if (bu_opt_color(NULL, 1, (const char **)&argv[0], (void *)&val) != 1) {
	bu_vls_printf(gedp->ged_result_str, "Invalid argument %s\n", argv[0]);
	return BRLCAD_ERROR;
    }

    bu_color_to_rgb_chars(&val, s->s_color);
    if (recurse) {
	if (recurse) {
	    std::queue<struct bsg_node *> sobjs;
	    sobjs.push(s);
	    while (!sobjs.empty()) {
		struct bsg_node *sc = sobjs.front();
		sobjs.pop();
		bu_color_to_rgb_chars(&val, sc->s_color);
		for (size_t i = 0; i < BU_PTBL_LEN(&sc->children); i++) {
		    struct bsg_node *scn = (struct bsg_node *)BU_PTBL_GET(&sc->children, i);
		    sobjs.push(scn);
		}
	    }
	}
    }
    return BRLCAD_OK;
}

int
_objs_cmd_arrow(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    struct ged *gedp = gd->gedp;
    const char *usage_string = "view obj set <name> arrow [0|1] [width [#]] [length [#]]";
    const char *purpose_string = "toggle arrow drawing, for those objects that support it";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string))
	return BRLCAD_OK;

    argc--; argv++;

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    struct bsg_node *s = gd->s;
    if (!gd->s) {
	bu_vls_printf(gedp->ged_result_str, "No view object named %s\n", gd->vobj);
	return BRLCAD_ERROR;
    }

    if (argc == 0) {
	bu_vls_printf(gedp->ged_result_str, "%d\n", s->s_arrow);
	return BRLCAD_OK;
    }

    if (BU_STR_EQUAL(argv[0], "0")) {
	s->s_arrow = 0;
	return BRLCAD_OK;
    }
    if (BU_STR_EQUAL(argv[0], "1")) {
	s->s_arrow = 1;
	return BRLCAD_OK;
    }
    if (BU_STR_EQUAL(argv[0], "width"))  {
	if (argc == 2) {
	    if (bu_opt_fastf_t(NULL, 1, (const char **)&argv[1], (void *)&s->s_os->s_arrow_tip_width) != 1) {
		bu_vls_printf(gedp->ged_result_str, "Invalid argument %s\n", argv[0]);
		return BRLCAD_ERROR;
	    }
	    return BRLCAD_OK;
	} else {
	    bu_vls_printf(gedp->ged_result_str, "%f\n", s->s_os->s_arrow_tip_width);
	    return BRLCAD_OK;
	}
    }

    if (BU_STR_EQUAL(argv[0], "length"))  {
	if (argc == 2) {
	    if (bu_opt_fastf_t(NULL, 1, (const char **)&argv[1], (void *)&s->s_os->s_arrow_tip_length) != 1) {
		bu_vls_printf(gedp->ged_result_str, "Invalid argument %s\n", argv[0]);
		return BRLCAD_ERROR;
	    }
	    return BRLCAD_OK;
	} else {
	    bu_vls_printf(gedp->ged_result_str, "%f\n", s->s_os->s_arrow_tip_length);
	    return BRLCAD_OK;
	}
    }

    bu_vls_printf(gedp->ged_result_str, "Invalid argument %s\n", argv[0]);
    return BRLCAD_ERROR;
}

int
_objs_cmd_lcnt(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    struct ged *gedp = gd->gedp;
    const char *usage_string = "view obj info <name> lcnt";
    const char *purpose_string = "print the number of vlist entities";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string))
	return BRLCAD_OK;

    argc--; argv++;

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    struct bsg_node *s = gd->s;
    if (!gd->s) {
	bu_vls_printf(gedp->ged_result_str, "No view object named %s\n", gd->vobj);
	return BRLCAD_ERROR;
    }
    bu_vls_printf(gedp->ged_result_str, "%d\n", bu_list_len(bsg_node_vlist_head(s)));
    return BRLCAD_OK;
}

static void
update_recurse(struct bsg_node *s, struct bsg_view *v, int flags)
{
    for (size_t i = 0; i < BU_PTBL_LEN(&s->children); i++) {
	struct bsg_node *sc = (struct bsg_node *)BU_PTBL_GET(&s->children, i);
	update_recurse(sc, v, flags);
    }
    s->s_changed = 0;
    s->s_v = v;
    if (s->s_update_callback)
	(*s->s_update_callback)(s, v, 0);
}

int
_objs_cmd_update(void *bs, int argc, const char **argv)
{
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    struct ged *gedp = gd->gedp;
    const char *usage_string = "view obj set <name> update [x y]";
    const char *purpose_string = "update object";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string))
	return BRLCAD_OK;

    argc--; argv++;

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    struct bsg_node *s = gd->s;
    if (!gd->s) {
	bu_vls_printf(gedp->ged_result_str, "No view object named %s\n", gd->vobj);
	return BRLCAD_ERROR;
    }


    if (argc && (argc != 2)) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s\n", usage_string);
	return BRLCAD_ERROR;
    }

    struct bsg_view *v = gd->cv;
    if (argc) {
	int x, y;
	if (bu_opt_int(NULL, 1, (const char **)&argv[0], (void *)&x) != 1 || x < 0) {
	    bu_vls_printf(gedp->ged_result_str, "Invalid argument %s\n", argv[0]);
	    return BRLCAD_ERROR;
	}
	if (bu_opt_int(NULL, 1, (const char **)&argv[1], (void *)&y) != 1 || y < 0) {
	    bu_vls_printf(gedp->ged_result_str, "Invalid argument %s\n", argv[1]);
	    return BRLCAD_ERROR;
	}
	v->gv_mouse_x = x;
	v->gv_mouse_y = y;
	bsg_screen_pt(&v->gv_point, x, y, v);
    }

    update_recurse(s, v, 0);

    return BRLCAD_OK;
}

const struct bu_cmdtab _obj_cmds[] = {
    { "draw",       _objs_cmd_draw},
    { "del",        _objs_cmd_delete},
    //{ "info",       _objs_cmd_info},
    { "update",     _objs_cmd_update},
    { "color",      _objs_cmd_color},
    { "axes",       _view_cmd_axes},
    { "arrow",      _objs_cmd_arrow},
    { "label",      _view_cmd_labels},
    { "lcnt",       _objs_cmd_lcnt},
    { "line",       _view_cmd_lines},
    { "polygon",    _view_cmd_polygons},
    { (char *)NULL,      NULL}
};

extern "C" int
_view_cmd_objs(void *bs, int argc, const char **argv)
{
    int help = 0;
    int list_view = 0;
    int list_db = 0;
    int not_shared = 0;
    struct bu_vls gobj_path = BU_VLS_INIT_ZERO;
    struct _ged_view_info *gd = (struct _ged_view_info *)bs;
    struct ged *gedp = gd->gedp;

    const char *usage_string = "view [options] obj [options] [args]";
    const char *purpose_string = "manipulate view objects";
    if (_view_cmd_msgs(bs, argc, argv, usage_string, purpose_string))
	return BRLCAD_OK;

    if (!gd->cv) {
	bu_vls_printf(gedp->ged_result_str, ": no view current in GED");
	return BRLCAD_ERROR;
    }
    if (!gd->cv->gv_draw_root) {
	struct bsg_view *cv = gedp->ged_gvp;
	gedp->ged_gvp = gd->cv;
	bsg_view_obj_ensure_root(gedp);
	gedp->ged_gvp = cv;
    }

    // See if we have any high level options set
    struct bu_opt_desc d[6];
    BU_OPT(d[0], "h", "help",        "",  NULL,  &help,       "Print help");
    BU_OPT(d[1], "L", "local",       "",  NULL,  &not_shared, "Object is scoped only to current/specified view");
    BU_OPT(d[2], "G", "geom_only",   "",  NULL,  &list_db,    "List view scene objects representing .g database objs");
    BU_OPT(d[3],  "", "view_only",   "",  NULL,  &list_view,  "List view-only scene objects (default)");
    BU_OPT(d[4], "g", "gobj",        "dbpath",  &bu_opt_vls, &gobj_path, "Use geometry path for gobj create");
    BU_OPT_NULL(d[5]);

    gd->gopts = d;

    // We know we're the obj command - start processing args
    argc--; argv++;

    std::set<std::string> unified_cmds = {"create", "remove", "list", "info", "set"};

    // High level options are only defined prior to the subcommand.  Find
    // the first non-option argument to check against the unified subcommand set.
    int first_pos = -1;
    int arg_idx = 0;
    while (arg_idx < argc) {
	if (argv[arg_idx][0] == '-') {
	    if ((BU_STR_EQUAL(argv[arg_idx], "-g") || BU_STR_EQUAL(argv[arg_idx], "--gobj")) && arg_idx + 1 < argc) {
		arg_idx += 2;
		continue;
	    }
	    arg_idx++;
	    continue;
	}
	first_pos = arg_idx;
	break;
    }

    int cmd_pos = -1;
    if (first_pos >= 0 && unified_cmds.find(std::string(argv[first_pos])) != unified_cmds.end())
	cmd_pos = first_pos;

    int acnt = (first_pos >= 0) ? first_pos : argc;
    (void)bu_opt_parse(NULL, acnt, argv, d);

    if (!list_db && !list_view)
	list_view = 1;

    gd->local_obj = not_shared;
    gd->gobj_dbpath = bu_vls_strlen(&gobj_path) ? bu_vls_cstr(&gobj_path) : NULL;

    struct bsg_view *v = gd->cv;
    if (help) {
	int hargc = (cmd_pos >= 0) ? argc - cmd_pos : 0;
	const char **hargv = (cmd_pos >= 0) ? &argv[cmd_pos] : NULL;
	_ged_subcmd_help(gedp, (struct bu_opt_desc *)d, (const struct bu_cmdtab *)_obj_cmds, "view obj", "[options] subcommand [args]", gd, hargc, hargv);
	bu_vls_free(&gobj_path);
	return BRLCAD_OK;
    }

    // No subcommand: default list (only when there are no positional args at all)
    if (cmd_pos < 0) {
	if (first_pos >= 0) {
	    bu_vls_free(&gobj_path);
	    bu_vls_printf(gd->gedp->ged_result_str,
		    "Unsupported subcommand '%s' (valid: create, remove, list, info, set)",
		    argv[first_pos]);
	    return BRLCAD_ERROR;
	}
	_view_obj_list(gd->gedp->ged_result_str, v, list_view, list_db, gd->local_obj, NULL);
	bu_vls_free(&gobj_path);
	return BRLCAD_OK;
    }

    if (cmd_pos >= argc || cmd_pos < 0) {
	bu_vls_free(&gobj_path);
	bu_vls_printf(gd->gedp->ged_result_str, "need subcommand");
	return BRLCAD_ERROR;
    }

    int subcmd_argc = argc - cmd_pos;
    const char **subcmd_argv = argv + cmd_pos;

    // Unified grammar
    if (unified_cmds.find(std::string(subcmd_argv[0])) != unified_cmds.end()) {
	const char *ucmd = subcmd_argv[0];
	if (BU_STR_EQUAL(ucmd, "list")) {
	    const char *glob = (subcmd_argc > 1) ? subcmd_argv[1] : NULL;
	    if (subcmd_argc > 2) {
		bu_vls_free(&gobj_path);
		bu_vls_printf(gd->gedp->ged_result_str, "Usage: view obj [-V view] [-L] list [glob_pattern]");
		return BRLCAD_ERROR;
	    }
	    _view_obj_list(gd->gedp->ged_result_str, v, list_view, list_db, gd->local_obj, glob);
	    bu_vls_free(&gobj_path);
	    return BRLCAD_OK;
	}

	if (BU_STR_EQUAL(ucmd, "create")) {
	    if (gd->gobj_dbpath) {
		if (subcmd_argc != 2) {
		    bu_vls_free(&gobj_path);
		    bu_vls_printf(gd->gedp->ged_result_str, "Usage: view obj [-V view] [-L] -g <dbpath> create <name>");
		    return BRLCAD_ERROR;
		}
		const char *gargv[4] = {"create", gd->gobj_dbpath, subcmd_argv[1], NULL};
		int ret = _gobjs_cmd_create(bs, 3, gargv);
		bu_vls_free(&gobj_path);
		return ret;
	    }
	    if (subcmd_argc < 3) {
		bu_vls_free(&gobj_path);
		bu_vls_printf(gd->gedp->ged_result_str, "Usage: view obj [-V view] [-L] create <name> <type> <args...>");
		return BRLCAD_ERROR;
	    }
	    gd->vobj = subcmd_argv[1];
	    const int find_view_objs = 1;
	    const int find_db_objs = 1;
	    gd->s = _view_obj_find(v, gd->vobj, find_view_objs, find_db_objs, gd->local_obj);
	    const char *otype = subcmd_argv[2];
	    const char **cargv = subcmd_argv + 2;
	    int cargc = subcmd_argc - 2;
	    int ret = BRLCAD_ERROR;
	    if (BU_STR_EQUAL(otype, "line")) {
		ret = _view_cmd_lines(bs, cargc, cargv);
	    } else if (BU_STR_EQUAL(otype, "axes")) {
		ret = _view_cmd_axes(bs, cargc, cargv);
	    } else if (BU_STR_EQUAL(otype, "label")) {
		ret = _view_cmd_labels(bs, cargc, cargv);
	    } else if (BU_STR_EQUAL(otype, "polygon")) {
		ret = _view_cmd_polygons(bs, cargc, cargv);
	    } else if (BU_STR_EQUAL(otype, "arrow")) {
		if (cargc < 5) {
		    bu_vls_printf(gd->gedp->ged_result_str, "Usage: view obj [-V view] [-L] create <name> arrow x y z");
		    bu_vls_free(&gobj_path);
		    return BRLCAD_ERROR;
		}
		ret = _view_cmd_lines(bs, cargc, cargv);
		if (ret == BRLCAD_OK) {
		    gd->s = _view_obj_find(v, gd->vobj, 1, 1, gd->local_obj);
		    const char *aargv[3] = {"arrow", "1", NULL};
		    ret = _objs_cmd_arrow(bs, 2, aargv);
		}
	    } else {
		bu_vls_printf(gd->gedp->ged_result_str, "Unsupported view object type %s", otype);
		bu_vls_free(&gobj_path);
		return BRLCAD_ERROR;
	    }
	    bu_vls_free(&gobj_path);
	    return ret;
	}

	if (subcmd_argc < 2) {
	    bu_vls_free(&gobj_path);
	    bu_vls_printf(gd->gedp->ged_result_str, "Usage: view obj [-V view] [-L] %s <name>", ucmd);
	    return BRLCAD_ERROR;
	}
	gd->vobj = subcmd_argv[1];
	gd->s = _view_obj_find(v, gd->vobj, list_view, list_db, gd->local_obj);

	if (BU_STR_EQUAL(ucmd, "remove")) {
	    const char *rargv[2] = {"del", NULL};
	    int ret = _objs_cmd_delete(bs, 1, rargv);
	    bu_vls_free(&gobj_path);
	    return ret;
	}

	if (BU_STR_EQUAL(ucmd, "info")) {
	    if (!gd->s) {
		bu_vls_free(&gobj_path);
		bu_vls_printf(gd->gedp->ged_result_str, "No view object named %s\n", gd->vobj);
		return BRLCAD_ERROR;
	    }
	    if (subcmd_argc == 2) {
		bu_vls_printf(gedp->ged_result_str, "%s %s\n", gd->vobj, _view_obj_type(gd->s));
		bu_vls_free(&gobj_path);
		return BRLCAD_OK;
	    }
	    if (BU_STR_EQUAL(subcmd_argv[2], "mode")) {
		_view_obj_mode_string(gedp->ged_result_str, gd->s);
		bu_vls_free(&gobj_path);
		return BRLCAD_OK;
	    }
	    if (BU_STR_EQUAL(subcmd_argv[2], "color")) {
		const char *fargv[2] = {"color", NULL};
		int ret = _objs_cmd_color(bs, 1, fargv);
		bu_vls_free(&gobj_path);
		return ret;
	    }
	    if (BU_STR_EQUAL(subcmd_argv[2], "draw")) {
		const char *fargv[2] = {"draw", NULL};
		int ret = _objs_cmd_draw(bs, 1, fargv);
		bu_vls_free(&gobj_path);
		return ret;
	    }
	    if (BU_STR_EQUAL(subcmd_argv[2], "lcnt")) {
		const char *fargv[2] = {"lcnt", NULL};
		int ret = _objs_cmd_lcnt(bs, 1, fargv);
		bu_vls_free(&gobj_path);
		return ret;
	    }
	    if (BU_STR_EQUAL(subcmd_argv[2], "type")) {
		bu_vls_printf(gedp->ged_result_str, "%s\n", _view_obj_type(gd->s));
		bu_vls_free(&gobj_path);
		return BRLCAD_OK;
	    }
	    bu_vls_free(&gobj_path);
	    bu_vls_printf(gd->gedp->ged_result_str, "Unsupported info field %s", subcmd_argv[2]);
	    return BRLCAD_ERROR;
	}

	if (BU_STR_EQUAL(ucmd, "set")) {
	    if (!gd->s || subcmd_argc < 4) {
		bu_vls_free(&gobj_path);
		bu_vls_printf(gd->gedp->ged_result_str, "Usage: view obj [-V view] [-L] set <name> <field> <value>");
		return BRLCAD_ERROR;
	    }
	    if (BU_STR_EQUAL(subcmd_argv[2], "draw")) {
		if (subcmd_argc == 4 && (BU_STR_EQUAL(subcmd_argv[3], "0") || BU_STR_EQUAL(subcmd_argv[3], "1"))) {
		    const char *dargv[3] = {"draw", BU_STR_EQUAL(subcmd_argv[3], "1") ? "UP" : "DOWN", NULL};
		    int ret = _objs_cmd_draw(bs, 2, dargv);
		    bu_vls_free(&gobj_path);
		    return ret;
		}
		int ret = _objs_cmd_draw(bs, subcmd_argc - 2, subcmd_argv + 2);
		bu_vls_free(&gobj_path);
		return ret;
	    }
	    if (BU_STR_EQUAL(subcmd_argv[2], "color")) {
		int ret = _objs_cmd_color(bs, subcmd_argc - 2, subcmd_argv + 2);
		bu_vls_free(&gobj_path);
		return ret;
	    }
	    if (BU_STR_EQUAL(subcmd_argv[2], "arrow")) {
		int ret = _objs_cmd_arrow(bs, subcmd_argc - 2, subcmd_argv + 2);
		bu_vls_free(&gobj_path);
		return ret;
	    }
	    if (BU_STR_EQUAL(subcmd_argv[2], "update")) {
		int ret = _objs_cmd_update(bs, subcmd_argc - 2, subcmd_argv + 2);
		bu_vls_free(&gobj_path);
		return ret;
	    }
	    bu_vls_free(&gobj_path);
	    bu_vls_printf(gd->gedp->ged_result_str, "Unsupported set field %s", subcmd_argv[2]);
	    return BRLCAD_ERROR;
	}
    }

    bu_vls_free(&gobj_path);
    const char *bad_subcmd = (cmd_pos >= 0 && cmd_pos < argc) ? argv[cmd_pos] : "(none)";
    bu_vls_printf(gd->gedp->ged_result_str,
	    "Unsupported subcommand %s (valid: create, remove, list, info, set)",
	    bad_subcmd);
    return BRLCAD_ERROR;
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
