/*           Q G E D L E G A C Y L O A D E R . C P P
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
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
/** @file QgEdLegacyLoader.cpp
 *
 * Centralized loader for legacy qged_plugin_info shared-library plugins.
 * See QgEdLegacyLoader.h for design rationale.
 */

#include "common.h"

#include <map>
#include <set>

#include <QPushButton>
#include <QIcon>

#include "bu/app.h"
#include "bu/dylib.h"
#include "bu/file.h"
#include "bu/str.h"
#include "bu/vls.h"

#include "qtcad/QgToolPalette.h"

#include "plugins/plugin.h"
#include "QgEdPalette.h"
#include "QgEdLegacyLoader.h"

QgEdLegacyLoader::QgEdLegacyLoader(QObject *par)
    : QObject(par)
{
}

QgEdLegacyLoader::~QgEdLegacyLoader()
{
    for (void *h : m_handles)
	bu_dlclose(h);
    m_handles.clear();
}

void
QgEdLegacyLoader::populate(QgEdPalette *vc, QgEdPalette *oc)
{
    if (!vc || !oc)
	return;

    /* Map: palette_priority -> elements, one map per palette type */
    std::map<int, std::set<QgToolPaletteElement *>> vc_map;
    std::map<int, std::set<QgToolPaletteElement *>> oc_map;

    const char *ppath = bu_dir(NULL, 0, BU_DIR_LIBEXEC, "qged", NULL);
    char **filenames = NULL;
    struct bu_vls plugin_pattern = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&plugin_pattern, "*%s", QGED_PLUGIN_SUFFIX);
    size_t nfiles = bu_file_list(ppath, bu_vls_cstr(&plugin_pattern), &filenames);
    bu_vls_free(&plugin_pattern);

    for (size_t i = 0; i < nfiles; i++) {
	char pfile[MAXPATHLEN] = {0};
	bu_dir(pfile, MAXPATHLEN, BU_DIR_LIBEXEC, "qged", filenames[i], NULL);

	void *dl_handle = bu_dlopen(pfile, BU_RTLD_NOW);
	if (!dl_handle) {
	    const char *err = bu_dlerror();
	    if (err)
		bu_log("%s\n", err);
	    bu_log("Unable to dynamically load '%s' (skipping)\n", pfile);
	    continue;
	}

	const char *psymbol = "qged_plugin_info";
	void *info_val = bu_dlsym(dl_handle, psymbol);
	const struct qged_plugin *(*plugin_info)() =
	    (const struct qged_plugin *(*)())(intptr_t)info_val;
	if (!plugin_info) {
	    /* Not a legacy qged_plugin_info plugin — may be a Qt plugin.
	     * Close the handle; QgPluginManager will load it separately. */
	    bu_dlclose(dl_handle);
	    continue;
	}

	const struct qged_plugin *plugin = plugin_info();
	if (!plugin || !plugin->cmds || !plugin->cmd_cnt) {
	    bu_log("Invalid or empty legacy plugin '%s' (skipping)\n", pfile);
	    bu_dlclose(dl_handle);
	    continue;
	}

	/* Determine the palette type from the api_version field. */
	uint32_t ptype = *((const uint32_t *)plugin);
	if (ptype != (uint32_t)QGED_VC_TOOL_PLUGIN &&
	    ptype != (uint32_t)QGED_OC_TOOL_PLUGIN) {
	    /* Command-only or future plugin type — not a palette tool. */
	    bu_dlclose(dl_handle);
	    continue;
	}

	/* Own this handle from here on. */
	m_handles.push_back(dl_handle);

	std::map<int, std::set<QgToolPaletteElement *>> &pmap =
	    (ptype == (uint32_t)QGED_VC_TOOL_PLUGIN) ? vc_map : oc_map;

	const struct qged_tool **cmds = plugin->cmds;
	for (int c = 0; c < plugin->cmd_cnt; c++) {
	    const struct qged_tool *cmd = cmds[c];
	    if (!cmd || !cmd->i || !cmd->i->tool_create)
		continue;
	    QgToolPaletteElement *el =
		(QgToolPaletteElement *)(*cmd->i->tool_create)();
	    if (el)
		pmap[cmd->palette_priority].insert(el);
	}
    }

    if (filenames)
	bu_argv_free(nfiles, filenames);

    /* Add elements to vc in priority order. */
    for (auto &kv : vc_map)
	for (QgToolPaletteElement *el : kv.second)
	    vc->addElement(el);

    /* Add elements to oc in priority order. */
    for (auto &kv : oc_map)
	for (QgToolPaletteElement *el : kv.second)
	    oc->addElement(el);

    /* Placeholder for the object-editing palette until real oc tools
     * are ported.  This mirrors the old QgEdPalette constructor behavior. */
    {
	QIcon *obj_icon = new QIcon();
	QString obj_label("primitive controls ");
	QPushButton *obj_control = new QPushButton(obj_label);
	QgToolPaletteElement *el = new QgToolPaletteElement(obj_icon, obj_control);
	oc->addElement(el);
    }
}

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
