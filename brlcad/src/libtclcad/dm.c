/*                            D M . C
 * BRL-CAD
 *
 * Copyright (c) 2004-2025 United States Government as represented by
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
/**
 *
 * Tcl logic specific to libdm — stubbed out (dm backend going away).
 *
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "tcl.h"
#include "vmath.h"
#include "bu/cmd.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/observer.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "tclcad.h"
/* private headers */
#include "brlcad_version.h"
#include "./tclcad_private.h"

/* forward declarations — dm types used only as opaque pointers */
struct dm;
struct rt_wdb;

/**
 *@brief
 * A display manager object is used for interacting with a display manager.
 */
struct dm_obj {
    struct bu_list l;
    struct bu_vls dmo_name;             /* display manager object name/cmd */
    struct dm *dmo_dmp;                 /* display manager pointer (NULL for stub) */
    struct bu_observer_list dmo_observers; /* observers */
    mat_t viewMat;
    int (*dmo_drawLabelsHook)(struct dm *, struct rt_wdb *, const char *, mat_t, int *, ClientData);
    void *dmo_drawLabelsHookClientData;
    Tcl_Interp *interp;
};


static struct dm_obj HeadDMObj; /* head of display manager object list */


/* ------------------------------------------------------------------ */
/* All drawing / configuration subcommands: no-op stubs.              */
/* ------------------------------------------------------------------ */

static int
dmo_bg_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_bounds_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_clear_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_configure_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_debug_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_depthMask_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_drawBegin_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_drawEnd_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_drawLine_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_drawPoint_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_drawScale_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_drawSList_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_drawString_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_drawVList_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_drawDataAxes_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_drawModelAxes_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_drawViewAxes_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_drawCenterDot_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_fg_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_flush_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_get_aspect_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_getDrawLabelsHook_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_light_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_lineStyle_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_lineWidth_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_loadmat_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_logfile_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_normal_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_observer_tcl(void *clientData, int argc, const char **argv)
{
    struct dm_obj *dmop = (struct dm_obj *)clientData;

    if (!dmop || !dmop->interp)
return BRLCAD_ERROR;

    if (argc < 3)
return BRLCAD_ERROR;

    return bu_observer_cmd((ClientData)&dmop->dmo_observers, argc-2, (const char **)argv+2);
}

static int
dmo_perspective_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_png_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_clearBufferAfter_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_setDrawLabelsHook_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_size_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_sync_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_transparency_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_zbuffer_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
dmo_zclip_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}


/* ------------------------------------------------------------------ */
/* Public functions that are also stubs (no dm_* calls).              */
/* ------------------------------------------------------------------ */

int
dmo_drawScale_cmd(struct dm_obj *UNUSED(dmop),
  int UNUSED(argc),
  const char **UNUSED(argv))
{
    return BRLCAD_OK;
}


/*
 * Called by Tcl when the object is destroyed.
 */
static void
dmo_deleteProc(ClientData clientData)
{
    struct dm_obj *dmop = (struct dm_obj *)clientData;

    bu_observer_free(&dmop->dmo_observers);
    bu_vls_free(&dmop->dmo_name);
    BU_LIST_DEQUEUE(&dmop->l);
    bu_free((void *)dmop, "dmo_deleteProc: dmop");
}


/*
 * Generic interface for display manager object routines.
 * Usage:
 * objname cmd ?args?
 *
 * Returns: result of DM command.
 */
static int
dmo_cmd(ClientData clientData, Tcl_Interp *UNUSED(interp), int argc, const char **argv)
{
    int ret;

    static struct bu_cmdtab dmo_cmds[] = {
{"bg",dmo_bg_tcl},
{"bounds",dmo_bounds_tcl},
{"clear",dmo_clear_tcl},
{"configure",dmo_configure_tcl},
{"debug",dmo_debug_tcl},
{"depthMask",dmo_depthMask_tcl},
{"drawBegin",dmo_drawBegin_tcl},
{"drawEnd",dmo_drawEnd_tcl},
{"drawGeom",BU_CMD_NULL},
{"drawLabels",BU_CMD_NULL},
{"drawLine",dmo_drawLine_tcl},
{"drawPoint",dmo_drawPoint_tcl},
{"drawScale",dmo_drawScale_tcl},
{"drawSList",dmo_drawSList_tcl},
{"drawString",dmo_drawString_tcl},
{"drawVList",dmo_drawVList_tcl},
{"drawDataAxes",dmo_drawDataAxes_tcl},
{"drawModelAxes",dmo_drawModelAxes_tcl},
{"drawViewAxes",dmo_drawViewAxes_tcl},
{"drawCenterDot",dmo_drawCenterDot_tcl},
{"fg",dmo_fg_tcl},
{"flush",dmo_flush_tcl},
{"get_aspect",dmo_get_aspect_tcl},
{"getDrawLabelsHook",dmo_getDrawLabelsHook_tcl},
{"light",dmo_light_tcl},
{"linestyle",dmo_lineStyle_tcl},
{"linewidth",dmo_lineWidth_tcl},
{"loadmat",dmo_loadmat_tcl},
{"logfile",dmo_logfile_tcl},
{"normal",dmo_normal_tcl},
{"observer",dmo_observer_tcl},
{"perspective",dmo_perspective_tcl},
{"png",        dmo_png_tcl},
{"clearBufferAfter",    dmo_clearBufferAfter_tcl},
{"setDrawLabelsHook",dmo_setDrawLabelsHook_tcl},
{"size",dmo_size_tcl},
{"sync",dmo_sync_tcl},
{"transparency",dmo_transparency_tcl},
{"zbuffer",dmo_zbuffer_tcl},
{"zclip",dmo_zclip_tcl},
{(const char *)NULL, BU_CMD_NULL}
    };

    if (bu_cmd(dmo_cmds, argc, argv, 1, clientData, &ret) == BRLCAD_OK)
return ret;

    bu_log("ERROR: '%s' command not found\n", argv[1]);
    return BRLCAD_ERROR;
}


/*
 * Open/create a display manager object.
 *
 * Usage:
 * dm_open [name type [args]]
 *
 * The dm backend is going away; all types result in a stub dm_obj
 * with dmo_dmp = NULL.  Draw sub-commands are no-ops.
 */
static int
dmo_open_tcl(ClientData UNUSED(clientData), Tcl_Interp *interp, int argc, char **argv)
{
    struct dm_obj *dmop;
    struct bu_vls vls = BU_VLS_INIT_ZERO;
    int name_index = 1;
    Tcl_Obj *obj;

    obj = Tcl_GetObjResult(interp);
    if (Tcl_IsShared(obj))
obj = Tcl_DuplicateObj(obj);

    if (argc == 1) {
/* get list of display manager objects */
for (BU_LIST_FOR(dmop, dm_obj, &HeadDMObj.l))
    Tcl_AppendStringsToObj(obj, bu_vls_addr(&dmop->dmo_name), " ", (char *)NULL);

Tcl_SetObjResult(interp, obj);
return BRLCAD_OK;
    }

    if (argc < 3) {
bu_vls_printf(&vls, "helplib_alias dm_open %s", argv[1]);
Tcl_Eval(interp, bu_vls_addr(&vls));
bu_vls_free(&vls);
return BRLCAD_ERROR;
    }

    /* check to see if display manager object exists */
    for (BU_LIST_FOR(dmop, dm_obj, &HeadDMObj.l)) {
if (BU_STR_EQUAL(argv[name_index], bu_vls_addr(&dmop->dmo_name))) {
    Tcl_AppendStringsToObj(obj, "dmo_open: ", argv[name_index],
   " exists.", (char *)NULL);
    Tcl_SetObjResult(interp, obj);
    return BRLCAD_ERROR;
}
    }

    /* acquire dm_obj struct */
    BU_ALLOC(dmop, struct dm_obj);

    /* initialize dm_obj — dmo_dmp is always NULL (dm backend going away) */
    bu_vls_init(&dmop->dmo_name);
    bu_vls_strcpy(&dmop->dmo_name, argv[name_index]);
    dmop->dmo_dmp = NULL;
    dmop->dmo_drawLabelsHook = (int (*)(struct dm *, struct rt_wdb *, const char *, mat_t, int *, ClientData))0;
    dmop->interp = interp;

    /* append to list of dm_obj's */
    BU_LIST_APPEND(&HeadDMObj.l, &dmop->l);

    (void)Tcl_CreateCommand(interp,
    bu_vls_addr(&dmop->dmo_name),
    (Tcl_CmdProc *)dmo_cmd,
    (ClientData)dmop,
    dmo_deleteProc);

    /* Return new function name as result */
    Tcl_SetResult(interp, bu_vls_addr(&dmop->dmo_name), TCL_VOLATILE);
    return BRLCAD_OK;
}


int
Dmo_Init(Tcl_Interp *interp)
{
    BU_LIST_INIT(&HeadDMObj.l);
    BU_VLS_INIT(&HeadDMObj.dmo_name);
    (void)Tcl_CreateCommand(interp, "dm_open", (Tcl_CmdProc *)dmo_open_tcl, (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);

    return BRLCAD_OK;
}


static int
dm_validXType_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}


static int
dm_bestXType_tcl(void *UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

/**
 * Hook function wrapper to the fb_common_file_size Tcl command — stub.
 */
int
fb_cmd_common_file_size(ClientData UNUSED(clientData), int UNUSED(argc), const char **UNUSED(argv))
{
    return TCL_OK;
}

static int
wrapper_func(ClientData data, Tcl_Interp *interp, int argc, const char *argv[])
{
    struct bu_cmdtab *ctp = (struct bu_cmdtab *)data;

    return ctp->ct_func(interp, argc, argv);
}

static void
register_cmds(Tcl_Interp *interp, struct bu_cmdtab *cmds)
{
    struct bu_cmdtab *ctp = NULL;

    for (ctp = cmds; ctp->ct_name != (char *)NULL; ctp++) {
(void)Tcl_CreateCommand(interp, ctp->ct_name, wrapper_func, (ClientData)ctp, (Tcl_CmdDeleteProc *)NULL);
    }
}


int
Dm_Init(Tcl_Interp *interp)
{
    static struct bu_cmdtab cmdtab[] = {
{"dm_validXType", dm_validXType_tcl},
{"dm_bestXType", dm_bestXType_tcl},
{"fb_common_file_size", fb_cmd_common_file_size},
{(const char *)NULL, BU_CMD_NULL}
    };

    /* register commands */
    register_cmds(interp, cmdtab);

    /* initialize display manager object code */
    Dmo_Init(interp);

    /* initialize framebuffer object code */
    Fbo_Init(interp);

    Tcl_PkgProvide(interp,  "Dm", brlcad_version());
    Tcl_PkgProvide(interp,  "Fb", brlcad_version());

    return BRLCAD_OK;
}

/**
 * @brief
 * A TCL interface to dm_list_types() — stub.
 *
 * @return empty string (dm backend going away).
 */
int
dm_list_tcl(ClientData UNUSED(clientData),
    Tcl_Interp *interp,
    int UNUSED(argc),
    const char **UNUSED(argv))
{
    Tcl_SetResult(interp, "", TCL_STATIC);
    return TCL_OK;
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
