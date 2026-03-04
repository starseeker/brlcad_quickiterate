/*              P R I M _ D E S C _ T O O L . C P P
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @file prim_desc_tool.cpp
 *
 * qged plugin registration for the descriptor-driven primitive edit panel.
 *
 * Registers a single QgToolPaletteElement backed by a QgPrimDescWidget.
 * The element's do_view_update slot is connected to the application's
 * view_update signal by the palette infrastructure, so the widget
 * automatically re-reads the current selection on every QG_VIEW_SELECT.
 */

#include "common.h"

#include "QgPrimDescWidget.h"
#include "qtcad/QgToolPalette.h"
#include "../../plugin.h"

static void *
prim_desc_tool()
{
    QIcon *obj_icon = new QIcon(QPixmap(":prim_desc.svg"));

    QgPrimDescWidget *w = new QgPrimDescWidget();

    QgToolPaletteElement *el = new QgToolPaletteElement(obj_icon, w);

    /* Forward the internal view-changed signal to the palette element so
     * that the application is notified whenever rt_edit_process() is called
     * (Apply button) or wdb_put_internal() creates a new object (Create
     * button). */
    QObject::connect(w, &QgPrimDescWidget::view_updated,
		     el, &QgToolPaletteElement::element_view_changed);

    /* Forward the palette's do_view_update signal to our slot so we receive
     * QG_VIEW_SELECT events and can update the widget. */
    QObject::connect(el, &QgToolPaletteElement::element_view_update,
		     w, &QgPrimDescWidget::do_view_update);

    return el;
}

extern "C" {

    struct qged_tool_impl prim_desc_tool_impl = {
	prim_desc_tool
    };

    const struct qged_tool prim_desc_tool_s = { &prim_desc_tool_impl, 900 };
    const struct qged_tool *prim_desc_tools[] = { &prim_desc_tool_s, NULL };

    static const struct qged_plugin pinfo = {
	QGED_OC_TOOL_PLUGIN, prim_desc_tools, 1
    };

    COMPILER_DLLEXPORT const struct qged_plugin *qged_plugin_info()
    {
	return &pinfo;
    }

} /* extern "C" */


/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
