/*                       R E F R E S H . C
 * BRL-CAD
 *
 * Copyright (c) 2000-2025 United States Government as represented by
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
/** @addtogroup libtclcad */
/** @{ */
/** @file libtclcad/views/refresh.c
 *
 */
/** @} */

#include "common.h"
#include "bu/units.h"
#include "ged.h"
#include "tclcad.h"

/* Private headers */
#include "../tclcad_private.h"
#include "../view/view.h"

void
go_refresh_draw(struct ged *gedp, bsg_view *gdvp, int restore_zbuffer)
{
    /* dm rendering path removed; all dm_ draw calls eliminated. */
    (void)gedp;
    (void)gdvp;
    (void)restore_zbuffer;
}

void
go_refresh(struct ged *gedp, bsg_view *gdvp)
{
    /* dm rendering path removed; only Obol path is active. */
    (void)gedp;
    (void)gdvp;
}

void
to_refresh_view(bsg_view *gdvp)
{

    if (current_top == NULL)
	return;

    struct tclcad_ged_data *tgd = (struct tclcad_ged_data *)current_top->to_gedp->u_data;
    if (!tgd->go_dmv.refresh_on)
	return;

    if (to_is_viewable(gdvp))
	go_refresh(current_top->to_gedp, gdvp);
}

void
to_refresh_all_views(struct tclcad_obj *top)
{
    bsg_view *gdvp;

    struct bu_ptbl *views = bsg_scene_views(&top->to_gedp->ged_views);
    for (size_t i = 0; i < BU_PTBL_LEN(views); i++) {
	gdvp = (bsg_view *)BU_PTBL_GET(views, i);
	to_refresh_view(gdvp);
    }

    /* Obol path: notify obol_view Tk widgets to re-render.
     * When apps (archer, etc.) use obol_view for display, to_refresh_view
     * above is a no-op because gdvp->dmp is null.  obol_notify_views
     * ensures all live obol_view widgets call obol_scene_assemble + render. */
    if (top && top->to_gedp && top->to_interp)
	(void)Tcl_Eval(top->to_interp, "catch {obol_notify_views}");
}

int
to_refresh(struct ged *gedp,
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

    if (argc != 2) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return BRLCAD_ERROR;
    }

    return to_handle_refresh(gedp, argv[1]);
}


int
to_refresh_all(struct ged *gedp,
	       int argc,
	       const char *argv[],
	       ged_func_ptr UNUSED(func),
	       const char *UNUSED(usage),
	       int UNUSED(maxargs))
{
    if (argc != 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s", argv[0]);
	return BRLCAD_ERROR;
    }

    to_refresh_all_views(current_top);

    return BRLCAD_OK;
}


int
to_refresh_on(struct ged *gedp,
	      int argc,
	      const char *argv[],
	      ged_func_ptr UNUSED(func),
	      const char *UNUSED(usage),
	      int UNUSED(maxargs))
{
    int on;
    struct tclcad_ged_data *tgd = (struct tclcad_ged_data *)current_top->to_gedp->u_data;

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    if (2 < argc) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s", argv[0]);
	return BRLCAD_ERROR;
    }

    /* Get refresh_on state */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "%d", tgd->go_dmv.refresh_on);
	return BRLCAD_OK;
    }

    /* Set refresh_on state */
    if (bu_sscanf(argv[1], "%d", &on) != 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s", argv[0]);
	return BRLCAD_ERROR;
    }

    tgd->go_dmv.refresh_on = on;

    return BRLCAD_OK;
}

int
to_handle_refresh(struct ged *gedp,
		  const char *name)
{
    bsg_view *gdvp;

    gdvp = bsg_scene_find_view(&gedp->ged_views, name);
    if (!gdvp) {
	bu_vls_printf(gedp->ged_result_str, "View not found - %s", name);
	return BRLCAD_ERROR;
    }

    to_refresh_view(gdvp);

    return BRLCAD_OK;
}


void
to_refresh_handler(void *clientdata)
{
    bsg_view *gdvp = (bsg_view *)clientdata;

    /* Possibly do more here */

    to_refresh_view(gdvp);
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
