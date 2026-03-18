/*                  D M _ P L U G I N S . C P P
 * BRL-CAD
 *
 * Copyright (c) 2020-2025 United States Government as represented by
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
/** @file dm_plugins.cpp
 *
 * Display manager plugin API entry points (dm_open, dm_have_graphics, etc.).
 * These use the dm_backends map populated by dm_init.cpp and are excluded from
 * BRLCAD_ENABLE_OBOL builds where no dm rendering plugins are present.  Obol
 * builds use dm_obol_stubs.cpp instead.
 *
 * The framebuffer counterparts (fb_open, fb_set_interface, etc.) live in
 * fb_plugins.cpp and are always compiled.
 *
 */

#include "common.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <cstdio>

#include "bu/app.h"
#include "bu/dylib.h"
#include "bu/file.h"
#include "bu/log.h"
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"

#include "dm.h"
#include "./include/private.h"


extern "C" struct dm *
dm_open(void *ctx, void *interp, const char *type, int argc, const char *argv[])
{
    if (BU_STR_EQUIV(type, "nu") || BU_STR_EQUIV(type, "null")) {
	return dm_null.i->dm_open(ctx , interp, argc, argv);
    }

    std::map<std::string, const struct dm *> *dmb = (std::map<std::string, const struct dm *> *)dm_backends;
    std::string key(type);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
    std::map<std::string, const struct dm *>::iterator d_it = dmb->find(key);
    if (d_it == dmb->end()) {
	return DM_NULL;
    }

    const struct dm *d = d_it->second;
    struct dm *dmp = d->i->dm_open(ctx, interp, argc, argv);
    return dmp;
}


extern "C" int
dm_have_graphics()
{
    int ret = 0;

    std::map<std::string, const struct dm *> *dmb = (std::map<std::string, const struct dm *> *)dm_backends;
    std::map<std::string, const struct dm *>::iterator d_it;

    for (d_it = dmb->begin(); d_it != dmb->end(); d_it++) {
	std::string key = d_it->first;
	const struct dm *d = d_it->second;
	if (dm_graphical(d)) {
	    ret = 1;
	    break;
	}
    }

    return ret;
}


extern "C" const char *
dm_graphics_system(const char *dmtype)
{
    const char *ret = NULL;

    std::map<std::string, const struct dm *> *dmb = (std::map<std::string, const struct dm *> *)dm_backends;
    std::map<std::string, const struct dm *>::iterator d_it;

    for (d_it = dmb->begin(); d_it != dmb->end(); d_it++) {
	std::string key = d_it->first;
	const struct dm *d = d_it->second;
	const char *dname = dm_get_dm_name(d);
	if (BU_STR_EQUIV(dmtype, dname)) {
	    ret = dm_get_graphics_system(d);
	    break;
	}
    }

    return ret;
}


static const char *priority_list[] = {"wgl", "ogl", "X", NULL};


extern "C" void
dm_list_types(struct bu_vls *list, const char *separator)
{
    if (!list) {
	return;
    }

    if (!separator)
	separator = " ";

    std::map<std::string, const struct dm *> *dmb = (std::map<std::string, const struct dm *> *)dm_backends;

    std::set<std::string> checked;

    /* First, do the priority list (Tcl/Tk client codes expect the reported output to be
     * in order of "most preferred" interface */
    int i = 0;
    const char *b = priority_list[i];
    while (b) {
	std::string key(b);
	std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
	checked.insert(key);
	std::map<std::string, const struct dm *>::iterator d_it = dmb->find(key);
	if (d_it == dmb->end()) {
	    i++;
	    b = priority_list[i];
	    continue;
	}
	const struct dm *d = d_it->second;
	const char *dname = dm_get_name(d);
	if (dname) {
	    if (strlen(bu_vls_cstr(list)) > 0)
		bu_vls_printf(list, "%s", separator);
	    bu_vls_printf(list, "%s", dname);
	}
	i++;
	b = priority_list[i];
    }

    /* Report anything not included in the priority list but still available */
    const char *cmd2 = getenv("DM_SWRAST");
    int report_swrast = 0;
    if (BU_STR_EQUAL(cmd2, "1"))
	report_swrast = 1;

    std::map<std::string, const struct dm *>::iterator d_it;
    for (d_it = dmb->begin(); d_it != dmb->end(); d_it++) {
	if (checked.find(d_it->first) != checked.end()) {
	    continue;
	}
	const struct dm *d = d_it->second;
	const char *dname = dm_get_name(d);
	if (dname) {
	    if (BU_STR_EQUAL(dname, "swrast") && !report_swrast)
		continue;
	    if (strlen(bu_vls_cstr(list)) > 0)
		bu_vls_printf(list, "%s", separator);
	    bu_vls_printf(list, "%s", dname);
	}
    }

    /* Null is always available */
    if (strlen(bu_vls_cstr(list)) > 0)
	bu_vls_printf(list, "%s", separator);
    bu_vls_strcat(list, "nu");
}


extern "C" int
dm_validXType(const char *dpy_string, const char *name)
{
    if (BU_STR_EQUIV(name, "nu") || BU_STR_EQUIV(name, "null")) {
	return 1;
    }

    std::map<std::string, const struct dm *> *dmb = (std::map<std::string, const struct dm *> *)dm_backends;
    std::string key(name);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
    std::map<std::string, const struct dm *>::iterator d_it = dmb->find(key);

    if (d_it == dmb->end()) {
	return 0;
    }

    const char *cmd2 = getenv("DM_SWRAST");
    int report_swrast = 0;
    if (BU_STR_EQUAL(cmd2, "1"))
	report_swrast = 1;
    if (BU_STR_EQUAL(name, "swrast") && !report_swrast)
	return 0;

    const struct dm *d = d_it->second;
    int is_valid = d->i->dm_viable(dpy_string);

    return is_valid;
}


extern "C" int
dm_valid_type(const char *name, const char *dpy_string)
{
    return dm_validXType(dpy_string, name);
}


/**
 * dm_bestXType determines what mged will normally use as the default display
 * manager.  Checks if the display manager backend can work at runtime, if the
 * backend supports that check, and will report the "best" available WORKING
 * backend rather than simply the first one present in the list that is also
 * in the plugins directory.
 *
 * Defaults to "nu" if nothing else is found - nu is compiled into libdm itself
 * and thus always viable, even if no plugins can be found.
 */
extern "C" const char *
dm_bestXType(const char *dpy_string)
{
    std::map<std::string, const struct dm *> *dmb = (std::map<std::string, const struct dm *> *)dm_backends;
    const char *ret = NULL;

    int i = 0;
    const char *b = priority_list[i];

    while (b) {
	std::string key(b);
	std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
	std::map<std::string, const struct dm *>::iterator d_it = dmb->find(key);

	if (d_it == dmb->end()) {
	    i++;
	    b = priority_list[i];
	    continue;
	}
	const struct dm *d = d_it->second;
	if (d->i->dm_viable(dpy_string) == 1) {
	    ret = b;
	    break;
	}
	i++;
	b = priority_list[i];
    }
    if (!ret)
	ret = "nu";

    return ret;
}


/**
 * dm_default_type suggests a display manager.  Checks if a plugin supplies the
 * specified backend type before reporting it, but does NOT perform a runtime
 * test to verify its suggestion will work (unlike dm_bestXType) before
 * reporting back.
 *
 * Defaults to "nu" if nothing else is found - nu is compiled into libdm itself
 * and thus always available, even if no plugins can be found.
 */
extern "C" const char *
dm_default_type()
{
    std::map<std::string, const struct dm *> *dmb = (std::map<std::string, const struct dm *> *)dm_backends;
    const char *ret = NULL;

    int i = 0;
    const char *b = priority_list[i];

    while (b) {
	std::string key(b);
	std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
	std::map<std::string, const struct dm *>::iterator d_it = dmb->find(key);

	if (d_it == dmb->end()) {
	    i++;
	    b = priority_list[i];
	    continue;
	}
	ret = b;
	break;
    }
    if (!ret)
	ret = "nu";

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
