/*                       D O E V E N T . C
 * BRL-CAD
 *
 * Copyright (c) 2004-2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file mged/doevent.c
 *
 * X event handling routines for MGED.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <math.h>

#ifdef HAVE_GL_DEVICE_H
#  include <gl/device.h>
#endif

#include "tcl.h"
#ifdef HAVE_TK
#  include "tk.h"
#endif

#ifdef HAVE_X11_XLIB_H
#  include <X11/Xutil.h>
#  include <X11/keysym.h>
#endif
#ifdef HAVE_X11_EXTENSIONS_XINPUT_H
#  include <X11/extensions/XI.h>
#  include <X11/extensions/XInput.h>
#endif

#include "vmath.h"
#include "raytrace.h"
#include "ged.h"
#include "bsg/util.h"

#include "./mged.h"
#include "./mged_dm.h"
#include "./sedit.h"

// FIXME: Global
extern int doMotion;			/* defined in buttons.c */

#ifdef HAVE_X11_TYPES
static void motion_event_handler(struct mged_state *, XMotionEvent *);
#endif

#ifdef HAVE_X11_TYPES
int
doEvent(ClientData UNUSED(clientData), XEvent *UNUSED(eventPtr))
{
    /* Step 8: libdm removed from MGED.  All panes are Obol panes;
     * X11 events are handled by the Obol/Qt event loop, not libdm.
     * This function is retained for the Tcl event binding only. */
    return TCL_OK;
}
#else
int
doEvent(ClientData UNUSED(clientData), void *UNUSED(eventPtr)) {
    return TCL_OK;
}
#endif /* HAVE_X11_XLIB_H */

#ifdef HAVE_X11_TYPES
/* Step 8: motion_event_handler removed — libdm mouse event dispatch
 * is no longer used.  Obol panes handle mouse events via Qt/Tk bindings.
 * The function prototype is kept to satisfy the forward declaration above. */
static void
motion_event_handler(struct mged_state *UNUSED(s), XMotionEvent *UNUSED(xmotion))
{
    /* no-op */
}
#endif /* HAVE_X11_XLIB_H */
