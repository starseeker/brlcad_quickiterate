/*                      P L U G I N . H
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
 *
 * TODO - beyond the current QgToolPaletteElement type, three more
 * plugin output categories would be useful to support:
 *
 * 1.  ged-style command line commands  (maybe should expand libged
 *     plugin setup to support hooking them in at that level, but may
 *     also want awareness of app level info in commands...)
 * 2.  QDockWidget items, equivalent to (say) the standard attributes
 *     dialog and able to be docked in the main GUI.
 * 3.  Full-fledged dialogs that are their own windows and launched
 *     from the menu.  Candidates might include complex procedural geometry
 *     generation tools, tabular report generators, or other large
 *     graphical layout scenarios that won't fit well as a widget in
 *     the main GUI.
 */

#ifndef QGED_PLUGIN_H
#define QGED_PLUGIN_H

#include "common.h"

/* Plugin ABI version.  Increment this when the plugin interface changes in
 * a way that is not backward compatible.  A plugin's api_version field must
 * equal this value for the plugin to be accepted by the loader. */
#define QGED_PLUGIN_API_VERSION 1

/* Plugin type constants.  These identify what kind of plugin this is and
 * therefore which palette will load it.  Each constant is a small stable
 * integer, independent of the BRL-CAD release version. */
#define QGED_CMD_PLUGIN     3  /* GED-style command-line tool */
#define QGED_VC_TOOL_PLUGIN 4  /* View-Canvas interaction tool (select, measure, ...) */
#define QGED_OC_TOOL_PLUGIN 6  /* Object-Control edit tool (primitive editing) */

/* Signature of the factory function every tool must supply.  It creates and
 * returns the tool's QgToolPaletteElement (cast to void* for C linkage). */
typedef void * (*qged_func_ptr)(void);

/* Describes a single tool contributed by a plugin. */
struct qged_tool {
    const char *name;           /* Human-readable name used in logs/tooltips */
    qged_func_ptr tool_create;  /* Factory: returns a QgToolPaletteElement* */
    int palette_priority;       /* Sort order within the palette (lower = first) */
};

/* Top-level descriptor returned by qged_plugin_info().
 *
 * plugin_type MUST be the first field so the loader can identify the plugin
 * type with a single uint32_t read before doing full validation.
 *
 * api_version must equal QGED_PLUGIN_API_VERSION.
 *
 * cmds is a NULL-terminated array of qged_tool pointers; cmd_cnt gives the
 * number of valid entries (not counting the terminating NULL). */
struct qged_plugin {
    uint32_t plugin_type;              /* must be first: one of QGED_*_PLUGIN */
    uint32_t api_version;              /* must equal QGED_PLUGIN_API_VERSION  */
    const struct qged_tool ** const cmds;
    int cmd_cnt;
};

#endif  /* QGED_PLUGIN_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
