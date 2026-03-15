/*                            F B . C
 * BRL-CAD
 *
 * Copyright (c) 1997-2025 United States Government as represented by
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
/** @addtogroup libstruct fb */
/** @{ */
/**
 *
 * A framebuffer object contains the attributes and
 * methods for controlling framebuffers.
 *
 * Framebuffer (fb/fbo) Tcl commands — stubbed out (dm backend going away).
 *
 */
/** @} */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "tcl.h"
#include "bu/cmd.h"
#include "bu/color.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "ged.h"
#include "tclcad.h"
#include "./tclcad_private.h"
#include "./view/view.h"

#define FB_OBJ_LIST_INIT_CAPACITY 8

struct fb_obj {
    struct bu_vls fbo_name;/* framebuffer object name/cmd */
    struct fbserv_obj fbo_fbs;/* fbserv object */
    Tcl_Interp *fbo_interp;
};

static struct fb_obj fb_obj_list_init[FB_OBJ_LIST_INIT_CAPACITY];

static struct fb_obj_list {
    size_t capacity, size;
    struct fb_obj *objs;
} fb_objs;


/*
 * Called when the object is destroyed.
 */
static void
fbo_deleteProc(void *clientData)
{
    struct fb_obj *fbop = (struct fb_obj *)clientData;

    bu_vls_free(&fbop->fbo_name);
    {
	size_t remaining = (size_t)((fb_objs.objs + fb_objs.size) - (fbop + 1));
	memmove(fbop, fbop + 1, sizeof(struct fb_obj) * remaining);
    }
    fb_objs.size--;
}


/*
 * Close a framebuffer object — stub.
 *
 * Usage:
 * procname close
 */
static int
fbo_close_tcl(void *clientData, int argc, const char **UNUSED(argv))
{
    struct fb_obj *fbop = (struct fb_obj *)clientData;

    if (argc != 2) {
bu_log("ERROR: expecting two arguments\n");
return BRLCAD_ERROR;
    }

    Tcl_DeleteCommand(fbop->fbo_interp, bu_vls_addr(&fbop->fbo_name));
    return BRLCAD_OK;
}


static int
fbo_clear_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}

static int
fbo_cursor_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}

static int
fbo_getcursor_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}

static int
fbo_refresh_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}

static int
fbo_listen_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}

static int
fbo_pixel_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}

static int
fbo_cell_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}

static int
fbo_flush_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}

static int
fbo_getheight_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}

static int
fbo_getwidth_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}

static int
fbo_getsize_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}

static int
fbo_rect_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}

static int
fbo_configure_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    Tcl_AppendResult(NULL, "framebuffer: dm backend going away - use Obol", (char *)NULL);
    return TCL_ERROR;
}


/*
 * Generic interface for framebuffer object routines.
 * Usage:
 * procname cmd ?args?
 *
 * Returns: result of FB command.
 */
static int
fbo_cmd(ClientData clientData, Tcl_Interp *UNUSED(interp), int argc, const char **argv)
{
    int ret;

    static struct bu_cmdtab fbo_cmds[] = {
{"cell",fbo_cell_tcl},
{"clear",fbo_clear_tcl},
{"close",fbo_close_tcl},
{"configure",fbo_configure_tcl},
{"cursor",fbo_cursor_tcl},
{"pixel",fbo_pixel_tcl},
{"flush",fbo_flush_tcl},
{"getcursor",fbo_getcursor_tcl},
{"getheight",fbo_getheight_tcl},
{"getsize",fbo_getsize_tcl},
{"getwidth",fbo_getwidth_tcl},
{"listen",fbo_listen_tcl},
{"rect",fbo_rect_tcl},
{"refresh",fbo_refresh_tcl},
{(const char *)NULL, BU_CMD_NULL}
    };

    if (bu_cmd(fbo_cmds, argc, argv, 1, clientData, &ret) == BRLCAD_OK)
return ret;

    bu_log("ERROR: '%s' command not found\n", argv[1]);
    return BRLCAD_ERROR;
}


/*
 * Open/create a framebuffer object — stub (dm backend going away).
 *
 * Usage:
 * fb_open [name device [args]]
 */
static int
fbo_open_tcl(void *UNUSED(clientData), Tcl_Interp *interp, int argc, const char **argv)
{
    struct fb_obj *fbop;
    struct bu_vls vls = BU_VLS_INIT_ZERO;
    size_t i;

    if (argc == 1) {
/* get list of framebuffer objects */
for (i = 0; i < fb_objs.size; i++) {
    fbop = &fb_objs.objs[i];
    Tcl_AppendResult(interp, bu_vls_addr(&fbop->fbo_name), " ", (char *)NULL);
}
return BRLCAD_OK;
    }

    if (argc < 3) {
bu_vls_printf(&vls, "helplib fb_open");
Tcl_Eval(interp, bu_vls_addr(&vls));
bu_vls_free(&vls);
return BRLCAD_ERROR;
    }

    /* dm/fb backend is going away — register a stub fbo so scripts don't error */
    if (fb_objs.capacity == 0) {
fb_objs.capacity = FB_OBJ_LIST_INIT_CAPACITY;
fb_objs.objs = fb_obj_list_init;
    } else if (fb_objs.size == fb_objs.capacity && fb_objs.capacity == FB_OBJ_LIST_INIT_CAPACITY) {
fb_objs.capacity *= 2;
fb_objs.objs = (struct fb_obj *)bu_malloc(
sizeof (struct fb_obj) * fb_objs.capacity,
"first resize of fb_obj list");
    } else if (fb_objs.size == fb_objs.capacity) {
fb_objs.capacity *= 2;
fb_objs.objs = (struct fb_obj *)bu_realloc(
fb_objs.objs,
sizeof (struct fb_obj) * fb_objs.capacity,
"additional resize of fb_obj list");
    }

    fbop = &fb_objs.objs[fb_objs.size];
    bu_vls_init(&fbop->fbo_name);
    bu_vls_strcpy(&fbop->fbo_name, argv[1]);
    fbop->fbo_fbs.fbs_fbp = NULL;
    fbop->fbo_fbs.fbs_listener.fbsl_fbsp = &fbop->fbo_fbs;
    fbop->fbo_fbs.fbs_listener.fbsl_fd = -1;
    fbop->fbo_fbs.fbs_listener.fbsl_port = -1;
    fbop->fbo_interp = interp;

    fb_objs.size++;

    (void)Tcl_CreateCommand(interp,
    bu_vls_addr(&fbop->fbo_name),
    (Tcl_CmdProc *)fbo_cmd,
    (ClientData)fbop,
    fbo_deleteProc);

    /* Return new function name as result */
    Tcl_ResetResult(interp);
    Tcl_AppendResult(interp, bu_vls_addr(&fbop->fbo_name), (char *)NULL);
    return BRLCAD_OK;
}


int
Fbo_Init(Tcl_Interp *interp)
{
    if (fb_objs.capacity == 0) {
fb_objs.capacity = FB_OBJ_LIST_INIT_CAPACITY;
fb_objs.objs = fb_obj_list_init;
    }

    (void)Tcl_CreateCommand(interp, "fb_open", (Tcl_CmdProc *)fbo_open_tcl,
    (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);

    return BRLCAD_OK;
}



void
to_fbs_callback(void *clientData)
{
    bsg_view *gdvp = (bsg_view *)clientData;

    to_refresh_view(gdvp);
}


int
to_close_fbs(bsg_view *gdvp)
{
    struct tclcad_view_data *tvd = (struct tclcad_view_data *)gdvp->u_data;
    if (tvd->gdv_fbs.fbs_fbp == NULL)
return TCL_OK;

    /* dm and its framebuffer are going away; Obol manages its own display */
    tvd->gdv_fbs.fbs_fbp = NULL;

    return TCL_OK;
}


/*
 * Open/activate the display manager's framebuffer — stub.
 * dm and its framebuffer are going away; Obol manages its own display.
 */
int
to_open_fbs(bsg_view *UNUSED(gdvp), Tcl_Interp *UNUSED(interp))
{
    /* dm and its framebuffer are going away; Obol manages its own display */
    return TCL_OK;
}



int
to_set_fb_mode(struct ged *gedp,
       int argc,
       const char *argv[],
       ged_func_ptr UNUSED(func),
       const char *usage,
       int UNUSED(maxargs))
{
    int mode;

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
return GED_HELP;
    }

    if (3 < argc) {
bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
return BRLCAD_ERROR;
    }

    bsg_view *gdvp = bsg_scene_find_view(&gedp->ged_views, argv[1]);
    if (!gdvp) {
bu_vls_printf(gedp->ged_result_str, "View not found - %s", argv[1]);
return BRLCAD_ERROR;
    }

    /* Get fb mode */
    if (argc == 2) {
struct tclcad_view_data *tvd = (struct tclcad_view_data *)gdvp->u_data;
bu_vls_printf(gedp->ged_result_str, "%d", tvd->gdv_fbs.fbs_mode);
return BRLCAD_OK;
    }

    /* Set fb mode */
    if (bu_sscanf(argv[2], "%d", &mode) != 1) {
bu_vls_printf(gedp->ged_result_str, "set_fb_mode: bad value - %s\n", argv[2]);
return BRLCAD_ERROR;
    }

    if (mode < 0)
mode = 0;
    else if (TCLCAD_OBJ_FB_MODE_OVERLAY < mode)
mode = TCLCAD_OBJ_FB_MODE_OVERLAY;

    {
struct tclcad_view_data *tvd = (struct tclcad_view_data *)gdvp->u_data;
tvd->gdv_fbs.fbs_mode = mode;
    }
    to_refresh_view(gdvp);

    return BRLCAD_OK;
}


int
to_listen(struct ged *gedp,
  int argc,
  const char *argv[],
  ged_func_ptr UNUSED(func),
  const char *usage,
  int UNUSED(maxargs))
{
    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
return GED_HELP;
    }

    if (3 < argc) {
bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
return BRLCAD_ERROR;
    }

    bsg_view *gdvp = bsg_scene_find_view(&gedp->ged_views, argv[1]);
    if (!gdvp) {
bu_vls_printf(gedp->ged_result_str, "View not found - %s", argv[1]);
return BRLCAD_ERROR;
    }

    struct tclcad_view_data *tvd = (struct tclcad_view_data *)gdvp->u_data;
    if (tvd->gdv_fbs.fbs_fbp == NULL) {
bu_vls_printf(gedp->ged_result_str, "%s listen: framebuffer not open!\n", argv[0]);
return BRLCAD_ERROR;
    }

    /* return the port number */
    if (argc == 2) {
bu_vls_printf(gedp->ged_result_str, "%d", tvd->gdv_fbs.fbs_listener.fbsl_port);
return BRLCAD_OK;
    }

    if (argc == 3) {
int port;

if (bu_sscanf(argv[2], "%d", &port) != 1) {
    bu_vls_printf(gedp->ged_result_str, "listen: bad value - %s\n", argv[2]);
    return BRLCAD_ERROR;
}

if (port >= 0) {
    tvd->gdv_fbs.fbs_is_listening = &tclcad_is_listening;
    tvd->gdv_fbs.fbs_listen_on_port = &tclcad_listen_on_port;
    tvd->gdv_fbs.fbs_open_server_handler = &tclcad_open_server_handler;
    tvd->gdv_fbs.fbs_close_server_handler = &tclcad_close_server_handler;
    tvd->gdv_fbs.fbs_open_client_handler = &tclcad_open_client_handler;
    tvd->gdv_fbs.fbs_close_client_handler = &tclcad_close_client_handler;
    fbs_open(&tvd->gdv_fbs, port);
} else {
    fbs_close(&tvd->gdv_fbs);
}
bu_vls_printf(gedp->ged_result_str, "%d", tvd->gdv_fbs.fbs_listener.fbsl_port);
return BRLCAD_OK;
    }

    bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
    return BRLCAD_ERROR;
}


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
