/*               D M _ O B O L _ S T U B S . C P P
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
/** @file dm_obol_stubs.cpp
 *
 * Stub implementations of the display manager plugin API for
 * BRLCAD_ENABLE_OBOL builds.  In Obol builds no dm rendering plugins are
 * loaded (all GL/X11/WGL backends are gated with NOT BRLCAD_ENABLE_OBOL) and
 * none of the dm_open family of functions is ever called.  These stubs satisfy
 * the linker for any code that transitively links libdm and includes dm.h, but
 * they do no real work.
 *
 * The counterpart for non-Obol builds is dm_plugins.cpp.
 *
 */

#include "common.h"

#include "bu/str.h"
#include "bu/vls.h"
#include "dm.h"


extern "C" struct dm *
dm_open(void *UNUSED(ctx), void *UNUSED(interp), const char *UNUSED(type),
	int UNUSED(argc), const char **UNUSED(argv))
{
    return DM_NULL;
}


extern "C" int
dm_have_graphics(void)
{
    return 0;
}


extern "C" const char *
dm_graphics_system(const char *UNUSED(dmtype))
{
    return NULL;
}


extern "C" void
dm_list_types(struct bu_vls *list, const char *separator)
{
    /* "nu" is the only display manager in Obol builds — always available */
    if (!list)
	return;
    if (!separator)
	separator = " ";
    if (bu_vls_strlen(list) > 0)
	bu_vls_printf(list, "%s", separator);
    bu_vls_strcat(list, "nu");
}


extern "C" int
dm_validXType(const char *UNUSED(dpy_string), const char *name)
{
    /* Only "nu"/"null" is valid in Obol builds */
    return (BU_STR_EQUIV(name, "nu") || BU_STR_EQUIV(name, "null")) ? 1 : 0;
}


extern "C" int
dm_valid_type(const char *name, const char *dpy_string)
{
    return dm_validXType(dpy_string, name);
}


extern "C" const char *
dm_bestXType(const char *UNUSED(dpy_string))
{
    return "nu";
}


extern "C" const char *
dm_default_type(void)
{
    return "nu";
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
